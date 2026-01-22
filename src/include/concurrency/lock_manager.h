#pragma once

#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <condition_variable>
#include <vector>
#include <queue>
#include <atomic>
#include <thread>
#include <chrono>
#include "common/config.h"
#include "common/rid.h"

namespace francodb {

/**
 * LockMode - Types of locks that can be acquired
 */
enum class LockMode {
    SHARED,      // Read lock (multiple readers allowed)
    EXCLUSIVE    // Write lock (single writer, no readers)
};

/**
 * LockRequestStatus - Status of a lock request
 */
enum class LockRequestStatus {
    WAITING,     // Request is queued
    GRANTED,     // Lock has been granted
    ABORTED      // Request was aborted (deadlock, timeout)
};

/**
 * LockRequest - Represents a request for a lock
 */
struct LockRequest {
    txn_id_t txn_id;
    LockMode mode;
    LockRequestStatus status;
    
    LockRequest(txn_id_t tid, LockMode m)
        : txn_id(tid), mode(m), status(LockRequestStatus::WAITING) {}
};

/**
 * LockRequestQueue - Queue of lock requests for a single resource
 */
struct LockRequestQueue {
    std::list<LockRequest> request_queue;
    std::condition_variable cv;
    bool upgrading = false;  // True if a txn is waiting for upgrade
    txn_id_t upgrading_txn_id = INVALID_TXN_ID;
};

/**
 * LockManager - Two-Phase Locking with Deadlock Detection
 * 
 * PROBLEM SOLVED:
 * - Enforces consistent lock ordering to prevent deadlocks
 * - Provides row-level locking for high concurrency
 * - Implements wait-die deadlock prevention scheme
 * 
 * LOCK HIERARCHY (must acquire in this order):
 * 1. Table locks (coarse-grained)
 * 2. Page locks (medium-grained)
 * 3. Row locks (fine-grained)
 * 
 * FEATURES:
 * - Shared (read) and Exclusive (write) locks
 * - Lock upgrade (S -> X)
 * - Deadlock detection via wait-for graph
 * - Lock timeout
 */
class LockManager {
public:
    LockManager() : enable_deadlock_detection_(true) {
        // Start deadlock detection thread
        deadlock_detection_thread_ = std::thread(&LockManager::DeadlockDetectionLoop, this);
    }
    
    ~LockManager() {
        // Stop deadlock detection thread
        enable_deadlock_detection_ = false;
        if (deadlock_detection_thread_.joinable()) {
            deadlock_detection_thread_.join();
        }
    }
    
    // ========================================================================
    // TABLE-LEVEL LOCKS
    // ========================================================================
    
    /**
     * Acquire a table-level lock.
     * 
     * @param txn Transaction requesting the lock
     * @param table_name Name of the table
     * @param mode Lock mode (SHARED or EXCLUSIVE)
     * @return true if lock acquired, false if aborted
     */
    bool LockTable(txn_id_t txn_id, const std::string& table_name, LockMode mode) {
        std::unique_lock<std::mutex> lock(table_latch_);
        
        auto& queue = table_lock_map_[table_name];
        
        // Check for existing lock by this transaction
        for (auto& req : queue.request_queue) {
            if (req.txn_id == txn_id) {
                if (req.mode == mode || req.mode == LockMode::EXCLUSIVE) {
                    return true;  // Already have sufficient lock
                }
                // Need upgrade from SHARED to EXCLUSIVE
                if (queue.upgrading) {
                    return false;  // Another upgrade in progress
                }
                queue.upgrading = true;
                queue.upgrading_txn_id = txn_id;
                
                // Wait for all other shared locks to release
                queue.cv.wait(lock, [&]() {
                    return CanGrantUpgrade(queue, txn_id);
                });
                
                req.mode = LockMode::EXCLUSIVE;
                queue.upgrading = false;
                queue.upgrading_txn_id = INVALID_TXN_ID;
                return true;
            }
        }
        
        // New lock request
        queue.request_queue.emplace_back(txn_id, mode);
        auto& request = queue.request_queue.back();
        
        // Wait for lock to be grantable
        queue.cv.wait(lock, [&]() {
            return CanGrantLock(queue, request);
        });
        
        request.status = LockRequestStatus::GRANTED;
        
        // Track what locks this transaction holds
        txn_table_locks_[txn_id].insert(table_name);
        
        return true;
    }
    
    /**
     * Release a table-level lock.
     */
    bool UnlockTable(txn_id_t txn_id, const std::string& table_name) {
        std::unique_lock<std::mutex> lock(table_latch_);
        
        auto it = table_lock_map_.find(table_name);
        if (it == table_lock_map_.end()) {
            return false;
        }
        
        auto& queue = it->second;
        
        // Find and remove the lock request
        for (auto req_it = queue.request_queue.begin(); 
             req_it != queue.request_queue.end(); ++req_it) {
            if (req_it->txn_id == txn_id) {
                queue.request_queue.erase(req_it);
                queue.cv.notify_all();
                
                txn_table_locks_[txn_id].erase(table_name);
                return true;
            }
        }
        
        return false;
    }
    
    // ========================================================================
    // ROW-LEVEL LOCKS
    // ========================================================================
    
    /**
     * Acquire a row-level lock.
     * 
     * @param txn_id Transaction ID
     * @param rid Row ID to lock
     * @param mode Lock mode
     * @return true if lock acquired
     */
    bool LockRow(txn_id_t txn_id, const RID& rid, LockMode mode) {
        std::unique_lock<std::mutex> lock(row_latch_);
        
        auto& queue = row_lock_map_[rid];
        
        // Check for existing lock
        for (auto& req : queue.request_queue) {
            if (req.txn_id == txn_id) {
                if (req.mode == mode || req.mode == LockMode::EXCLUSIVE) {
                    return true;
                }
                // Need upgrade
                if (queue.upgrading) {
                    return false;
                }
                queue.upgrading = true;
                queue.upgrading_txn_id = txn_id;
                
                queue.cv.wait(lock, [&]() {
                    return CanGrantUpgrade(queue, txn_id);
                });
                
                req.mode = LockMode::EXCLUSIVE;
                queue.upgrading = false;
                queue.upgrading_txn_id = INVALID_TXN_ID;
                return true;
            }
        }
        
        // New request
        queue.request_queue.emplace_back(txn_id, mode);
        auto& request = queue.request_queue.back();
        
        // Add to wait-for graph for deadlock detection
        if (!CanGrantLock(queue, request)) {
            AddWaitForEdges(txn_id, queue);
        }
        
        queue.cv.wait(lock, [&]() {
            return CanGrantLock(queue, request) || 
                   request.status == LockRequestStatus::ABORTED;
        });
        
        RemoveWaitForEdges(txn_id);
        
        if (request.status == LockRequestStatus::ABORTED) {
            // Remove from queue
            queue.request_queue.remove_if([txn_id](const LockRequest& r) {
                return r.txn_id == txn_id;
            });
            return false;
        }
        
        request.status = LockRequestStatus::GRANTED;
        txn_row_locks_[txn_id].insert(rid);
        
        return true;
    }
    
    /**
     * Release a row-level lock.
     */
    bool UnlockRow(txn_id_t txn_id, const RID& rid) {
        std::unique_lock<std::mutex> lock(row_latch_);
        
        auto it = row_lock_map_.find(rid);
        if (it == row_lock_map_.end()) {
            return false;
        }
        
        auto& queue = it->second;
        
        for (auto req_it = queue.request_queue.begin();
             req_it != queue.request_queue.end(); ++req_it) {
            if (req_it->txn_id == txn_id) {
                queue.request_queue.erase(req_it);
                queue.cv.notify_all();
                
                txn_row_locks_[txn_id].erase(rid);
                return true;
            }
        }
        
        return false;
    }
    
    // ========================================================================
    // TRANSACTION RELEASE
    // ========================================================================
    
    /**
     * Release all locks held by a transaction.
     * Called during commit or abort.
     */
    void ReleaseAllLocks(txn_id_t txn_id) {
        // Release table locks
        {
            std::unique_lock<std::mutex> lock(table_latch_);
            auto it = txn_table_locks_.find(txn_id);
            if (it != txn_table_locks_.end()) {
                for (const auto& table_name : it->second) {
                    auto map_it = table_lock_map_.find(table_name);
                    if (map_it != table_lock_map_.end()) {
                        auto& queue = map_it->second;
                        queue.request_queue.remove_if([txn_id](const LockRequest& r) {
                            return r.txn_id == txn_id;
                        });
                        queue.cv.notify_all();
                    }
                }
                txn_table_locks_.erase(it);
            }
        }
        
        // Release row locks
        {
            std::unique_lock<std::mutex> lock(row_latch_);
            auto it = txn_row_locks_.find(txn_id);
            if (it != txn_row_locks_.end()) {
                for (const auto& rid : it->second) {
                    auto map_it = row_lock_map_.find(rid);
                    if (map_it != row_lock_map_.end()) {
                        auto& queue = map_it->second;
                        queue.request_queue.remove_if([txn_id](const LockRequest& r) {
                            return r.txn_id == txn_id;
                        });
                        queue.cv.notify_all();
                    }
                }
                txn_row_locks_.erase(it);
            }
        }
        
        // Remove from wait-for graph
        RemoveWaitForEdges(txn_id);
    }

private:
    // ========================================================================
    // LOCK GRANTING LOGIC
    // ========================================================================
    
    bool CanGrantLock(const LockRequestQueue& queue, const LockRequest& request) {
        // Check all granted locks before this request
        for (const auto& req : queue.request_queue) {
            if (&req == &request) {
                break;  // Reached our request
            }
            
            if (req.status == LockRequestStatus::GRANTED) {
                // Check compatibility
                if (request.mode == LockMode::EXCLUSIVE || 
                    req.mode == LockMode::EXCLUSIVE) {
                    return false;  // Incompatible
                }
            }
        }
        
        // Check if there's a pending upgrade that should go first
        if (queue.upgrading && queue.upgrading_txn_id != request.txn_id) {
            if (request.mode == LockMode::EXCLUSIVE) {
                return false;
            }
        }
        
        return true;
    }
    
    bool CanGrantUpgrade(const LockRequestQueue& queue, txn_id_t txn_id) {
        // Can upgrade when we're the only one holding the lock
        int holders = 0;
        for (const auto& req : queue.request_queue) {
            if (req.status == LockRequestStatus::GRANTED) {
                holders++;
                if (req.txn_id != txn_id) {
                    return false;
                }
            }
        }
        return holders <= 1;
    }
    
    // ========================================================================
    // DEADLOCK DETECTION
    // ========================================================================
    
    void AddWaitForEdges(txn_id_t waiter, const LockRequestQueue& queue) {
        std::lock_guard<std::mutex> lock(graph_latch_);
        for (const auto& req : queue.request_queue) {
            if (req.status == LockRequestStatus::GRANTED) {
                wait_for_graph_[waiter].insert(req.txn_id);
            }
        }
    }
    
    void RemoveWaitForEdges(txn_id_t txn_id) {
        std::lock_guard<std::mutex> lock(graph_latch_);
        wait_for_graph_.erase(txn_id);
        
        // Remove edges pointing to this transaction
        for (auto& [_, waitees] : wait_for_graph_) {
            waitees.erase(txn_id);
        }
    }
    
    bool HasCycle(txn_id_t start, std::unordered_set<txn_id_t>& visited,
                  std::unordered_set<txn_id_t>& rec_stack) {
        visited.insert(start);
        rec_stack.insert(start);
        
        auto it = wait_for_graph_.find(start);
        if (it != wait_for_graph_.end()) {
            for (txn_id_t neighbor : it->second) {
                if (visited.find(neighbor) == visited.end()) {
                    if (HasCycle(neighbor, visited, rec_stack)) {
                        return true;
                    }
                } else if (rec_stack.find(neighbor) != rec_stack.end()) {
                    return true;
                }
            }
        }
        
        rec_stack.erase(start);
        return false;
    }
    
    txn_id_t DetectDeadlock() {
        std::lock_guard<std::mutex> lock(graph_latch_);
        
        std::unordered_set<txn_id_t> visited;
        std::unordered_set<txn_id_t> rec_stack;
        
        for (const auto& [txn_id, _] : wait_for_graph_) {
            if (visited.find(txn_id) == visited.end()) {
                if (HasCycle(txn_id, visited, rec_stack)) {
                    // Return youngest transaction in cycle (highest ID)
                    txn_id_t victim = txn_id;
                    for (txn_id_t t : rec_stack) {
                        if (t > victim) {
                            victim = t;
                        }
                    }
                    return victim;
                }
            }
        }
        
        return INVALID_TXN_ID;
    }
    
    void DeadlockDetectionLoop() {
        while (enable_deadlock_detection_) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(DEADLOCK_DETECTION_INTERVAL_MS));
            
            txn_id_t victim = DetectDeadlock();
            if (victim != INVALID_TXN_ID) {
                AbortTransaction(victim);
            }
        }
    }
    
    void AbortTransaction(txn_id_t txn_id) {
        // Mark all pending requests as aborted
        {
            std::lock_guard<std::mutex> lock(row_latch_);
            for (auto& [_, queue] : row_lock_map_) {
                for (auto& req : queue.request_queue) {
                    if (req.txn_id == txn_id && 
                        req.status == LockRequestStatus::WAITING) {
                        req.status = LockRequestStatus::ABORTED;
                        queue.cv.notify_all();
                    }
                }
            }
        }
    }
    
    // ========================================================================
    // DATA MEMBERS
    // ========================================================================
    
    std::mutex table_latch_;
    std::unordered_map<std::string, LockRequestQueue> table_lock_map_;
    std::unordered_map<txn_id_t, std::unordered_set<std::string>> txn_table_locks_;
    
    std::mutex row_latch_;
    std::unordered_map<RID, LockRequestQueue> row_lock_map_;
    std::unordered_map<txn_id_t, std::unordered_set<RID>> txn_row_locks_;
    
    std::mutex graph_latch_;
    std::unordered_map<txn_id_t, std::unordered_set<txn_id_t>> wait_for_graph_;
    
    std::atomic<bool> enable_deadlock_detection_;
    std::thread deadlock_detection_thread_;
};

} // namespace francodb

