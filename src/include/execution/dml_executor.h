#pragma once

#include "execution/execution_result.h"
#include "execution/executor_context.h"
#include "concurrency/transaction.h"
#include "recovery/log_manager.h"

namespace francodb {

// Forward declarations
class InsertStatement;
class SelectStatement;
class UpdateStatement;
class DeleteStatement;
class SessionContext;
class BufferPoolManager;
class Catalog;
class TableMetadata;

/**
 * DMLExecutor - Data Manipulation Language operations
 * 
 * PROBLEM SOLVED:
 * - Extracts DML logic from ExecutionEngine (SRP)
 * - Single class responsible for data modifications
 * - Handles transaction integration
 * 
 * OPERATIONS:
 * - INSERT
 * - SELECT
 * - UPDATE
 * - DELETE
 */
class DMLExecutor {
public:
    DMLExecutor(BufferPoolManager* bpm, Catalog* catalog, LogManager* log_manager = nullptr)
        : bpm_(bpm), catalog_(catalog), log_manager_(log_manager) {}
    
    // ========================================================================
    // DATA OPERATIONS
    // ========================================================================
    
    /**
     * Insert a tuple into a table.
     * 
     * @param stmt Insert statement
     * @param txn Transaction context
     * @return Execution result
     */
    ExecutionResult Insert(InsertStatement* stmt, Transaction* txn);
    
    /**
     * Select tuples from a table.
     * 
     * @param stmt Select statement
     * @param session Session context (for database context, permissions)
     * @param txn Transaction context
     * @return Execution result with result set
     */
    ExecutionResult Select(SelectStatement* stmt, SessionContext* session, Transaction* txn);
    
    /**
     * Update tuples in a table.
     * 
     * @param stmt Update statement
     * @param txn Transaction context
     * @return Execution result
     */
    ExecutionResult Update(UpdateStatement* stmt, Transaction* txn);
    
    /**
     * Delete tuples from a table.
     * 
     * @param stmt Delete statement
     * @param txn Transaction context
     * @return Execution result
     */
    ExecutionResult Delete(DeleteStatement* stmt, Transaction* txn);
    
    // ========================================================================
    // CONFIGURATION
    // ========================================================================
    
    void SetBufferPoolManager(BufferPoolManager* bpm) { bpm_ = bpm; }
    void SetCatalog(Catalog* catalog) { catalog_ = catalog; }
    void SetLogManager(LogManager* log_manager) { log_manager_ = log_manager; }
    
    BufferPoolManager* GetBufferPoolManager() const { return bpm_; }
    Catalog* GetCatalog() const { return catalog_; }
    LogManager* GetLogManager() const { return log_manager_; }

private:
    BufferPoolManager* bpm_;
    Catalog* catalog_;
    LogManager* log_manager_;
    
    // ========================================================================
    // PRIVATE HELPERS
    // ========================================================================
    
    /**
     * Check if we can use an index scan for a SELECT
     */
    bool CanUseIndexScan(SelectStatement* stmt) const;
    
    /**
     * Execute SELECT with sequential scan
     */
    ExecutionResult ExecuteSeqScan(SelectStatement* stmt, ExecutorContext* ctx, Transaction* txn);
    
    /**
     * Execute SELECT with index scan
     */
    ExecutionResult ExecuteIndexScan(SelectStatement* stmt, ExecutorContext* ctx, Transaction* txn);
    
    /**
     * Execute JOIN query
     */
    ExecutionResult ExecuteJoin(SelectStatement* stmt, ExecutorContext* ctx, Transaction* txn);
};

} // namespace francodb

