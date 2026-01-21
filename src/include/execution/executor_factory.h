#pragma once

#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>
#include "parser/statement.h"
#include "execution/execution_result.h"

namespace francodb {

// Forward declarations
class ExecutorContext;
class SessionContext;
class Transaction;

/**
 * ExecutorFunc - Function signature for statement executors.
 * 
 * Each executor function takes:
 *   - Statement pointer (to be dynamic_cast'd to specific type)
 *   - ExecutorContext (buffer pool, catalog, log manager)
 *   - SessionContext (current user, database, permissions)
 *   - Transaction pointer (for DML operations)
 * 
 * Returns:
 *   - ExecutionResult (success message, data, or error)
 */
using ExecutorFunc = std::function<ExecutionResult(
    Statement*, 
    ExecutorContext*, 
    SessionContext*,
    Transaction*
)>;

/**
 * ExecutorFactory - Registry pattern for statement executors.
 * 
 * PROBLEM SOLVED:
 * - Eliminates the 200+ line switch statement in ExecutionEngine::Execute()
 * - Allows adding new statement types without modifying existing code (OCP)
 * - Decouples statement parsing from execution
 * 
 * USAGE:
 * 
 * 1. Register executors at startup (or via static initialization):
 * 
 *   ExecutorFactory::Instance().Register(StatementType::INSERT, 
 *       [](Statement* stmt, ExecutorContext* ctx, SessionContext* session, Transaction* txn) {
 *           auto* insert_stmt = dynamic_cast<InsertStatement*>(stmt);
 *           InsertExecutor executor(ctx, insert_stmt, txn);
 *           executor.Init();
 *           Tuple t;
 *           executor.Next(&t);
 *           return ExecutionResult::Message("INSERT 1");
 *       });
 * 
 * 2. Execute statements through the factory:
 * 
 *   auto result = ExecutorFactory::Instance().Execute(stmt, ctx, session, txn);
 */
class ExecutorFactory {
public:
    /**
     * Get the singleton instance.
     */
    static ExecutorFactory& Instance() {
        static ExecutorFactory instance;
        return instance;
    }
    
    /**
     * Register an executor for a specific statement type.
     * 
     * @param type The statement type to handle
     * @param executor The function that executes this statement type
     */
    void Register(StatementType type, ExecutorFunc executor) {
        std::lock_guard<std::mutex> lock(mutex_);
        executors_[type] = std::move(executor);
    }
    
    /**
     * Unregister an executor (for testing).
     */
    void Unregister(StatementType type) {
        std::lock_guard<std::mutex> lock(mutex_);
        executors_.erase(type);
    }
    
    /**
     * Check if an executor is registered for a statement type.
     */
    bool HasExecutor(StatementType type) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return executors_.find(type) != executors_.end();
    }
    
    /**
     * Execute a statement.
     * 
     * @param stmt The statement to execute
     * @param ctx Executor context (buffer pool, catalog, etc.)
     * @param session Session context (user, database, permissions)
     * @param txn Current transaction (may be nullptr for read-only)
     * @return ExecutionResult
     */
    ExecutionResult Execute(Statement* stmt, 
                           ExecutorContext* ctx, 
                           SessionContext* session,
                           Transaction* txn) {
        if (stmt == nullptr) {
            return ExecutionResult::Error("Empty statement");
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = executors_.find(stmt->GetType());
        if (it == executors_.end()) {
            return ExecutionResult::Error(
                "Unknown statement type: " + std::to_string(static_cast<int>(stmt->GetType()))
            );
        }
        
        try {
            return it->second(stmt, ctx, session, txn);
        } catch (const std::exception& e) {
            return ExecutionResult::Error(std::string("Execution error: ") + e.what());
        }
    }
    
    /**
     * Get all registered statement types (for debugging/introspection).
     */
    std::vector<StatementType> GetRegisteredTypes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<StatementType> types;
        types.reserve(executors_.size());
        for (const auto& [type, _] : executors_) {
            types.push_back(type);
        }
        return types;
    }
    
    /**
     * Clear all registrations (for testing).
     */
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        executors_.clear();
    }

private:
    ExecutorFactory() = default;
    ~ExecutorFactory() = default;
    
    // Non-copyable
    ExecutorFactory(const ExecutorFactory&) = delete;
    ExecutorFactory& operator=(const ExecutorFactory&) = delete;
    
    mutable std::mutex mutex_;
    std::unordered_map<StatementType, ExecutorFunc> executors_;
};

/**
 * REGISTER_EXECUTOR - Macro for auto-registration.
 * 
 * Place this in the .cpp file of each executor to automatically
 * register it with the factory at program startup.
 * 
 * USAGE:
 *   // In insert_executor.cpp
 *   REGISTER_EXECUTOR(INSERT, InsertExecutorHandler);
 */
#define REGISTER_EXECUTOR(type, handler) \
    namespace { \
        static bool _registered_##type = []() { \
            ExecutorFactory::Instance().Register(StatementType::type, handler); \
            return true; \
        }(); \
    }

/**
 * ExecutorRegistrar - Alternative registration using RAII.
 * 
 * USAGE:
 *   // At file scope
 *   static ExecutorRegistrar<StatementType::INSERT> _insert_registrar(
 *       [](Statement* stmt, ...) { ... }
 *   );
 */
template<StatementType Type>
class ExecutorRegistrar {
public:
    explicit ExecutorRegistrar(ExecutorFunc executor) {
        ExecutorFactory::Instance().Register(Type, std::move(executor));
    }
};

} // namespace francodb

