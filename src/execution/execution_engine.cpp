#include "execution/execution_engine.h"
#include "execution/executors/insert_executor.h"
#include "execution/executors/seq_scan_executor.h"
#include "execution/executors/delete_executor.h"
#include "execution/executors/index_scan_executor.h"
#include "execution/executors/update_executor.h"
#include "common/exception.h"
#include <algorithm>
#include <sstream>
#include <filesystem>

#include "common/auth_manager.h"
#include "common/session_context.h"

namespace francodb {
    struct SessionContext;
    
    SessionContext* g_session_context = nullptr;

    ExecutionEngine::ExecutionEngine(BufferPoolManager *bpm, Catalog *catalog)
    : catalog_(catalog), exec_ctx_(new ExecutorContext(catalog, bpm)), 
      current_transaction_(nullptr), next_txn_id_(1), in_explicit_transaction_(false) {
}

ExecutionEngine::~ExecutionEngine() { 
    if (current_transaction_) delete current_transaction_;
    delete exec_ctx_; 
}

Transaction* ExecutionEngine::GetCurrentTransaction() { return current_transaction_; }

Transaction* ExecutionEngine::GetCurrentTransactionForWrite() {
    if (current_transaction_ == nullptr) {
        current_transaction_ = new Transaction(next_txn_id_++);
    }
    return current_transaction_;
}

void ExecutionEngine::AutoCommitIfNeeded() {
    if (!in_explicit_transaction_ && current_transaction_ != nullptr && 
        current_transaction_->GetState() == Transaction::TransactionState::RUNNING) {
        ExecuteCommit(); // We can ignore result of auto-commit
    }
}

// --- MAIN EXECUTE DISPATCHER ---
ExecutionResult ExecutionEngine::Execute(Statement *stmt) {
    if (stmt == nullptr) return ExecutionResult::Error("Empty Statement");

    try {
        ExecutionResult res;
        switch (stmt->GetType()) {
            case StatementType::CREATE_INDEX: 
                res = ExecuteCreateIndex(dynamic_cast<CreateIndexStatement *>(stmt)); break;
            case StatementType::CREATE:       
                res = ExecuteCreate(dynamic_cast<CreateStatement *>(stmt)); break;
            case StatementType::INSERT:       
                res = ExecuteInsert(dynamic_cast<InsertStatement *>(stmt)); break;
            case StatementType::SELECT:       
                res = ExecuteSelect(dynamic_cast<SelectStatement *>(stmt)); break;
            case StatementType::DROP:         
                res = ExecuteDrop(dynamic_cast<DropStatement *>(stmt)); break;
            case StatementType::DELETE_CMD:   
                res = ExecuteDelete(dynamic_cast<DeleteStatement *>(stmt)); break;
            case StatementType::UPDATE_CMD:   
                res = ExecuteUpdate(dynamic_cast<UpdateStatement *>(stmt)); break;
            case StatementType::SHOW_USERS:   
                res = ExecuteShowUsers(dynamic_cast<ShowUsersStatement *>(stmt)); break;
            case StatementType::SHOW_DATABASES:
                res = ExecuteShowDatabases(dynamic_cast<ShowDatabasesStatement *>(stmt)); break;
            case StatementType::SHOW_TABLES:  
                res = ExecuteShowTables(dynamic_cast<ShowTablesStatement *>(stmt)); break;
            case StatementType::SHOW_STATUS:  
                res = ExecuteShowStatus(dynamic_cast<ShowStatusStatement *>(stmt)); break;
            case StatementType::WHOAMI:       
                res = ExecuteWhoAmI(dynamic_cast<WhoAmIStatement *>(stmt)); break;
            case StatementType::BEGIN:        
                res = ExecuteBegin(); break;
            case StatementType::ROLLBACK:     
                res = ExecuteRollback(); break;
            case StatementType::COMMIT:       
                res = ExecuteCommit(); break;
            default: return ExecutionResult::Error("Unknown Statement Type.");
        }
        
        // Auto-commit logic
        if (stmt->GetType() == StatementType::INSERT || 
            stmt->GetType() == StatementType::UPDATE_CMD || 
            stmt->GetType() == StatementType::DELETE_CMD) {
            AutoCommitIfNeeded();
        }
        return res;

    } catch (const std::exception &e) {
        return ExecutionResult::Error(e.what());
    }
}

std::string ExecutionEngine::ValueToString(const Value &v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

// --- EXECUTORS ---

ExecutionResult ExecutionEngine::ExecuteCreate(CreateStatement *stmt) {
    Schema schema(stmt->columns_);
    bool success = catalog_->CreateTable(stmt->table_name_, schema);
    if (!success) return ExecutionResult::Error("Table already exists: " + stmt->table_name_);
    return ExecutionResult::Message("CREATE TABLE SUCCESS");
}

ExecutionResult ExecutionEngine::ExecuteCreateIndex(CreateIndexStatement *stmt) {
    auto *index = catalog_->CreateIndex(stmt->index_name_, stmt->table_name_, stmt->column_name_);
    if (index == nullptr) return ExecutionResult::Error("Failed to create index");
    return ExecutionResult::Message("CREATE INDEX SUCCESS");
}

ExecutionResult ExecutionEngine::ExecuteInsert(InsertStatement *stmt) {
    InsertExecutor executor(exec_ctx_, stmt, GetCurrentTransactionForWrite());
    executor.Init();
    Tuple t;
    int count = 0;
    while(executor.Next(&t)) count++; // Usually 1 for single insert
    return ExecutionResult::Message("INSERT 1"); // Assuming simple insert
}

ExecutionResult ExecutionEngine::ExecuteSelect(SelectStatement *stmt) {
    AbstractExecutor *executor = nullptr;
    bool use_index = false;

    // Optimizer Logic (Simplified)
    if (!stmt->where_clause_.empty() && stmt->where_clause_[0].op == "=") {
        auto &cond = stmt->where_clause_[0];
        auto indexes = catalog_->GetTableIndexes(stmt->table_name_);
        for (auto *idx: indexes) {
            if (idx->col_name_ == cond.column && idx->b_plus_tree_) {
                try {
                    executor = new IndexScanExecutor(exec_ctx_, stmt, idx, cond.value, GetCurrentTransaction());
                    use_index = true;
                    break;
                } catch(...) {}
            }
        }
    }

    if (!use_index) {
        executor = new SeqScanExecutor(exec_ctx_, stmt, GetCurrentTransaction());
    }

    try {
        executor->Init();
    } catch (...) {
        if (use_index) { // Fallback
            delete executor;
            executor = new SeqScanExecutor(exec_ctx_, stmt, GetCurrentTransaction());
            executor->Init();
        } else {
            throw;
        }
    }

    // --- POPULATE RESULT SET ---
    auto rs = std::make_shared<ResultSet>();
    const Schema *output_schema = executor->GetOutputSchema();
    
    // 1. Column Headers
    for(const auto &col : output_schema->GetColumns()) {
        rs->column_names.push_back(col.GetName());
    }

    // 2. Rows
    Tuple t;
    while (executor->Next(&t)) {
        std::vector<std::string> row_strings;
        for (uint32_t i = 0; i < output_schema->GetColumnCount(); ++i) {
            row_strings.push_back(ValueToString(t.GetValue(*output_schema, i)));
        }
        rs->AddRow(row_strings);
    }
    delete executor;
    
    return ExecutionResult::Data(rs);
}

ExecutionResult ExecutionEngine::ExecuteDrop(DropStatement *stmt) {
    if (!catalog_->DropTable(stmt->table_name_)) return ExecutionResult::Error("Table not found");
    return ExecutionResult::Message("DROP TABLE SUCCESS");
}

ExecutionResult ExecutionEngine::ExecuteDelete(DeleteStatement *stmt) {
    DeleteExecutor executor(exec_ctx_, stmt, GetCurrentTransactionForWrite());
    executor.Init();
    Tuple t;
    executor.Next(&t); // The executor prints log internally currently, but we can change that later
    return ExecutionResult::Message("DELETE SUCCESS");
}

ExecutionResult ExecutionEngine::ExecuteUpdate(UpdateStatement *stmt) {
    UpdateExecutor executor(exec_ctx_, stmt, GetCurrentTransactionForWrite());
    executor.Init();
    Tuple t;
    executor.Next(&t);
    return ExecutionResult::Message("UPDATE SUCCESS");
}

ExecutionResult ExecutionEngine::ExecuteBegin() {
    if (current_transaction_ && in_explicit_transaction_) return ExecutionResult::Error("Transaction in progress");
    if (current_transaction_) ExecuteCommit();
    current_transaction_ = new Transaction(next_txn_id_++);
    in_explicit_transaction_ = true;
    return ExecutionResult::Message("BEGIN TRANSACTION " + std::to_string(current_transaction_->GetTransactionId()));
}

ExecutionResult ExecutionEngine::ExecuteRollback() {
    if (!current_transaction_ || !in_explicit_transaction_) return ExecutionResult::Error("No transaction to rollback");

    // ... (Keep existing rollback logic here, copied from your previous file) ...
    // ... Copy the reverse iteration logic here ...
    const auto &modifications = current_transaction_->GetModifications();
    std::vector<std::pair<RID, Transaction::TupleModification>> mods_vec;
    for (const auto &[rid, mod] : modifications) mods_vec.push_back({rid, mod});
    std::reverse(mods_vec.begin(), mods_vec.end());

    for (const auto &[rid, mod] : mods_vec) {
        if (mod.table_name.empty()) continue;
        TableMetadata *table_info = catalog_->GetTable(mod.table_name);
        if (!table_info) continue;

        if (mod.is_deleted) { // Undo Delete
            table_info->table_heap_->UnmarkDelete(rid, nullptr);
             auto indexes = catalog_->GetTableIndexes(mod.table_name);
             for (auto *index : indexes) {
                int col_idx = table_info->schema_.GetColIdx(index->col_name_);
                if (col_idx >= 0) {
                    GenericKey<8> key; 
                    key.SetFromValue(mod.old_tuple.GetValue(table_info->schema_, col_idx));
                    index->b_plus_tree_->Insert(key, rid, nullptr);
                }
             }
        } else if (mod.old_tuple.GetLength() == 0) { // Undo Insert
            Tuple current;
            if (table_info->table_heap_->GetTuple(rid, &current, nullptr)) {
                auto indexes = catalog_->GetTableIndexes(mod.table_name);
                for (auto *index : indexes) {
                    int col_idx = table_info->schema_.GetColIdx(index->col_name_);
                    if (col_idx >= 0) {
                        GenericKey<8> key;
                        key.SetFromValue(current.GetValue(table_info->schema_, col_idx));
                        index->b_plus_tree_->Remove(key, nullptr);
                    }
                }
            }
            table_info->table_heap_->MarkDelete(rid, nullptr);
        } else { // Undo Update
             table_info->table_heap_->UnmarkDelete(rid, nullptr);
             // Note: In a real system we'd restore values. 
             // For now we just unmark delete assuming update was delete+insert
        }
    }

    current_transaction_->SetState(Transaction::TransactionState::ABORTED);
    delete current_transaction_;
    current_transaction_ = nullptr;
    in_explicit_transaction_ = false;
    return ExecutionResult::Message("ROLLBACK SUCCESS");
}

ExecutionResult ExecutionEngine::ExecuteCommit() {
    if (current_transaction_) {
        current_transaction_->SetState(Transaction::TransactionState::COMMITTED);
        delete current_transaction_;
        current_transaction_ = nullptr;
    }
    in_explicit_transaction_ = false;
    return ExecutionResult::Message("COMMIT SUCCESS");
}

// --- SHOW DATABASES ---
ExecutionResult ExecutionEngine::ExecuteShowDatabases(ShowDatabasesStatement *stmt) {
    // List all database directories in the data/ folder (excluding system)
    std::vector<std::string> dbs;
    namespace fs = std::filesystem;
    std::string data_dir = "data";
    for (const auto &entry : fs::directory_iterator(data_dir)) {
        if (entry.is_directory()) {
            std::string name = entry.path().filename().string();
            if (name != "system") dbs.push_back(name);
        } else if (entry.is_regular_file()) {
            std::string fname = entry.path().filename().string();
            if (fname.ends_with(".francodb")) {
                std::string dbname = fname.substr(0, fname.size() - 9);
                if (dbname != "system") dbs.push_back(dbname);
            }
        }
    }
    std::sort(dbs.begin(), dbs.end());
    auto rs = std::make_shared<ResultSet>();
    rs->column_names.push_back("Database");
    for (const auto &db : dbs) rs->AddRow({db});
    return ExecutionResult::Data(rs);
}

// --- SHOW TABLES ---
ExecutionResult ExecutionEngine::ExecuteShowTables(ShowTablesStatement *stmt) {
    std::vector<std::string> tables = catalog_->GetAllTableNames();
    std::sort(tables.begin(), tables.end());
    auto rs = std::make_shared<ResultSet>();
    rs->column_names.push_back("Table");
    for (const auto &tbl : tables) rs->AddRow({tbl});
    return ExecutionResult::Data(rs);
}

// --- SHOW USERS ---
ExecutionResult ExecutionEngine::ExecuteShowUsers(ShowUsersStatement *stmt) {
    // Get users from AuthManager (global, not per ExecutorContext)
    extern AuthManager *g_auth_manager; // You may need to declare this in a header or pass it in
    std::vector<UserInfo> users = g_auth_manager->GetAllUsers();
    auto rs = std::make_shared<ResultSet>();
    rs->column_names = {"Username", "Role"};
    for (const auto &user : users) {
        // Find the role for the current DB, or show all roles as a comma-separated string
        std::string roles;
        for (const auto &pair : user.db_roles) {
            if (!roles.empty()) roles += ", ";
            switch (pair.second) {
                case UserRole::SUPERADMIN: roles += "SUPERADMIN"; break;
                case UserRole::ADMIN: roles += "ADMIN"; break;
                case UserRole::USER: roles += "USER"; break;
                case UserRole::READONLY: roles += "READONLY"; break;
                case UserRole::DENIED: roles += "DENIED"; break;
            }
            roles += "@" + pair.first;
        }
        rs->AddRow({user.username, roles});
    }
    return ExecutionResult::Data(rs);
}

// --- SHOW STATUS ---
ExecutionResult ExecutionEngine::ExecuteShowStatus(ShowStatusStatement *stmt) {
    auto rs = std::make_shared<ResultSet>();
    rs->column_names = {"Status"};
    rs->AddRow({"OK"});
    return ExecutionResult::Data(rs);
}

// --- WHOAMI ---
    ExecutionResult ExecutionEngine::ExecuteWhoAmI(WhoAmIStatement *stmt) {
        // FIXED: Using the global raw pointer g_session_context defined at the top
        std::string user = (g_session_context) ? g_session_context->current_user : "unknown";
        
        auto rs = std::make_shared<ResultSet>();
        rs->column_names = {"User"};
        rs->AddRow({user});
        return ExecutionResult::Data(rs);
    }

} // namespace francodb