#pragma once

#include <string>
#include "execution/execution_result.h"
#include "catalog/catalog.h"
#include "recovery/log_manager.h"

namespace francodb {

// Forward declarations
class CreateStatement;
class CreateIndexStatement;
class DropStatement;
class AlterTableStatement;
class DescribeTableStatement;
class ShowCreateTableStatement;

/**
 * DDLExecutor - Data Definition Language operations
 * 
 * PROBLEM SOLVED:
 * - Extracts DDL logic from ExecutionEngine (SRP)
 * - Single class responsible for schema modifications
 * 
 * OPERATIONS:
 * - CREATE TABLE
 * - CREATE INDEX
 * - DROP TABLE
 * - ALTER TABLE
 * - DESCRIBE TABLE
 * - SHOW CREATE TABLE
 */
class DDLExecutor {
public:
    DDLExecutor(Catalog* catalog, LogManager* log_manager = nullptr)
        : catalog_(catalog), log_manager_(log_manager) {}
    
    // ========================================================================
    // TABLE OPERATIONS
    // ========================================================================
    
    /**
     * Create a new table.
     */
    ExecutionResult CreateTable(CreateStatement* stmt);
    
    /**
     * Drop a table.
     */
    ExecutionResult DropTable(DropStatement* stmt);
    
    /**
     * Alter table schema.
     */
    ExecutionResult AlterTable(AlterTableStatement* stmt);
    
    // ========================================================================
    // INDEX OPERATIONS
    // ========================================================================
    
    /**
     * Create an index on a table column.
     */
    ExecutionResult CreateIndex(CreateIndexStatement* stmt);
    
    /**
     * Drop an index.
     */
    ExecutionResult DropIndex(const std::string& index_name);
    
    // ========================================================================
    // SCHEMA INSPECTION
    // ========================================================================
    
    /**
     * Describe table structure.
     */
    ExecutionResult DescribeTable(DescribeTableStatement* stmt);
    
    /**
     * Show CREATE TABLE statement.
     */
    ExecutionResult ShowCreateTable(ShowCreateTableStatement* stmt);
    
    /**
     * List all tables in the current database.
     */
    ExecutionResult ListTables();
    
    // ========================================================================
    // UTILITY
    // ========================================================================
    
    void SetCatalog(Catalog* catalog) { catalog_ = catalog; }
    void SetLogManager(LogManager* log_manager) { log_manager_ = log_manager; }

private:
    Catalog* catalog_;
    LogManager* log_manager_;
    
    /**
     * Log a DDL operation.
     */
    void LogDDL(LogRecordType type, const std::string& object_name);
};

} // namespace francodb

