#include "execution/execution_engine.h"
#include "execution/executors/insert_executor.h"
#include "execution/executors/seq_scan_executor.h"
#include "execution/executors/delete_executor.h"
#include "execution/executors/index_scan_executor.h"
#include "execution/executors/update_executor.h"
#include "common/exception.h"
#include <algorithm>

namespace francodb {

ExecutionEngine::ExecutionEngine(BufferPoolManager *bpm, Catalog *catalog)
    : catalog_(catalog), exec_ctx_(new ExecutorContext(catalog, bpm)), 
      current_transaction_(nullptr), next_txn_id_(1), in_explicit_transaction_(false) {
}

ExecutionEngine::~ExecutionEngine() { 
    if (current_transaction_) {
        delete current_transaction_;
    }
    delete exec_ctx_; 
}

Transaction* ExecutionEngine::GetCurrentTransaction() {
    return current_transaction_;
}

Transaction* ExecutionEngine::GetCurrentTransactionForWrite() {
    if (current_transaction_ == nullptr) {
        current_transaction_ = new Transaction(next_txn_id_++);
    }
    return current_transaction_;
}

void ExecutionEngine::AutoCommitIfNeeded() {
    if (!in_explicit_transaction_ && current_transaction_ != nullptr && 
        current_transaction_->GetState() == Transaction::TransactionState::RUNNING) {
        ExecuteCommit();
    }
}

void ExecutionEngine::Execute(Statement *stmt) {
    if (stmt == nullptr) {
        return;
    }

    switch (stmt->GetType()) {
        case StatementType::CREATE_INDEX: {
            auto *idx_stmt = dynamic_cast<CreateIndexStatement *>(stmt);
            ExecuteCreateIndex(idx_stmt);
            break;
        }

        case StatementType::CREATE: {
            auto *create_stmt = dynamic_cast<CreateStatement *>(stmt);
            ExecuteCreate(create_stmt);
            break;
        }
        case StatementType::INSERT: {
            auto *insert_stmt = dynamic_cast<InsertStatement *>(stmt);
            ExecuteInsert(insert_stmt);
            break;
        }
        case StatementType::SELECT: {
            auto *select_stmt = dynamic_cast<SelectStatement *>(stmt);
            ExecuteSelect(select_stmt);
            break;
        }
        case StatementType::DROP: {
            auto *drop_stmt = dynamic_cast<DropStatement *>(stmt);
            ExecuteDrop(drop_stmt);
            break;
        }
        case StatementType::DELETE_CMD: {
            auto *del_stmt = dynamic_cast<DeleteStatement *>(stmt);
            ExecuteDelete(del_stmt);
            break;
        }
        case StatementType::UPDATE_CMD: {
            auto *upd_stmt = dynamic_cast<UpdateStatement *>(stmt);
            ExecuteUpdate(upd_stmt);
            break;
        }
        case StatementType::BEGIN: {
            ExecuteBegin();
            break;
        }
        case StatementType::ROLLBACK: {
            ExecuteRollback();
            break;
        }
        case StatementType::COMMIT: {
            ExecuteCommit();
            break;
        }
        default: {
            throw Exception(ExceptionType::EXECUTION, "Unknown Statement Type.");
        }
    }
    
    // Auto-commit after WRITE statements only (INSERT, UPDATE, DELETE)
    if (stmt->GetType() == StatementType::INSERT || 
        stmt->GetType() == StatementType::UPDATE_CMD || 
        stmt->GetType() == StatementType::DELETE_CMD) {
        AutoCommitIfNeeded();
    }
}

std::string ExecutionEngine::ValueToString(const Value &v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

void ExecutionEngine::PrintPostgresTable(const Schema *schema, const std::vector<std::vector<std::string>> &rows) {
    if (schema->GetColumnCount() == 0) {
        return;
    }

    // Calculate column widths
    std::vector<size_t> col_widths;
    for (uint32_t i = 0; i < schema->GetColumnCount(); ++i) {
        size_t max_width = schema->GetColumns()[i].GetName().length();
        for (const auto &row : rows) {
            max_width = std::max(max_width, row[i].length());
        }
        col_widths.push_back(max_width);
    }

    // Print column headers
    std::cout << " ";
    for (uint32_t i = 0; i < schema->GetColumnCount(); ++i) {
        std::cout << std::left << std::setw(col_widths[i]) << schema->GetColumns()[i].GetName();
        if (i < schema->GetColumnCount() - 1) {
            std::cout << " | ";
        }
    }
    std::cout << std::endl;

    // Print separator line
    std::cout << "-";
    for (uint32_t i = 0; i < schema->GetColumnCount(); ++i) {
        std::cout << std::string(col_widths[i], '-');
        if (i < schema->GetColumnCount() - 1) {
            std::cout << "-+-";
        }
    }
    std::cout << "-" << std::endl;

    // Print rows
    for (const auto &row : rows) {
        std::cout << " ";
        for (uint32_t i = 0; i < row.size(); ++i) {
            std::cout << std::left << std::setw(col_widths[i]) << row[i];
            if (i < row.size() - 1) {
                std::cout << " | ";
            }
        }
        std::cout << std::endl;
    }

    // Print footer
    std::cout << "(" << rows.size() << " row" << (rows.size() != 1 ? "s" : "") << ")" << std::endl;
}

void ExecutionEngine::ExecuteCreate(CreateStatement *stmt) {
    Schema schema(stmt->columns_);
    bool success = catalog_->CreateTable(stmt->table_name_, schema);
    if (!success) {
        throw Exception(ExceptionType::EXECUTION, "Table already exists: " + stmt->table_name_);
    }
    std::cout << "[EXEC] Created Table: " << stmt->table_name_ << std::endl;
}

void ExecutionEngine::ExecuteCreateIndex(CreateIndexStatement *stmt) {
    auto *index = catalog_->CreateIndex(stmt->index_name_, stmt->table_name_, stmt->column_name_);
    if (index == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "Failed to create index (Table exists? Column exists?)");
    }
    std::cout << "[EXEC] Created Index: " << stmt->index_name_ << " on " << stmt->table_name_ << std::endl;
}

void ExecutionEngine::ExecuteInsert(InsertStatement *stmt) {
    InsertExecutor executor(exec_ctx_, stmt, GetCurrentTransactionForWrite());
    executor.Init();
    Tuple t;
    executor.Next(&t);
    std::cout << "[EXEC] Insert successful." << std::endl;
}

void ExecutionEngine::ExecuteSelect(SelectStatement *stmt) {
    AbstractExecutor *executor = nullptr;

    // --- OPTIMIZER LOGIC START ---
    bool use_index = false;
    std::string index_col_name;
    Value index_search_value;

    if (!stmt->where_clause_.empty()) {
        auto &cond = stmt->where_clause_[0];
        if (cond.op == "=") {
            auto indexes = catalog_->GetTableIndexes(stmt->table_name_);
            for (auto *idx: indexes) {
                if (idx->col_name_ == cond.column && idx->b_plus_tree_ != nullptr) {
                    // Try to use index, but fall back to seq scan if it fails
                    try {
                        use_index = true;
                        index_col_name = idx->name_;
                        index_search_value = cond.value;
                        executor = new IndexScanExecutor(exec_ctx_, stmt, idx, index_search_value, GetCurrentTransaction());
                        std::cout << "[OPTIMIZER] Using Index: " << idx->name_ << std::endl;
                        break;
                    } catch (...) {
                        // If index scan creation fails, fall back to sequential scan
                        use_index = false;
                        executor = nullptr;
                        break;
                    }
                }
            }
        }
    }
    // --- OPTIMIZER LOGIC END ---

    if (!use_index) {
        executor = new SeqScanExecutor(exec_ctx_, stmt, GetCurrentTransaction());
        std::cout << "[OPTIMIZER] Using Sequential Scan" << std::endl;
    }

    // --- EXECUTION WITH BEAUTIFUL FORMATTING ---
    try {
        executor->Init();
    } catch (...) {
        // If index scan Init() fails, fall back to sequential scan
        if (use_index) {
            delete executor;
            executor = new SeqScanExecutor(exec_ctx_, stmt);
            std::cout << "[OPTIMIZER] Index scan failed, falling back to Sequential Scan" << std::endl;
            executor->Init();
        } else {
            throw; // Re-throw if it's not an index scan issue
        }
    }
    Tuple t;
    const Schema *output_schema = executor->GetOutputSchema();
    
    // Collect all rows first
    std::vector<std::vector<std::string>> rows;
    while (executor->Next(&t)) {
        std::vector<std::string> row;
        for (uint32_t i = 0; i < output_schema->GetColumnCount(); ++i) {
            Value v = t.GetValue(*output_schema, i);
            row.push_back(ValueToString(v));
        }
        rows.push_back(row);
    }

    // Print the beautiful table
    std::cout << std::endl;
    PrintPostgresTable(output_schema, rows);
    std::cout << std::endl;

    delete executor;
}

void ExecutionEngine::ExecuteDrop(DropStatement *stmt) {
    bool success = catalog_->DropTable(stmt->table_name_);
    if (!success) {
        throw Exception(ExceptionType::EXECUTION, "Table not found: " + stmt->table_name_);
    }
    std::cout << "[EXEC] Dropped Table: " << stmt->table_name_ << std::endl;
}

void ExecutionEngine::ExecuteDelete(DeleteStatement *stmt) {
    DeleteExecutor executor(exec_ctx_, stmt, GetCurrentTransactionForWrite());
    executor.Init();
    Tuple t;
    executor.Next(&t);
}

void ExecutionEngine::ExecuteUpdate(UpdateStatement *stmt) {
    UpdateExecutor executor(exec_ctx_, stmt, GetCurrentTransactionForWrite());
    executor.Init();
    Tuple t;
    executor.Next(&t);
}

void ExecutionEngine::ExecuteBegin() {
    if (current_transaction_ != nullptr && in_explicit_transaction_) {
        throw Exception(ExceptionType::EXECUTION, "Transaction already in progress. Commit or rollback first.");
    }
    // Commit any auto-commit transaction first
    if (current_transaction_ != nullptr) {
        ExecuteCommit();
    }
    current_transaction_ = new Transaction(next_txn_id_++);
    in_explicit_transaction_ = true;
    std::cout << "[EXEC] Transaction started (ID: " << current_transaction_->GetTransactionId() << ")" << std::endl;
}

void ExecutionEngine::ExecuteRollback() {
    if (current_transaction_ == nullptr || !in_explicit_transaction_) {
        throw Exception(ExceptionType::EXECUTION, "No active transaction to rollback.");
    }
    
    // Rollback all modifications in reverse order
    const auto &modifications = current_transaction_->GetModifications();
    
    // Process modifications in reverse to handle UPDATEs correctly
    std::vector<std::pair<RID, Transaction::TupleModification>> mods_vec;
    for (const auto &[rid, mod] : modifications) {
        mods_vec.push_back({rid, mod});
    }
    // Reverse to process newest first
    std::reverse(mods_vec.begin(), mods_vec.end());
    
    for (const auto &[rid, mod] : mods_vec) {
        if (mod.table_name.empty()) continue; // Skip if no table name
        
        TableMetadata *table_info = catalog_->GetTable(mod.table_name);
        if (table_info == nullptr) continue;
        
        if (mod.is_deleted) {
            // DELETE operation: Restore the tuple by unmarking delete
            table_info->table_heap_->UnmarkDelete(rid, nullptr);
            
            // Restore index entries
            auto indexes = catalog_->GetTableIndexes(mod.table_name);
            for (auto *index : indexes) {
                int col_idx = table_info->schema_.GetColIdx(index->col_name_);
                if (col_idx >= 0) {
                    Value key_val = mod.old_tuple.GetValue(table_info->schema_, col_idx);
                    GenericKey<8> key;
                    key.SetFromValue(key_val);
                    index->b_plus_tree_->Insert(key, rid, nullptr);
                }
            }
        } else if (mod.old_tuple.GetLength() == 0) {
            // INSERT operation: Delete the tuple
            Tuple current_tuple;
            if (table_info->table_heap_->GetTuple(rid, &current_tuple, nullptr)) {
                // Remove from indexes
                auto indexes = catalog_->GetTableIndexes(mod.table_name);
                for (auto *index : indexes) {
                    int col_idx = table_info->schema_.GetColIdx(index->col_name_);
                    if (col_idx >= 0) {
                        Value key_val = current_tuple.GetValue(table_info->schema_, col_idx);
                        GenericKey<8> key;
                        key.SetFromValue(key_val);
                        index->b_plus_tree_->Remove(key, nullptr);
                    }
                }
            }
            // Delete from table
            table_info->table_heap_->MarkDelete(rid, nullptr);
        } else {
            // UPDATE operation: Restore old tuple at old_rid
            table_info->table_heap_->UnmarkDelete(rid, nullptr);
            
            // Restore index entries with old tuple values
            auto indexes = catalog_->GetTableIndexes(mod.table_name);
            for (auto *index : indexes) {
                int col_idx = table_info->schema_.GetColIdx(index->col_name_);
                if (col_idx >= 0) {
                    Value old_key_val = mod.old_tuple.GetValue(table_info->schema_, col_idx);
                    GenericKey<8> old_key;
                    old_key.SetFromValue(old_key_val);
                    
                    // Try to remove any existing entry (might fail, that's OK)
                    index->b_plus_tree_->Remove(old_key, nullptr);
                    
                    // Add back with old tuple value and old RID
                    index->b_plus_tree_->Insert(old_key, rid, nullptr);
                }
            }
        }
    }
    
    current_transaction_->SetState(Transaction::TransactionState::ABORTED);
    delete current_transaction_;
    current_transaction_ = nullptr;
    in_explicit_transaction_ = false;
    std::cout << "[EXEC] Transaction rolled back." << std::endl;
}

void ExecutionEngine::ExecuteCommit() {
    if (current_transaction_ == nullptr) {
        // Allow commit even if no transaction (no-op)
        return;
    }
    
    current_transaction_->SetState(Transaction::TransactionState::COMMITTED);
    current_transaction_->Clear();
    delete current_transaction_;
    current_transaction_ = nullptr;
    in_explicit_transaction_ = false;
    std::cout << "[EXEC] Transaction committed." << std::endl;
}

} // namespace francodb

