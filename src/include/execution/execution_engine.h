#pragma once

#include <vector>
#include <memory>
#include <string>
#include <atomic>
#include <unordered_map>
#include <functional>
#include "execution/execution_result.h"
#include "execution/executor_context.h"
#include "parser/statement.h"
#include "concurrency/transaction.h"
#include "concurrency/lock_manager.h"
#include "catalog/catalog.h"
#include "common/auth_manager.h"
#include "network/database_registry.h" 
#include "network/session_context.h"
#include "recovery/log_manager.h"

namespace francodb {

// Forward declarations for specialized executors (SOLID - SRP)
class DDLExecutor;
class DMLExecutor;
class SystemExecutor;
class UserExecutor;
class DatabaseExecutor;
class TransactionExecutor;

/**
 * ExecutionEngine - Query Execution Coordinator
 * 
 * SOLID COMPLIANCE:
 * =================
 * This class follows the Open/Closed Principle using a dispatch map pattern.
 * Adding a new statement type only requires adding one entry to InitializeDispatchMap().
 * No modification to Execute() method is needed!
 * 
 * Architecture:
 * - dispatch_map_: Maps StatementType → Handler function
 * - Specialized executors handle the actual work (SRP)
 * - Execute() is a thin coordinator (~30 lines)
 * 
 * @author FrancoDB Team
 */
class ExecutionEngine {
public:
    static std::shared_mutex global_lock_;
    
    // Handler function type for dispatch map
    using StatementHandler = std::function<ExecutionResult(Statement*, SessionContext*, Transaction*)>;
    
    ExecutionEngine(BufferPoolManager* bpm, Catalog* catalog, AuthManager* auth_manager, 
                    DatabaseRegistry* db_registry, LogManager* log_manager);

    ~ExecutionEngine();

    // ========================================================================
    // PUBLIC API
    // ========================================================================
    
    /**
     * Execute a statement using the dispatch map.
     * No switch/if-else chains - pure lookup + call.
     */
    ExecutionResult Execute(Statement* stmt, SessionContext* session);
    
    Catalog* GetCatalog() { return catalog_; }
    Transaction* GetCurrentTransaction();
    Transaction* GetCurrentTransactionForWrite();

private:
    // ========================================================================
    // DISPATCH MAP (Open/Closed Principle)
    // ========================================================================
    std::unordered_map<StatementType, StatementHandler> dispatch_map_;
    
    /**
     * Initialize the dispatch map with all statement handlers.
     * To add a new statement type, just add one line here!
     */
    void InitializeDispatchMap();

    // ========================================================================
    // HELPER METHODS
    // ========================================================================
    std::string ValueToString(const Value& v);
    std::string CheckPermissions(StatementType type, SessionContext* session);
    ExecutionResult HandleUseDatabase(UseDatabaseStatement* stmt, SessionContext* session, Transaction* txn);

    // ========================================================================
    // RECOVERY OPERATIONS
    // ========================================================================
    ExecutionResult ExecuteCheckpoint();
    ExecutionResult ExecuteRecover(RecoverStatement* stmt);

    // ========================================================================
    // CORE DEPENDENCIES (order matches constructor initialization)
    // ========================================================================
    BufferPoolManager* bpm_;
    Catalog* catalog_;
    AuthManager* auth_manager_;
    DatabaseRegistry* db_registry_;
    LogManager* log_manager_;
    ExecutorContext* exec_ctx_;
    
    // ========================================================================
    // SPECIALIZED EXECUTORS (SOLID - SRP Compliance)
    // ========================================================================
    std::unique_ptr<DDLExecutor> ddl_executor_;
    std::unique_ptr<DMLExecutor> dml_executor_;
    std::unique_ptr<SystemExecutor> system_executor_;
    std::unique_ptr<UserExecutor> user_executor_;
    std::unique_ptr<DatabaseExecutor> database_executor_;
    std::unique_ptr<TransactionExecutor> transaction_executor_;
    
    // ========================================================================
    // CONCURRENCY CONTROL
    // ========================================================================
    std::unique_ptr<LockManager> lock_manager_;
    
    // ========================================================================
    // THREAD SAFETY (Issue #4 & #5 Fix)
    // ========================================================================
    alignas(64) std::atomic<int> next_txn_id_{1};
};

} // namespace francodb