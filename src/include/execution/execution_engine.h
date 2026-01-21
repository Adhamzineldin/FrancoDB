#pragma once

#include <vector>
#include <memory>
#include <string>
#include "execution/execution_result.h"
#include "execution/executor_context.h"
#include "parser/statement.h"
#include "concurrency/transaction.h"
#include "catalog/catalog.h"
#include "common/auth_manager.h"
#include "network/database_registry.h" 
#include "network/session_context.h"
#include "recovery/log_manager.h" // [ACID]

namespace francodb {
    class ExecutionEngine {
    public:
        static std::shared_mutex global_lock_;
        // [ACID] Updated Constructor
        ExecutionEngine(BufferPoolManager *bpm, Catalog *catalog, AuthManager *auth_manager, 
                        DatabaseRegistry *db_registry, LogManager *log_manager);

        ~ExecutionEngine();

        Transaction *GetCurrentTransaction();

        Transaction *GetCurrentTransactionForWrite();

        void AutoCommitIfNeeded();

        Catalog *GetCatalog() { return catalog_; }

        ExecutionResult Execute(Statement *stmt, SessionContext *session);

    private:
        std::string ValueToString(const Value &v);

        // --- DATA & DDL EXECUTORS ---
        ExecutionResult ExecuteCreate(CreateStatement *stmt);

        ExecutionResult ExecuteCreateIndex(CreateIndexStatement *stmt);

        ExecutionResult ExecuteInsert(InsertStatement *stmt);

        ExecutionResult ExecuteSelect(SelectStatement *stmt);

        ExecutionResult ExecuteDrop(DropStatement *stmt);

        ExecutionResult ExecuteDelete(DeleteStatement *stmt);

        ExecutionResult ExecuteUpdate(UpdateStatement *stmt);
        
        // --- SCHEMA INSPECTION ---
        ExecutionResult ExecuteDescribeTable(DescribeTableStatement *stmt);
        ExecutionResult ExecuteShowCreateTable(ShowCreateTableStatement *stmt);
        ExecutionResult ExecuteAlterTable(AlterTableStatement *stmt);

        ExecutionResult ExecuteCheckpoint();

        ExecutionResult ExecuteRecover(RecoverStatement *stmt);

        // --- TRANSACTION EXECUTORS ---
        ExecutionResult ExecuteBegin();

        ExecutionResult ExecuteRollback();

        ExecutionResult ExecuteCommit();

        // --- SYSTEM/METADATA EXECUTORS ---
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

        BufferPoolManager *bpm_;
        AuthManager *auth_manager_;
        Catalog *catalog_;
        DatabaseRegistry *db_registry_;
        LogManager *log_manager_; // [ACID]
        ExecutorContext *exec_ctx_;
        Transaction *current_transaction_;
        int next_txn_id_;
        bool in_explicit_transaction_;
    };
} // namespace francodb