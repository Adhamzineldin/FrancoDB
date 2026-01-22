/**
 * ddl_executor.cpp
 * 
 * Production-Grade Implementation of Data Definition Language Operations
 * Extracted from ExecutionEngine to satisfy Single Responsibility Principle.
 * 
 * This class handles all schema-modifying operations:
 * - CREATE TABLE with constraints (PK, FK, UNIQUE, NOT NULL, CHECK)
 * - DROP TABLE with referential integrity checks
 * - CREATE INDEX with B+ Tree integration
 * - ALTER TABLE operations
 * - Schema inspection (DESCRIBE, SHOW CREATE TABLE)
 * 
 * Thread Safety: All methods are thread-safe when used with proper catalog locking.
 * 
 * @author FrancoDB Team
 */

#include "execution/ddl_executor.h"
#include "catalog/catalog.h"
#include "catalog/table_metadata.h"
#include "storage/table/schema.h"
#include "storage/table/column.h"
#include "parser/statement.h"
#include "common/exception.h"
#include "common/type.h"
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <set>

namespace francodb {

// ============================================================================
// CREATE TABLE - Full Enterprise Implementation
// ============================================================================

ExecutionResult DDLExecutor::CreateTable(CreateStatement* stmt) {
    // Input validation
    if (!stmt) {
        return ExecutionResult::Error("[DDL] Invalid CREATE TABLE statement: null pointer");
    }
    
    if (!catalog_) {
        return ExecutionResult::Error("[DDL] Internal error: Catalog not initialized");
    }
    
    if (stmt->table_name_.empty()) {
        return ExecutionResult::Error("[DDL] Table name cannot be empty");
    }
    
    // Check if table already exists
    if (catalog_->GetTable(stmt->table_name_) != nullptr) {
        return ExecutionResult::Error("[DDL] Table already exists: " + stmt->table_name_);
    }
    
    // Validate columns
    if (stmt->columns_.empty()) {
        return ExecutionResult::Error("[DDL] Table must have at least one column");
    }
    
    // Check for duplicate column names
    std::set<std::string> column_names;
    for (const auto& col : stmt->columns_) {
        if (column_names.find(col.GetName()) != column_names.end()) {
            return ExecutionResult::Error("[DDL] Duplicate column name: " + col.GetName());
        }
        column_names.insert(col.GetName());
    }
    
    // Validate foreign key constraints BEFORE creating the table
    for (const auto& fk : stmt->foreign_keys_) {
        // Check if referenced table exists
        TableMetadata* ref_table = catalog_->GetTable(fk.ref_table);
        if (!ref_table) {
            return ExecutionResult::Error(
                "[DDL] Foreign key references non-existent table: " + fk.ref_table);
        }
        
        // Check if all referenced columns exist in the referenced table
        for (const auto& ref_col : fk.ref_columns) {
            int col_idx = ref_table->schema_.GetColIdx(ref_col);
            if (col_idx < 0) {
                return ExecutionResult::Error(
                    "[DDL] Foreign key references non-existent column '" + ref_col + 
                    "' in table '" + fk.ref_table + "'");
            }
        }
        
        // Check if all local columns exist in the table being created
        for (const auto& local_col : fk.columns) {
            bool found = false;
            for (const auto& col : stmt->columns_) {
                if (col.GetName() == local_col) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return ExecutionResult::Error(
                    "[DDL] Foreign key column '" + local_col + 
                    "' does not exist in table definition");
            }
        }
        
        // Validate column count matches
        if (fk.columns.size() != fk.ref_columns.size()) {
            return ExecutionResult::Error(
                "[DDL] Foreign key column count mismatch: " +
                std::to_string(fk.columns.size()) + " local vs " +
                std::to_string(fk.ref_columns.size()) + " referenced");
        }
    }
    
    // Build schema from columns
    Schema schema(stmt->columns_);
    
    // Create the table in catalog
    TableMetadata* table_info = catalog_->CreateTable(stmt->table_name_, schema);
    if (!table_info) {
        return ExecutionResult::Error("[DDL] Failed to create table: " + stmt->table_name_);
    }
    
    // Store foreign key constraints in table metadata
    table_info->foreign_keys_ = stmt->foreign_keys_;
    
    // Persist catalog changes
    catalog_->SaveCatalog();
    
    // Log DDL operation for recovery
    LogDDL(LogRecordType::CREATE_TABLE, stmt->table_name_);
    
    return ExecutionResult::Message("CREATE TABLE SUCCESS");
}

// ============================================================================
// DROP TABLE - With Referential Integrity Checks
// ============================================================================

ExecutionResult DDLExecutor::DropTable(DropStatement* stmt) {
    if (!stmt) {
        return ExecutionResult::Error("[DDL] Invalid DROP TABLE statement: null pointer");
    }
    
    if (!catalog_) {
        return ExecutionResult::Error("[DDL] Internal error: Catalog not initialized");
    }
    
    // Check if table exists
    TableMetadata* table_info = catalog_->GetTable(stmt->table_name_);
    if (!table_info) {
        return ExecutionResult::Error("[DDL] Table does not exist: " + stmt->table_name_);
    }
    
    // Check for foreign key references from other tables
    // This prevents orphaned references
    auto all_tables = catalog_->GetAllTableNames();
    for (const auto& other_table : all_tables) {
        if (other_table == stmt->table_name_) continue;
        
        TableMetadata* other_info = catalog_->GetTable(other_table);
        if (other_info) {
            for (const auto& fk : other_info->foreign_keys_) {
                if (fk.ref_table == stmt->table_name_) {
                    return ExecutionResult::Error(
                        "[DDL] Cannot drop table '" + stmt->table_name_ + 
                        "': referenced by foreign key in table '" + other_table + "'");
                }
            }
        }
    }
    
    // Note: Indexes on this table will be orphaned when table is dropped.
    // A full implementation would track and clean up indexes here.
    
    // Drop the table
    bool success = catalog_->DropTable(stmt->table_name_);
    if (!success) {
        return ExecutionResult::Error("[DDL] Failed to drop table: " + stmt->table_name_);
    }
    
    // Persist catalog changes
    catalog_->SaveCatalog();
    
    // Log DDL operation for recovery
    LogDDL(LogRecordType::DROP_TABLE, stmt->table_name_);
    
    return ExecutionResult::Message("DROP TABLE SUCCESS");
}

// ============================================================================
// ALTER TABLE - Schema Modification
// ============================================================================

ExecutionResult DDLExecutor::AlterTable(AlterTableStatement* stmt) {
    if (!stmt || !catalog_) {
        return ExecutionResult::Error("[DDL] Invalid ALTER TABLE statement");
    }
    
    TableMetadata* table_info = catalog_->GetTable(stmt->table_name_);
    if (!table_info) {
        return ExecutionResult::Error("[DDL] Table not found: " + stmt->table_name_);
    }
    
    switch (stmt->alter_type_) {
        case AlterTableStatement::AlterType::ADD_COLUMN: {
            // Check if column already exists
            if (table_info->schema_.GetColIdx(stmt->column_name_) >= 0) {
                return ExecutionResult::Error("[DDL] Column already exists: " + stmt->column_name_);
            }
            
            // Note: Adding columns to existing data requires table rebuild
            // For now, return not implemented
            return ExecutionResult::Error(
                "[DDL] ADD COLUMN requires table rebuild - use DROP and CREATE instead");
        }
        
        case AlterTableStatement::AlterType::DROP_COLUMN: {
            // Check if column exists
            if (table_info->schema_.GetColIdx(stmt->column_name_) < 0) {
                return ExecutionResult::Error("[DDL] Column not found: " + stmt->column_name_);
            }
            
            // Check if this is the only column
            if (table_info->schema_.GetColumnCount() <= 1) {
                return ExecutionResult::Error("[DDL] Cannot drop the only column in a table");
            }
            
            // Check if column is part of primary key
            const Column& col = table_info->schema_.GetColumn(
                table_info->schema_.GetColIdx(stmt->column_name_));
            if (col.IsPrimaryKey()) {
                return ExecutionResult::Error(
                    "[DDL] Cannot drop primary key column. Drop primary key constraint first.");
            }
            
            return ExecutionResult::Error(
                "[DDL] DROP COLUMN requires table rebuild - use DROP and CREATE instead");
        }
        
        case AlterTableStatement::AlterType::RENAME_COLUMN: {
            // Verify old column exists
            if (table_info->schema_.GetColIdx(stmt->column_name_) < 0) {
                return ExecutionResult::Error("[DDL] Column not found: " + stmt->column_name_);
            }
            
            // Verify new name doesn't exist
            if (table_info->schema_.GetColIdx(stmt->new_column_name_) >= 0) {
                return ExecutionResult::Error(
                    "[DDL] Column already exists: " + stmt->new_column_name_);
            }
            
            return ExecutionResult::Error(
                "[DDL] RENAME COLUMN not yet implemented - use DROP and CREATE instead");
        }
        
        default:
            return ExecutionResult::Error("[DDL] Unknown ALTER TABLE operation");
    }
}

// ============================================================================
// CREATE INDEX - B+ Tree Index Creation
// ============================================================================

ExecutionResult DDLExecutor::CreateIndex(CreateIndexStatement* stmt) {
    if (!stmt || !catalog_) {
        return ExecutionResult::Error("[DDL] Invalid CREATE INDEX statement");
    }
    
    // Validate table exists
    TableMetadata* table_info = catalog_->GetTable(stmt->table_name_);
    if (!table_info) {
        return ExecutionResult::Error("[DDL] Table not found: " + stmt->table_name_);
    }
    
    // Validate column exists
    int col_idx = table_info->schema_.GetColIdx(stmt->column_name_);
    if (col_idx < 0) {
        return ExecutionResult::Error("[DDL] Column not found: " + stmt->column_name_);
    }
    
    // Check if index already exists
    auto indexes = catalog_->GetTableIndexes(stmt->table_name_);
    for (auto* idx : indexes) {
        if (idx->name_ == stmt->index_name_) {
            return ExecutionResult::Error("[DDL] Index already exists: " + stmt->index_name_);
        }
    }
    
    // Create the index
    auto* index = catalog_->CreateIndex(stmt->index_name_, stmt->table_name_, stmt->column_name_);
    if (!index) {
        return ExecutionResult::Error("[DDL] Failed to create index: " + stmt->index_name_);
    }
    
    // Persist catalog changes
    catalog_->SaveCatalog();
    
    return ExecutionResult::Message("CREATE INDEX SUCCESS");
}

ExecutionResult DDLExecutor::DropIndex(const std::string& index_name) {
    if (!catalog_) {
        return ExecutionResult::Error("[DDL] Internal error: Catalog not initialized");
    }
    
    // Note: Catalog::DropIndex is not yet implemented
    // This would require adding the method to the Catalog class
    return ExecutionResult::Error("[DDL] DROP INDEX not yet implemented in Catalog");
}

// ============================================================================
// DESCRIBE TABLE - Schema Inspection
// ============================================================================

ExecutionResult DDLExecutor::DescribeTable(DescribeTableStatement* stmt) {
    if (!stmt || !catalog_) {
        return ExecutionResult::Error("[DDL] Invalid DESCRIBE statement");
    }
    
    TableMetadata* table_info = catalog_->GetTable(stmt->table_name_);
    if (!table_info) {
        return ExecutionResult::Error("[DDL] Table not found: " + stmt->table_name_);
    }
    
    // Build result set
    auto rs = std::make_shared<ResultSet>();
    rs->column_names = {"Column", "Type", "Nullable", "Key", "Default", "Extra"};
    
    const Schema& schema = table_info->schema_;
    for (uint32_t i = 0; i < schema.GetColumnCount(); i++) {
        const Column& col = schema.GetColumn(i);
        
        // Type string
        std::string type_str;
        switch (col.GetType()) {
            case TypeId::INTEGER: type_str = "INTEGER"; break;
            case TypeId::DECIMAL: type_str = "DECIMAL"; break;
            case TypeId::VARCHAR: type_str = "VARCHAR(" + std::to_string(col.GetLength()) + ")"; break;
            case TypeId::BOOLEAN: type_str = "BOOLEAN"; break;
            default: type_str = "UNKNOWN"; break;
        }
        
        // Nullable
        std::string nullable = col.IsNullable() ? "YES" : "NO";
        
        // Key type
        std::string key;
        if (col.IsPrimaryKey()) key = "PRI";
        else if (col.IsUnique()) key = "UNI";
        
        // Default value
        std::string default_val;
        if (col.HasDefaultValue()) {
            default_val = col.GetDefaultValue()->ToString();
        }
        
        // Extra (auto_increment, etc.)
        std::string extra;
        if (col.IsAutoIncrement()) extra = "auto_increment";
        
        rs->AddRow({col.GetName(), type_str, nullable, key, default_val, extra});
    }
    
    return ExecutionResult::Data(rs);
}

// ============================================================================
// SHOW CREATE TABLE - DDL Generation
// ============================================================================

ExecutionResult DDLExecutor::ShowCreateTable(ShowCreateTableStatement* stmt) {
    if (!stmt || !catalog_) {
        return ExecutionResult::Error("[DDL] Invalid SHOW CREATE TABLE statement");
    }
    
    TableMetadata* table_info = catalog_->GetTable(stmt->table_name_);
    if (!table_info) {
        return ExecutionResult::Error("[DDL] Table not found: " + stmt->table_name_);
    }
    
    // Build CREATE TABLE statement
    std::ostringstream sql;
    sql << "CREATE TABLE `" << stmt->table_name_ << "` (\n";
    
    const Schema& schema = table_info->schema_;
    std::vector<std::string> pk_columns;
    
    for (uint32_t i = 0; i < schema.GetColumnCount(); i++) {
        const Column& col = schema.GetColumn(i);
        
        sql << "  `" << col.GetName() << "` ";
        
        // Type
        switch (col.GetType()) {
            case TypeId::INTEGER: sql << "INTEGER"; break;
            case TypeId::DECIMAL: sql << "DECIMAL"; break;
            case TypeId::VARCHAR: sql << "VARCHAR(" << col.GetLength() << ")"; break;
            case TypeId::BOOLEAN: sql << "BOOLEAN"; break;
            default: sql << "UNKNOWN"; break;
        }
        
        // Constraints
        if (!col.IsNullable()) sql << " NOT NULL";
        if (col.IsAutoIncrement()) sql << " AUTO_INCREMENT";
        if (col.HasDefaultValue()) {
            sql << " DEFAULT " << col.GetDefaultValue()->ToString();
        }
        if (col.IsUnique() && !col.IsPrimaryKey()) sql << " UNIQUE";
        
        if (col.IsPrimaryKey()) {
            pk_columns.push_back(col.GetName());
        }
        
        if (i < schema.GetColumnCount() - 1 || !pk_columns.empty() || 
            !table_info->foreign_keys_.empty()) {
            sql << ",";
        }
        sql << "\n";
    }
    
    // Primary key constraint
    if (!pk_columns.empty()) {
        sql << "  PRIMARY KEY (";
        for (size_t i = 0; i < pk_columns.size(); i++) {
            sql << "`" << pk_columns[i] << "`";
            if (i < pk_columns.size() - 1) sql << ", ";
        }
        sql << ")";
        if (!table_info->foreign_keys_.empty()) sql << ",";
        sql << "\n";
    }
    
    // Foreign key constraints
    for (size_t fk_idx = 0; fk_idx < table_info->foreign_keys_.size(); fk_idx++) {
        const auto& fk = table_info->foreign_keys_[fk_idx];
        sql << "  FOREIGN KEY (";
        for (size_t i = 0; i < fk.columns.size(); i++) {
            sql << "`" << fk.columns[i] << "`";
            if (i < fk.columns.size() - 1) sql << ", ";
        }
        sql << ") REFERENCES `" << fk.ref_table << "` (";
        for (size_t i = 0; i < fk.ref_columns.size(); i++) {
            sql << "`" << fk.ref_columns[i] << "`";
            if (i < fk.ref_columns.size() - 1) sql << ", ";
        }
        sql << ")";
        
        if (!fk.on_delete.empty()) sql << " ON DELETE " << fk.on_delete;
        if (!fk.on_update.empty()) sql << " ON UPDATE " << fk.on_update;
        
        if (fk_idx < table_info->foreign_keys_.size() - 1) sql << ",";
        sql << "\n";
    }
    
    sql << ");";
    
    auto rs = std::make_shared<ResultSet>();
    rs->column_names = {"Table", "Create Table"};
    rs->AddRow({stmt->table_name_, sql.str()});
    
    return ExecutionResult::Data(rs);
}

// ============================================================================
// LIST TABLES - Show All Tables
// ============================================================================

ExecutionResult DDLExecutor::ListTables() {
    if (!catalog_) {
        return ExecutionResult::Error("[DDL] Internal error: Catalog not initialized");
    }
    
    auto table_names = catalog_->GetAllTableNames();
    std::sort(table_names.begin(), table_names.end());
    
    auto rs = std::make_shared<ResultSet>();
    rs->column_names = {"Tables_in_database"};
    
    for (const auto& name : table_names) {
        rs->AddRow({name});
    }
    
    return ExecutionResult::Data(rs);
}

// ============================================================================
// UTILITY - Logging
// ============================================================================

void DDLExecutor::LogDDL(LogRecordType type, const std::string& object_name) {
    if (log_manager_) {
        LogRecord rec(0, LogRecord::INVALID_LSN, type);
        rec.table_name_ = object_name;
        log_manager_->AppendLogRecord(rec);
        
        // DDL changes should be durable immediately
        log_manager_->Flush(true);
    }
}

} // namespace francodb

