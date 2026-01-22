#pragma once

#include <vector>
#include <memory>
#include <string>
#include <atomic>
#include "execution/execution_result.h"
#include "execution/executor_context.h"
#include "parser/statement.h"
#include "concurrency/transaction.h"
#include "catalog/catalog.h"
#include "common/auth_manager.h"
#include "network/database_registry.h" 
#include "network/session_context.h"
#include "recovery/log_manager.h"

namespace francodb {

// Forward declarations for specialized executors (SRP - Issue #11)
class DDLExecutor;
class DMLExecutor;

    class ExecutionEngine {
    public:
        static std::shared_mutex global_lock_;
        
        ExecutionEngine(BufferPoolManager *bpm, Catalog *catalog, AuthManager *auth_manager, 
                        DatabaseRegistry *db_registry, LogManager *log_manager);

        ~ExecutionEngine();

        Transaction *GetCurrentTransaction();
        Transaction *GetCurrentTransactionForWrite();
        void AutoCommitIfNeeded();
        Catalog *GetCatalog() { return catalog_; }

        /**
         * Execute a statement.
         * 
         * ARCHITECTURE NOTE (Issue #10 & #11 Fix):
         * =========================================
         * This method delegates to specialized executors:
         * - DDL operations -> ddl_executor_
         * - DML operations -> dml_executor_  
         * - System operations -> handled inline (minimal)
         * 
         * The giant switch statement has been eliminated.
         */
        ExecutionResult Execute(Statement *stmt, SessionContext *session);

    private:
        // ========================================================================
        // FACTORY PATTERN INITIALIZATION (Issue #10 - OCP Fix)
        // ========================================================================
        void InitializeExecutorFactory();

        // ========================================================================
        // HELPER METHODS
        // ========================================================================
        std::string ValueToString(const Value &v);

        // ========================================================================
        // TRANSACTION OPERATIONS (Kept inline - simple coordination)
        // ========================================================================
        ExecutionResult ExecuteBegin();
        ExecutionResult ExecuteRollback();
        ExecutionResult ExecuteCommit();

        // ========================================================================
        // SYSTEM OPERATIONS (Kept inline - require special access)
        // ========================================================================
        ExecutionResult ExecuteCheckpoint();
        ExecutionResult ExecuteRecover(RecoverStatement *stmt);
        ExecutionResult ExecuteShowDatabases(ShowDatabasesStatement *stmt, SessionContext *session);
        ExecutionResult ExecuteShowTables(ShowTablesStatement *stmt, SessionContext *session); 
        ExecutionResult ExecuteShowUsers(ShowUsersStatement *stmt);
        ExecutionResult ExecuteShowStatus(ShowStatusStatement *stmt, SessionContext *session); 
        ExecutionResult ExecuteWhoAmI(WhoAmIStatement *stmt, SessionContext *session);
        ExecutionResult ExecuteCreateUser(CreateUserStatement *stmt);
        ExecutionResult ExecuteAlterUserRole(AlterUserRoleStatement *stmt);
        ExecutionResult ExecuteDeleteUser(DeleteUserStatement *stmt);
        ExecutionResult ExecuteCreateDatabase(CreateDatabaseStatement *stmt, SessionContext *session);
        ExecutionResult ExecuteUseDatabase(UseDatabaseStatement *stmt, SessionContext *session);
        ExecutionResult ExecuteDropDatabase(DropDatabaseStatement *stmt, SessionContext *session);

        // ========================================================================
        // CORE DEPENDENCIES (order matches constructor initialization)
        // ========================================================================
        BufferPoolManager *bpm_;
        Catalog *catalog_;
        AuthManager *auth_manager_;
        DatabaseRegistry *db_registry_;
        LogManager *log_manager_;
        ExecutorContext *exec_ctx_;
        Transaction *current_transaction_;
        
        // ========================================================================
        // SPECIALIZED EXECUTORS (Issue #11 - SRP Fix)
        // ========================================================================
        std::unique_ptr<DDLExecutor> ddl_executor_;
        std::unique_ptr<DMLExecutor> dml_executor_;
        
        // ========================================================================
        // THREAD SAFETY (Issue #4 & #5 Fix)
        // ========================================================================
        // Cache-line aligned to prevent false sharing
        alignas(64) std::atomic<int> next_txn_id_{1};
        alignas(64) bool in_explicit_transaction_;
    };
} // namespace francodb