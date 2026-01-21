#include "execution/execution_engine.h"
#include "execution/executors/insert_executor.h"
#include "execution/executors/seq_scan_executor.h"
#include "execution/executors/delete_executor.h"
#include "execution/executors/index_scan_executor.h"
#include "execution/executors/update_executor.h"
#include "execution/executors/join_executor.h"
#include "recovery/checkpoint_manager.h"
#include "recovery/recovery_manager.h"
#include "common/exception.h"
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <set>
#include <map>

#include "recovery/snapshot_manager.h"

#include "common/auth_manager.h"
#include "../include/network/session_context.h"

namespace francodb {
    struct SessionContext;
    std::shared_mutex ExecutionEngine::global_lock_;


    SessionContext *g_session_context = nullptr;

    ExecutionEngine::ExecutionEngine(BufferPoolManager *bpm, Catalog *catalog,
                                     AuthManager *auth_manager, DatabaseRegistry *db_registry,
                                     LogManager *log_manager)
        : bpm_(bpm), 
          catalog_(catalog), 
          auth_manager_(auth_manager),
          db_registry_(db_registry), 
          log_manager_(log_manager), 
          exec_ctx_(nullptr),
          current_transaction_(nullptr), 
          next_txn_id_(1),
          in_explicit_transaction_(false) {
        exec_ctx_ = new ExecutorContext(bpm_, catalog_, nullptr, log_manager_);
    }

    ExecutionEngine::~ExecutionEngine() {
        if (current_transaction_) delete current_transaction_;
        delete exec_ctx_;
    }

    Transaction *ExecutionEngine::GetCurrentTransaction() { return current_transaction_; }

    Transaction *ExecutionEngine::GetCurrentTransactionForWrite() {
        if (current_transaction_ == nullptr) {
            // Atomic increment ensures unique transaction IDs under concurrency
            int txn_id = next_txn_id_.fetch_add(1, std::memory_order_relaxed);
            current_transaction_ = new Transaction(txn_id);
        }
        return current_transaction_;
    }

    void ExecutionEngine::AutoCommitIfNeeded() {
        if (!in_explicit_transaction_ && current_transaction_ != nullptr &&
            current_transaction_->GetState() == Transaction::TransactionState::RUNNING) {
            ExecuteCommit();
        }
    }


    ExecutionResult ExecutionEngine::Execute(Statement *stmt, SessionContext *session) {
        if (stmt == nullptr) return ExecutionResult::Error("Empty Statement");

        // =================================================================================
        // [ENTERPRISE] CONCURRENCY GATEKEEPER
        // =================================================================================

        // Pointer to the lock we might acquire
        std::unique_lock<std::shared_mutex> exclusive_lock;
        std::shared_lock<std::shared_mutex> shared_lock;

        // Check the statement type to decide locking strategy
        bool requires_exclusive = (stmt->GetType() == StatementType::RECOVER ||
                                   stmt->GetType() == StatementType::CHECKPOINT);

        if (requires_exclusive) {
            // STOP THE WORLD: Wait for all queries to finish, then block new ones.
            exclusive_lock = std::unique_lock<std::shared_mutex>(global_lock_);
        } else {
            // SHARED ACCESS: Allow concurrent execution, but fail if System is recovering.
            shared_lock = std::shared_lock<std::shared_mutex>(global_lock_);
        }

        // =================================================================================

        try {
            ExecutionResult res;
            switch (stmt->GetType()) {
                // --- DDL ---
                case StatementType::CREATE_INDEX: res = ExecuteCreateIndex(dynamic_cast<CreateIndexStatement *>(stmt));
                    break;
                case StatementType::CREATE: res = ExecuteCreate(dynamic_cast<CreateStatement *>(stmt));
                    break;
                case StatementType::DROP:
                    if (session && session->current_db == "system" && session->role != UserRole::SUPERADMIN) {
                        return ExecutionResult::Error("Cannot drop tables in system database");
                    }
                    res = ExecuteDrop(dynamic_cast<DropStatement *>(stmt));
                    break;

                // --- USER MANAGEMENT ---
                case StatementType::CREATE_USER:
                    res = ExecuteCreateUser(dynamic_cast<CreateUserStatement *>(stmt));
                    break;
                case StatementType::ALTER_USER_ROLE:
                    res = ExecuteAlterUserRole(dynamic_cast<AlterUserRoleStatement *>(stmt));
                    break;
                case StatementType::DELETE_USER:
                    res = ExecuteDeleteUser(dynamic_cast<DeleteUserStatement *>(stmt));
                    break;

                // --- DML ---
                case StatementType::INSERT:
                    if (session && session->current_db == "system" && session->role != UserRole::SUPERADMIN) {
                        return ExecutionResult::Error("Cannot modify system database tables");
                    }
                    res = ExecuteInsert(dynamic_cast<InsertStatement *>(stmt));
                    break;
                case StatementType::SELECT: res = ExecuteSelect(dynamic_cast<SelectStatement *>(stmt), session);
                    break;
                case StatementType::DELETE_CMD:
                    if (session && session->current_db == "system" && session->role != UserRole::SUPERADMIN) {
                        return ExecutionResult::Error("Cannot modify system database tables");
                    }
                    res = ExecuteDelete(dynamic_cast<DeleteStatement *>(stmt));
                    break;
                case StatementType::UPDATE_CMD:
                    if (session && session->current_db == "system" && session->role != UserRole::SUPERADMIN) {
                        return ExecutionResult::Error("Cannot modify system database tables");
                    }
                    res = ExecuteUpdate(dynamic_cast<UpdateStatement *>(stmt));
                    break;

                // --- SYSTEM & METADATA ---
                case StatementType::SHOW_DATABASES: res = ExecuteShowDatabases(
                                                        dynamic_cast<ShowDatabasesStatement *>(stmt), session);
                    break;
                case StatementType::SHOW_TABLES: res = ExecuteShowTables(dynamic_cast<ShowTablesStatement *>(stmt),
                                                                         session);
                    break;
                case StatementType::SHOW_STATUS: res = ExecuteShowStatus(dynamic_cast<ShowStatusStatement *>(stmt),
                                                                         session);
                    break;
                case StatementType::WHOAMI: res = ExecuteWhoAmI(dynamic_cast<WhoAmIStatement *>(stmt), session);
                    break;
                case StatementType::SHOW_USERS: res = ExecuteShowUsers(dynamic_cast<ShowUsersStatement *>(stmt));
                    break;
                case StatementType::DESCRIBE_TABLE:
                    res = ExecuteDescribeTable(dynamic_cast<DescribeTableStatement *>(stmt));
                    break;
                case StatementType::SHOW_CREATE_TABLE:
                    res = ExecuteShowCreateTable(dynamic_cast<ShowCreateTableStatement *>(stmt));
                    break;
                case StatementType::ALTER_TABLE:
                    res = ExecuteAlterTable(dynamic_cast<AlterTableStatement *>(stmt));
                    break;

                // --- TRANSACTIONS ---
                case StatementType::BEGIN: res = ExecuteBegin();
                    break;
                case StatementType::ROLLBACK: res = ExecuteRollback();
                    break;
                case StatementType::COMMIT: res = ExecuteCommit();
                    break;

                // --- DB MGMT ---
                case StatementType::CREATE_DB: {
                    auto *create_db = dynamic_cast<CreateDatabaseStatement *>(stmt);
                    return ExecuteCreateDatabase(create_db, session);
                }
                case StatementType::USE_DB: {
                    auto *use_db = dynamic_cast<UseDatabaseStatement *>(stmt);
                    return ExecuteUseDatabase(use_db, session);
                }
                case StatementType::DROP_DB: {
                    auto *drop_db = dynamic_cast<DropDatabaseStatement *>(stmt);
                    return ExecuteDropDatabase(drop_db, session);
                }

                case StatementType::CREATE_TABLE: {
                    if (session->current_db.empty()) {
                        return ExecutionResult::Error("No database selected. Use 'USE DATABASE_NAME' first.");
                    }
                    if ((session->current_db == "francodb" || session->current_db == "system") && session->role !=
                        UserRole::SUPERADMIN) {
                        return ExecutionResult::Error(
                            "Cannot create tables in reserved database: " + session->current_db);
                    }
                    auto *create = dynamic_cast<CreateStatement *>(stmt);
                    return ExecuteCreate(create);
                }

                // --- RECOVERY OPS (Protected by Exclusive Lock above) ---
                case StatementType::CHECKPOINT:
                    return ExecuteCheckpoint();

                case StatementType::RECOVER:
                    return ExecuteRecover(dynamic_cast<RecoverStatement *>(stmt));

                default: return ExecutionResult::Error("Unknown Statement Type in Engine.");
            }

            // Auto-commit for single statements
            if (stmt->GetType() == StatementType::INSERT ||
                stmt->GetType() == StatementType::UPDATE_CMD ||
                stmt->GetType() == StatementType::DELETE_CMD) {
                AutoCommitIfNeeded();
            }
            return res;
        } catch (const std::exception &e) {
            if (current_transaction_ && current_transaction_->GetState() == Transaction::TransactionState::RUNNING) {
                // Force Rollback to release row-level locks
                bool was_explicit = in_explicit_transaction_;
                in_explicit_transaction_ = true;
                ExecuteRollback();
                in_explicit_transaction_ = was_explicit;
            }
            return ExecutionResult::Error(e.what());
        }
    }

    std::string ExecutionEngine::ValueToString(const Value &v) {
        std::ostringstream oss;
        oss << v;
        return oss.str();
    }


    ExecutionResult ExecutionEngine::ExecuteCreate(CreateStatement *stmt) {
        Schema schema(stmt->columns_);

        // Validate foreign keys BEFORE creating the table
        for (const auto &fk: stmt->foreign_keys_) {
            // Check if referenced table exists
            TableMetadata *ref_table = catalog_->GetTable(fk.ref_table);
            if (!ref_table) {
                return ExecutionResult::Error("Referenced table does not exist: " + fk.ref_table);
            }

            // Check if all referenced columns exist in the referenced table
            for (const auto &ref_col: fk.ref_columns) {
                int col_idx = ref_table->schema_.GetColIdx(ref_col);
                if (col_idx < 0) {
                    return ExecutionResult::Error("Referenced column '" + ref_col +
                                                  "' does not exist in table '" + fk.ref_table + "'");
                }
            }

            // Check if all local columns exist in the table being created
            for (const auto &local_col: fk.columns) {
                bool found = false;
                for (const auto &col: stmt->columns_) {
                    if (col.GetName() == local_col) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return ExecutionResult::Error("Foreign key column '" + local_col +
                                                  "' does not exist in table definition");
                }
            }
        }

        TableMetadata *table_info = catalog_->CreateTable(stmt->table_name_, schema);
        if (!table_info) {
            return ExecutionResult::Error("Table already exists: " + stmt->table_name_);
        }

        // Store foreign keys in table metadata
        table_info->foreign_keys_ = stmt->foreign_keys_;
        
        catalog_->SaveCatalog();

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
        while (executor.Next(&t)) count++; // Usually 1 for single insert
        return ExecutionResult::Message("INSERT 1"); // Assuming simple insert
    }

    ExecutionResult ExecutionEngine::ExecuteSelect(SelectStatement *stmt, SessionContext *session) {
        AbstractExecutor *executor = nullptr;
        bool use_index = false;
        
        TableHeap* target_heap = nullptr;
        std::unique_ptr<TableHeap> shadow_heap = nullptr;

        // 1. DETERMINE DATA SOURCE (Live vs Time Travel)
        if (stmt->as_of_timestamp_ > 0) {
            // Get current database from session
            std::string db_name = (session) ? session->current_db : "system";
            std::cout << "[TIME TRAVEL] Building snapshot as of " << stmt->as_of_timestamp_ 
                      << " for database '" << db_name << "'..." << std::endl;
            
            if (log_manager_) {
                log_manager_->Flush(true);     
            }
            
            shadow_heap = SnapshotManager::BuildSnapshot(
                stmt->table_name_, 
                stmt->as_of_timestamp_, 
                bpm_, 
                log_manager_, 
                catalog_,
                db_name  // Pass the correct database name
            );
            target_heap = shadow_heap.get();
            use_index = false; 
            
        } else {
            auto table_info = catalog_->GetTable(stmt->table_name_);
            if (!table_info) return ExecutionResult::Error("Table not found: " + stmt->table_name_);
            target_heap = table_info->table_heap_.get();
        }

        // 2. OPTIMIZER
        if (stmt->as_of_timestamp_ == 0 && !stmt->where_clause_.empty() && stmt->where_clause_[0].op == "=") {
            auto &cond = stmt->where_clause_[0];
            auto indexes = catalog_->GetTableIndexes(stmt->table_name_);
            
            for (auto *idx: indexes) {
                if (idx->col_name_ == cond.column && idx->b_plus_tree_) {
                    try {
                        // [FIX] Use .ToString() before std::stoi
                        Value lookup_val(TypeId::INTEGER, std::stoi(cond.value.ToString()));
                        
                        executor = new IndexScanExecutor(exec_ctx_, stmt, idx, lookup_val, GetCurrentTransaction());
                        use_index = true;
                        break;
                    } catch (...) {}
                }
            }
        }

        if (!use_index) {
            // Pass target_heap to SeqScanExecutor
            executor = new SeqScanExecutor(exec_ctx_, stmt, GetCurrentTransaction(), target_heap);
        }

        try {
            executor->Init();
        } catch (...) {
            if (executor) delete executor;
            return ExecutionResult::Error("Execution Init Failed");
        }

        // 3. COLLECT RESULTS
        auto rs = std::make_shared<ResultSet>();
        const Schema *output_schema = executor->GetOutputSchema();

        std::vector<uint32_t> column_indices;
        if (stmt->select_all_) {
            for (uint32_t i = 0; i < output_schema->GetColumnCount(); i++) {
                rs->column_names.push_back(output_schema->GetColumn(i).GetName());
                column_indices.push_back(i);
            }
        } else {
            for (const auto &col_name: stmt->columns_) {
                int col_idx = output_schema->GetColIdx(col_name);
                if (col_idx < 0) {
                    delete executor;
                    return ExecutionResult::Error("Column not found: " + col_name);
                }
                rs->column_names.push_back(col_name);
                column_indices.push_back(static_cast<uint32_t>(col_idx));
            }
        }

        std::vector<std::vector<std::string>> all_rows;
        Tuple t;
        while (executor->Next(&t)) {
            std::vector<std::string> row_strings;
            for (uint32_t col_idx: column_indices) {
                row_strings.push_back(t.GetValue(*output_schema, col_idx).ToString());
            }
            all_rows.push_back(row_strings);
        }
        delete executor;

        // [SIMPLE] Just fill result set (Limit/Sort omitted for brevity but should be here)
        for (const auto& row : all_rows) {
            rs->AddRow(row);
        }

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

        // [ACID] Log BEGIN
        if (log_manager_) {
            LogRecord rec(current_transaction_->GetTransactionId(), current_transaction_->GetPrevLSN(),
                          LogRecordType::BEGIN);
            auto lsn = log_manager_->AppendLogRecord(rec);
            current_transaction_->SetPrevLSN(lsn);
        }

        return ExecutionResult::Message(
            "BEGIN TRANSACTION " + std::to_string(current_transaction_->GetTransactionId()));
    }

    ExecutionResult ExecutionEngine::ExecuteRollback() {
        if (!current_transaction_ || !in_explicit_transaction_)
            return ExecutionResult::Error("No transaction to rollback");

        // [ACID] Log ABORT (Before rolling back memory)
        if (log_manager_) {
            LogRecord rec(current_transaction_->GetTransactionId(), current_transaction_->GetPrevLSN(),
                          LogRecordType::ABORT);
            log_manager_->AppendLogRecord(rec);
            // No flush needed for abort usually
        }

        // ... (Existing in-memory Rollback Logic) ...
        const auto &modifications = current_transaction_->GetModifications();
        std::vector<std::pair<RID, Transaction::TupleModification> > mods_vec;
        for (const auto &[rid, mod]: modifications) mods_vec.push_back({rid, mod});
        std::reverse(mods_vec.begin(), mods_vec.end());

        for (const auto &[rid, mod]: mods_vec) {
            if (mod.table_name.empty()) continue;
            TableMetadata *table_info = catalog_->GetTable(mod.table_name);
            if (!table_info) continue;
            if (mod.is_deleted) table_info->table_heap_->UnmarkDelete(rid, nullptr);
            else if (mod.old_tuple.GetLength() == 0) table_info->table_heap_->MarkDelete(rid, nullptr);
            else table_info->table_heap_->UnmarkDelete(rid, nullptr);
        }

        current_transaction_->SetState(Transaction::TransactionState::ABORTED);
        delete current_transaction_;
        current_transaction_ = nullptr;
        in_explicit_transaction_ = false;
        return ExecutionResult::Message("ROLLBACK SUCCESS");
    }

    ExecutionResult ExecutionEngine::ExecuteCommit() {
        if (current_transaction_) {
            // [ACID] Log COMMIT and FLUSH
            if (log_manager_) {
                LogRecord rec(current_transaction_->GetTransactionId(), current_transaction_->GetPrevLSN(),
                              LogRecordType::COMMIT);
                log_manager_->AppendLogRecord(rec);
                // FORCE FLUSH ensures Durability
                log_manager_->Flush(true);
            }

            current_transaction_->SetState(Transaction::TransactionState::COMMITTED);
            delete current_transaction_;
            current_transaction_ = nullptr;
        }
        in_explicit_transaction_ = false;
        return ExecutionResult::Message("COMMIT SUCCESS");
    }

    ExecutionResult ExecutionEngine::ExecuteWhoAmI(WhoAmIStatement *stmt, SessionContext *session) {
        auto rs = std::make_shared<ResultSet>();
        rs->column_names = {"Current User", "Current DB", "Role"};

        std::string role_str = "USER";
        if (session->role == UserRole::SUPERADMIN) role_str = "SUPERADMIN";
        else if (session->role == UserRole::ADMIN) role_str = "ADMIN";
        else if (session->role == UserRole::READONLY) role_str = "READONLY";

        rs->AddRow({session->current_user, session->current_db, role_str});
        return ExecutionResult::Data(rs);
    }

    ExecutionResult ExecutionEngine::ExecuteShowDatabases(ShowDatabasesStatement *stmt, SessionContext *session) {
        auto rs = std::make_shared<ResultSet>();
        rs->column_names.push_back("Database");

        // Use a set to avoid duplicates
        std::set<std::string> db_names;

        // Always show francodb if user has access
        if (auth_manager_->HasDatabaseAccess(session->current_user, "francodb")) {
            db_names.insert("francodb");
        }

        // Scan filesystem directory for persisted databases
        namespace fs = std::filesystem;
        auto &config = ConfigManager::GetInstance();
        std::string data_dir = config.GetDataDirectory();

        if (fs::exists(data_dir)) {
            for (const auto &entry: fs::directory_iterator(data_dir)) {
                // Check for database directories
                std::string db_name;
                if (entry.is_directory()) {
                    db_name = entry.path().filename().string();

                    // Verify it's a valid database directory (contains .francodb file)
                    fs::path db_file = entry.path() / (db_name + ".francodb");
                    if (!fs::exists(db_file)) {
                        continue; // Skip if no .francodb file inside
                    }
                }

                if (!db_name.empty() && db_name != "system") {
                    if (auth_manager_->HasDatabaseAccess(session->current_user, db_name)) {
                        db_names.insert(db_name);
                    }
                }
            }
        }

        // Add all unique database names to result set
        for (const auto &db_name: db_names) {
            rs->AddRow({db_name});
        }

        return ExecutionResult::Data(rs);
    }

    ExecutionResult ExecutionEngine::ExecuteShowUsers(ShowUsersStatement *stmt) {
        std::vector<UserInfo> users = auth_manager_->GetAllUsers();
        auto rs = std::make_shared<ResultSet>();
        rs->column_names = {"Username", "Roles"};

        for (const auto &user: users) {
            std::string roles_desc;
            for (const auto &[db, role]: user.db_roles) {
                if (!roles_desc.empty()) roles_desc += ", ";
                roles_desc += db + ":";
                switch (role) {
                    case UserRole::SUPERADMIN: roles_desc += "SUPER";
                        break;
                    case UserRole::ADMIN: roles_desc += "ADMIN";
                        break;
                    case UserRole::NORMAL: roles_desc += "NORMAL";
                        break;
                    case UserRole::READONLY: roles_desc += "READONLY";
                        break;
                    case UserRole::DENIED: roles_desc += "DENIED";
                        break;
                    default: roles_desc += "UNKNOWN";
                        break;
                }
            }
            rs->AddRow({user.username, roles_desc});
        }
        return ExecutionResult::Data(rs);
    }

    ExecutionResult ExecutionEngine::ExecuteCreateUser(CreateUserStatement *stmt) {
        UserRole r = UserRole::NORMAL;
        std::string role_upper = stmt->role_;
        std::transform(role_upper.begin(), role_upper.end(), role_upper.begin(), ::toupper);

        if (role_upper == "ADMIN") r = UserRole::ADMIN;
        else if (role_upper == "SUPERADMIN") r = UserRole::SUPERADMIN;
        else if (role_upper == "READONLY") r = UserRole::READONLY;

        if (auth_manager_->CreateUser(stmt->username_, stmt->password_, r)) {
            return ExecutionResult::Message("User created successfully.");
        }
        return ExecutionResult::Error("User creation failed.");
    }


    ExecutionResult ExecutionEngine::ExecuteShowStatus(ShowStatusStatement *stmt, SessionContext *session) {
        auto rs = std::make_shared<ResultSet>();
        rs->column_names = {"Variable", "Value"};

        // 1. Current User
        rs->AddRow({"Current User", session->current_user.empty() ? "Guest" : session->current_user});

        // 2. Current Database
        rs->AddRow({"Current Database", session->current_db});

        // 3. Current Role
        std::string role_str;
        switch (session->role) {
            case UserRole::SUPERADMIN: role_str = "SUPERADMIN (Full Access)";
                break;
            case UserRole::ADMIN: role_str = "ADMIN (Read/Write)";
                break;
            case UserRole::NORMAL: role_str = "NORMAL (Read/Write)";
                break;
            case UserRole::READONLY: role_str = "READONLY (Select Only)";
                break;
            case UserRole::DENIED: role_str = "DENIED (No Access)";
                break;
        }
        rs->AddRow({"Current Role", role_str});

        // 4. Auth Status
        rs->AddRow({"Authenticated", session->is_authenticated ? "Yes" : "No"});

        return ExecutionResult::Data(rs);
    }

    ExecutionResult ExecutionEngine::ExecuteShowTables(ShowTablesStatement *stmt, SessionContext *session) {
        auto rs = std::make_shared<ResultSet>();
        rs->column_names = {"Tables_in_" + session->current_db};

        // Tables are stored inside the .francodb file and tracked by the catalog
        // We should read from the current catalog instance
        if (!catalog_) {
            return ExecutionResult::Error("No catalog available");
        }

        try {
            // Get all table names from the current catalog
            std::vector<std::string> table_names = catalog_->GetAllTableNames();

            // Sort for nice output
            std::sort(table_names.begin(), table_names.end());

            for (const auto &name: table_names) {
                rs->AddRow({name});
            }

            return ExecutionResult::Data(rs);
        } catch (const std::exception &e) {
            return ExecutionResult::Error("Failed to retrieve tables: " + std::string(e.what()));
        }
    }


    ExecutionResult ExecutionEngine::ExecuteAlterUserRole(AlterUserRoleStatement *stmt) {
        // 1. Convert String Role to Enum
        UserRole r = UserRole::NORMAL; // Default

        std::string role_upper = stmt->role_;
        std::transform(role_upper.begin(), role_upper.end(), role_upper.begin(), ::toupper);

        if (role_upper == "SUPERADMIN") r = UserRole::SUPERADMIN;
        else if (role_upper == "ADMIN") r = UserRole::ADMIN;
        else if (role_upper == "NORMAL") r = UserRole::NORMAL;
        else if (role_upper == "READONLY") r = UserRole::READONLY;
        else if (role_upper == "DENIED") r = UserRole::DENIED;
        else return ExecutionResult::Error("Invalid Role: " + stmt->role_);

        // 2. Determine Target DB (Default to "francodb" if not specified)
        std::string target_db = stmt->db_name_.empty() ? "francodb" : stmt->db_name_;

        // 3. Call AuthManager
        if (auth_manager_->SetUserRole(stmt->username_, target_db, r)) {
            return ExecutionResult::Message("User role updated successfully for DB: " + target_db);
        }

        return ExecutionResult::Error("Failed to update user role (User might not exist or is Root).");
    }

    ExecutionResult ExecutionEngine::ExecuteDeleteUser(DeleteUserStatement *stmt) {
        // Safety: Prevent deleting the current user to avoid locking yourself out?
        // (Optional check, but AuthManager::DeleteUser handles Root protection already)

        if (auth_manager_->DeleteUser(stmt->username_)) {
            return ExecutionResult::Message("User '" + stmt->username_ + "' deleted successfully.");
        }
        return ExecutionResult::Error("Failed to delete user (User might not exist or is Root).");
    }


    ExecutionResult ExecutionEngine::ExecuteCreateDatabase(
        CreateDatabaseStatement *stmt, SessionContext *session) {
        // Check permissions
        if (!auth_manager_->HasPermission(session->role, StatementType::CREATE_DB)) {
            return ExecutionResult::Error("Permission denied: CREATE DATABASE requires ADMIN role");
        }

        try {
            // Protect system/reserved database names
            std::string db_lower = stmt->db_name_;
            std::transform(db_lower.begin(), db_lower.end(), db_lower.begin(), ::tolower);
            if (db_lower == "system" || db_lower == "francodb") {
                return ExecutionResult::Error("Cannot create database with reserved name: " + stmt->db_name_);
            }

            // Check if database already exists
            if (db_registry_->Get(stmt->db_name_) != nullptr) {
                return ExecutionResult::Error("Database '" + stmt->db_name_ + "' already exists");
            }

            // Create the database (this initializes DiskManager, BufferPool, Catalog)
            // Now creates a DIRECTORY structure: data/dbname/
            auto entry = db_registry_->GetOrCreate(stmt->db_name_);

            if (!entry) {
                return ExecutionResult::Error("Failed to create database '" + stmt->db_name_ + "'");
            }

            // CRITICAL: Create the WAL log directory for this database
            if (log_manager_) {
                log_manager_->CreateDatabaseLog(stmt->db_name_);
            }

            // Grant creator ADMIN rights on this database
            UserRole creator_role = (session->role == UserRole::SUPERADMIN)
                                        ? UserRole::SUPERADMIN
                                        : UserRole::ADMIN;
            auth_manager_->SetUserRole(session->current_user, stmt->db_name_, creator_role);

            return ExecutionResult::Message("Database '" + stmt->db_name_ + "' created successfully");
        } catch (const std::exception &e) {
            return ExecutionResult::Error("Failed to create database: " + std::string(e.what()));
        }
    }

    // ====================================================================
    // USE DATABASE
    // ====================================================================
    ExecutionResult ExecutionEngine::ExecuteUseDatabase(
        UseDatabaseStatement *stmt, SessionContext *session) {
        // Check access permissions
        if (!auth_manager_->HasDatabaseAccess(session->current_user, stmt->db_name_)) {
            return ExecutionResult::Error("Access denied to database '" + stmt->db_name_ + "'");
        }

        try {
            // Try registry first
            auto entry = db_registry_->Get(stmt->db_name_);

            // If not already loaded, try to load from disk (database directory exists)
            if (!entry) {
                namespace fs = std::filesystem;
                auto &config = ConfigManager::GetInstance();
                fs::path db_dir = fs::path(config.GetDataDirectory()) / stmt->db_name_;
                fs::path db_file = db_dir / (stmt->db_name_ + ".francodb");
                if (fs::exists(db_file)) {
                    entry = db_registry_->GetOrCreate(stmt->db_name_);
                }
            }

            if (!entry) {
                return ExecutionResult::Error("Database '" + stmt->db_name_ + "' does not exist");
            }

            // CRITICAL: Update the execution engine's pointers
            // Check if this is an external registration or owned entry
            if (db_registry_->ExternalBpm(stmt->db_name_)) {
                bpm_ = db_registry_->ExternalBpm(stmt->db_name_);
                catalog_ = db_registry_->ExternalCatalog(stmt->db_name_);
            } else {
                bpm_ = entry->bpm.get();
                catalog_ = entry->catalog.get();
            }

            // CRITICAL: Switch the log manager to the new database's WAL file
            if (log_manager_) {
                log_manager_->SwitchDatabase(stmt->db_name_);
            }

            // Update executor context
            if (exec_ctx_) {
                delete exec_ctx_;
            }
            exec_ctx_ = new ExecutorContext(bpm_, catalog_, current_transaction_, log_manager_);

            // Update session context
            session->current_db = stmt->db_name_;
            session->role = auth_manager_->GetUserRole(session->current_user, stmt->db_name_);

            if (auth_manager_->IsSuperAdmin(session->current_user)) {
                session->role = UserRole::SUPERADMIN;
            }

            return ExecutionResult::Message("Now using database: " + stmt->db_name_);
        } catch (const std::exception &e) {
            return ExecutionResult::Error("Failed to switch database: " + std::string(e.what()));
        }
    }

    // ====================================================================
    // DROP DATABASE
    // ====================================================================
    ExecutionResult ExecutionEngine::ExecuteDropDatabase(
        DropDatabaseStatement *stmt, SessionContext *session) {
        // Check permissions
        if (!auth_manager_->HasPermission(session->role, StatementType::DROP_DB)) {
            return ExecutionResult::Error("Permission denied: DROP DATABASE requires ADMIN role");
        }

        try {
            // Protect system/reserved databases
            std::string db_lower = stmt->db_name_;
            std::transform(db_lower.begin(), db_lower.end(), db_lower.begin(), ::tolower);
            if (db_lower == "system" || db_lower == "francodb") {
                return ExecutionResult::Error("Cannot drop system database: " + stmt->db_name_);
            }

            auto entry = db_registry_->Get(stmt->db_name_);

            if (!entry) {
                return ExecutionResult::Error("Database '" + stmt->db_name_ + "' does not exist");
            }

            // Prevent dropping currently active database
            if (session->current_db == stmt->db_name_) {
                return ExecutionResult::Error(
                    "Cannot drop currently active database. Switch to another database first.");
            }

            // Flush and close the database
            if (entry->bpm) {
                entry->bpm->FlushAllPages();
            }
            if (entry->catalog) {
                entry->catalog->SaveCatalog();
            }

            // Remove from registry first
            bool removed = db_registry_->Remove(stmt->db_name_);

            // Delete the entire database DIRECTORY
            auto &config = ConfigManager::GetInstance();
            std::string data_dir = config.GetDataDirectory();
            std::filesystem::path db_dir = std::filesystem::path(data_dir) / stmt->db_name_;

            if (std::filesystem::exists(db_dir) && std::filesystem::is_directory(db_dir)) {
                std::filesystem::remove_all(db_dir); // Recursively delete directory and contents
            }

            return ExecutionResult::Message("Database '" + stmt->db_name_ + "' dropped successfully");
        } catch (const std::exception &e) {
            return ExecutionResult::Error("Failed to drop database: " + std::string(e.what()));
        }
    }

    // ====================================================================
    // DESCRIBE TABLE / DESC
    // ====================================================================
    ExecutionResult ExecutionEngine::ExecuteDescribeTable(DescribeTableStatement *stmt) {
        TableMetadata *table_info = catalog_->GetTable(stmt->table_name_);
        if (!table_info) {
            return ExecutionResult::Error("Table not found: " + stmt->table_name_);
        }

        auto rs = std::make_shared<ResultSet>();
        rs->column_names = {"Field", "Type", "Null", "Key", "Default", "Extra"};

        const Schema &schema = table_info->schema_;
        for (uint32_t i = 0; i < schema.GetColumnCount(); i++) {
            const Column &col = schema.GetColumn(i);

            // Field name
            std::string field = col.GetName();

            // Type
            std::string type;
            switch (col.GetType()) {
                case TypeId::INTEGER: type = "RAKAM";
                    break;
                case TypeId::DECIMAL: type = "KASR";
                    break;
                case TypeId::VARCHAR: type = "GOMLA(" + std::to_string(col.GetLength()) + ")";
                    break;
                default: type = "UNKNOWN";
                    break;
            }

            // Null
            std::string null_str = col.IsNullable() ? "YES" : "NO";

            // Key
            std::string key_str;
            if (col.IsPrimaryKey()) key_str = "PRI";
            else if (col.IsUnique()) key_str = "UNI";
            else key_str = "";

            // Default
            std::string default_str = col.HasDefaultValue() ? ValueToString(col.GetDefaultValue().value()) : "NULL";

            // Extra - includes CHECK, AUTO_INCREMENT, and FOREIGN KEY info
            std::string extra;
            if (col.IsAutoIncrement()) extra = "AUTO_INCREMENT";
            if (col.HasCheckConstraint()) {
                if (!extra.empty()) extra += ", ";
                extra += "CHECK(" + col.GetCheckConstraint() + ")";
            }

            // Check if this column is part of a foreign key
            for (const auto &fk: table_info->foreign_keys_) {
                for (const auto &fk_col: fk.columns) {
                    if (fk_col == col.GetName()) {
                        if (!extra.empty()) extra += ", ";
                        extra += "FK(" + fk.ref_table + "." + fk.ref_columns[0] + ")";
                        break;
                    }
                }
            }

            rs->AddRow({field, type, null_str, key_str, default_str, extra});
        }

        return ExecutionResult::Data(rs);
    }

    // ====================================================================
    // SHOW CREATE TABLE
    // ====================================================================
    ExecutionResult ExecutionEngine::ExecuteShowCreateTable(ShowCreateTableStatement *stmt) {
        TableMetadata *table_info = catalog_->GetTable(stmt->table_name_);
        if (!table_info) {
            return ExecutionResult::Error("Table not found: " + stmt->table_name_);
        }

        auto rs = std::make_shared<ResultSet>();
        rs->column_names = {"Table", "Create Table"};

        std::string create_sql = "2E3MEL GADWAL " + stmt->table_name_ + " (\n";

        const Schema &schema = table_info->schema_;
        for (uint32_t i = 0; i < schema.GetColumnCount(); i++) {
            const Column &col = schema.GetColumn(i);

            create_sql += "  " + col.GetName() + " ";

            // Type
            switch (col.GetType()) {
                case TypeId::INTEGER: create_sql += "RAKAM";
                    break;
                case TypeId::DECIMAL: create_sql += "KASR";
                    break;
                case TypeId::VARCHAR: create_sql += "GOMLA";
                    break;
                default: create_sql += "UNKNOWN";
                    break;
            }

            // Primary key
            if (col.IsPrimaryKey()) create_sql += " ASASI";

            // Nullable
            if (!col.IsNullable()) create_sql += " NOT NULL";

            // Unique
            if (col.IsUnique()) create_sql += " UNIQUE";

            // Auto increment
            if (col.IsAutoIncrement()) create_sql += " AUTO_INCREMENT";

            // Check constraint
            if (col.HasCheckConstraint()) {
                create_sql += " FA7S (" + col.GetCheckConstraint() + ")";
            }

            // Default value
            if (col.HasDefaultValue()) {
                create_sql += " DEFAULT " + ValueToString(col.GetDefaultValue().value());
            }

            if (i < schema.GetColumnCount() - 1) create_sql += ",";
            create_sql += "\n";
        }

        // Add foreign keys
        if (!table_info->foreign_keys_.empty()) {
            for (const auto &fk: table_info->foreign_keys_) {
                create_sql += "  , FOREIGN KEY (";
                for (size_t j = 0; j < fk.columns.size(); j++) {
                    if (j > 0) create_sql += ", ";
                    create_sql += fk.columns[j];
                }
                create_sql += ") YOSHEER " + fk.ref_table + " (";
                for (size_t j = 0; j < fk.ref_columns.size(); j++) {
                    if (j > 0) create_sql += ", ";
                    create_sql += fk.ref_columns[j];
                }
                create_sql += ")\n";
            }
        }

        create_sql += ");";

        rs->AddRow({stmt->table_name_, create_sql});
        return ExecutionResult::Data(rs);
    }

    // ====================================================================
    // ALTER TABLE
    // ====================================================================
    ExecutionResult ExecutionEngine::ExecuteAlterTable(AlterTableStatement *stmt) {
        return ExecutionResult::Error(
            "ALTER TABLE is not fully implemented yet. Use DROP and CREATE to modify schema.");
    }


    ExecutionResult ExecutionEngine::ExecuteCheckpoint() {
        // Permission Check (Optional: restrict to Admin)
        // if (!auth_manager_->HasPermission(UserRole::ADMIN, StatementType::CHECKPOINT_CMD)) ...

        CheckpointManager cp_mgr(bpm_, log_manager_);
        cp_mgr.BeginCheckpoint();
        return ExecutionResult::Message("CHECKPOINT SUCCESS");
    }


    
    ExecutionResult ExecutionEngine::ExecuteRecover(RecoverStatement *stmt) {
        std::cout << "[SYSTEM] Global Lock Verified. Preparing for Time Travel..." << std::endl;
        
        
        uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        if (stmt->timestamp_ > now) {
            return ExecutionResult::Error("Cannot travel to the future! Timestamp is > Now.");
        }
        
        if (stmt->timestamp_ == 0) {
            return ExecutionResult::Error("Invalid timestamp (0).");
        }

        // 3. Force Buffer Pool Flush
        // We ensure the current state is safely on disk before we mess with it.
        bpm_->FlushAllPages();
        bpm_->Clear(); 
        log_manager_->StopFlushThread();
        
        std::cout << "[SYSTEM] Initiating Time Travel to: " << stmt->timestamp_ << std::endl;

        try {
            // Create checkpoint manager for recovery operations
            CheckpointManager cp_mgr(bpm_, log_manager_);
            RecoveryManager recovery(log_manager_, catalog_, bpm_, &cp_mgr);
            
            // This now calls RollbackToTime internally
            recovery.RecoverToTime(stmt->timestamp_);
            
            // Create a new checkpoint after recovery
            cp_mgr.BeginCheckpoint();
            
        } catch (const std::exception& e) {
            return ExecutionResult::Error(std::string("Recovery Failed: ") + e.what());
        }

        std::cout << "[SYSTEM] Time Travel Complete. Resuming normal operations." << std::endl;
        return ExecutionResult::Message("TIME TRAVEL COMPLETE. System state reverted.");
    }
} // namespace francodb
