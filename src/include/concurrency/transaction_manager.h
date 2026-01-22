#pragma once

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <memory>
#include "concurrency/transaction.h"
#include "recovery/log_manager.h"

namespace francodb {

/**
 * TransactionManager - Centralized transaction lifecycle management
 * 
 * PROBLEM SOLVED:
 * - Extracts transaction logic from ExecutionEngine (SRP)
 * - Provides thread-safe transaction ID generation
 * - Manages transaction state transitions
 * - Handles commit/abort logging
 * 
 * TRANSACTION LIFECYCLE:
 * 1. Begin() -> RUNNING
 * 2. Commit() -> COMMITTED (log flushed, locks released)
 * 3. Abort() -> ABORTED (undo applied, locks released)
 */
class TransactionManager {
public:
    /**
     * Constructor
     * 
     * @param log_manager Log manager for WAL operations
     */
    explicit TransactionManager(LogManager* log_manager = nullptr)
        : log_manager_(log_manager), next_txn_id_(1) {}
    
    ~TransactionManager() {
        // Abort all running transactions on shutdown
        std::lock_guard<std::mutex> lock(latch_);
        for (auto& [txn_id, txn] : active_transactions_) {
            if (txn && txn->GetState() == Transaction::TransactionState::RUNNING) {
                txn->SetState(Transaction::TransactionState::ABORTED);
            }
        }
        active_transactions_.clear();
    }
    
    // ========================================================================
    // TRANSACTION LIFECYCLE
    // ========================================================================
    
    /**
     * Begin a new transaction.
     * 
     * @return Pointer to the new transaction
     */
    Transaction* Begin() {
        txn_id_t txn_id = next_txn_id_.fetch_add(1, std::memory_order_relaxed);
        auto txn = std::make_unique<Transaction>(txn_id);
        
        // Log BEGIN record
        if (log_manager_) {
            LogRecord rec(txn_id, INVALID_LSN, LogRecordType::BEGIN);
            lsn_t lsn = log_manager_->AppendLogRecord(rec);
            txn->SetPrevLSN(lsn);
            log_manager_->BeginTransaction(txn_id);
        }
        
        Transaction* txn_ptr = txn.get();
        
        {
            std::lock_guard<std::mutex> lock(latch_);
            active_transactions_[txn_id] = std::move(txn);
        }
        
        return txn_ptr;
    }
    
    /**
     * Commit a transaction.
     * 
     * @param txn The transaction to commit
     * @return true if successful
     */
    bool Commit(Transaction* txn) {
        if (!txn) return false;
        
        if (txn->GetState() != Transaction::TransactionState::RUNNING) {
            return false;
        }
        
        // Log COMMIT record
        if (log_manager_) {
            LogRecord rec(txn->GetTransactionId(), txn->GetPrevLSN(), LogRecordType::COMMIT);
            log_manager_->AppendLogRecord(rec);
            
            // FORCE: Flush log to disk for durability
            log_manager_->Flush(true);
            
            log_manager_->CommitTransaction(txn->GetTransactionId());
        }
        
        txn->SetState(Transaction::TransactionState::COMMITTED);
        
        // Remove from active transactions
        {
            std::lock_guard<std::mutex> lock(latch_);
            active_transactions_.erase(txn->GetTransactionId());
        }
        
        return true;
    }
    
    /**
     * Abort a transaction and undo all modifications.
     * 
     * @param txn The transaction to abort
     * @return true if successful
     */
    bool Abort(Transaction* txn) {
        if (!txn) return false;
        
        if (txn->GetState() != Transaction::TransactionState::RUNNING) {
            return false;
        }
        
        // Undo all modifications in reverse order
        const auto& modifications = txn->GetModifications();
        for (auto it = modifications.rbegin(); it != modifications.rend(); ++it) {
            // Apply undo logic based on modification type
            // (This should be done by RecoveryManager in production)
        }
        
        // Log ABORT record
        if (log_manager_) {
            LogRecord rec(txn->GetTransactionId(), txn->GetPrevLSN(), LogRecordType::ABORT);
            log_manager_->AppendLogRecord(rec);
            log_manager_->AbortTransaction(txn->GetTransactionId());
        }
        
        txn->SetState(Transaction::TransactionState::ABORTED);
        
        // Remove from active transactions
        {
            std::lock_guard<std::mutex> lock(latch_);
            active_transactions_.erase(txn->GetTransactionId());
        }
        
        return true;
    }
    
    // ========================================================================
    // TRANSACTION QUERIES
    // ========================================================================
    
    /**
     * Get a transaction by ID.
     * 
     * @param txn_id Transaction ID
     * @return Pointer to transaction, or nullptr if not found
     */
    Transaction* GetTransaction(txn_id_t txn_id) {
        std::lock_guard<std::mutex> lock(latch_);
        auto it = active_transactions_.find(txn_id);
        if (it == active_transactions_.end()) {
            return nullptr;
        }
        return it->second.get();
    }
    
    /**
     * Get count of active transactions.
     */
    size_t GetActiveTransactionCount() const {
        std::lock_guard<std::mutex> lock(latch_);
        return active_transactions_.size();
    }
    
    /**
     * Get all active transaction IDs.
     */
    std::vector<txn_id_t> GetActiveTransactionIds() const {
        std::lock_guard<std::mutex> lock(latch_);
        std::vector<txn_id_t> ids;
        ids.reserve(active_transactions_.size());
        for (const auto& [txn_id, _] : active_transactions_) {
            ids.push_back(txn_id);
        }
        return ids;
    }
    
    /**
     * Check if a transaction is active.
     */
    bool IsActive(txn_id_t txn_id) const {
        std::lock_guard<std::mutex> lock(latch_);
        return active_transactions_.find(txn_id) != active_transactions_.end();
    }
    
    // ========================================================================
    // UTILITY
    // ========================================================================
    
    /**
     * Set log manager (for late binding).
     */
    void SetLogManager(LogManager* log_manager) {
        log_manager_ = log_manager;
    }
    
    /**
     * Get next transaction ID (for debugging).
     */
    txn_id_t GetNextTxnId() const {
        return next_txn_id_.load();
    }

private:
    LogManager* log_manager_;
    std::atomic<txn_id_t> next_txn_id_;
    
    mutable std::mutex latch_;
    std::unordered_map<txn_id_t, std::unique_ptr<Transaction>> active_transactions_;
};

} // namespace francodb

