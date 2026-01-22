#pragma once

#include "catalog/catalog.h"
#include "buffer/buffer_pool_manager.h"
#include "concurrency/transaction.h"
#include "concurrency/lock_manager.h"
#include "recovery/log_manager.h"

namespace francodb {

    /**
     * ExecutorContext holds global state that all executors need.
     * 
     * CONCURRENCY FIX:
     * Added LockManager for proper row-level locking during DML operations.
     * This fixes the "Bank Problem" data corruption issue.
     */
    class ExecutorContext {
    public:
        ExecutorContext(BufferPoolManager *bpm, Catalog *catalog, Transaction *txn, 
                        LogManager *log_manager, LockManager *lock_manager = nullptr)
            : bpm_(bpm), catalog_(catalog), transaction_(txn), 
              log_manager_(log_manager), lock_manager_(lock_manager) {}

        Catalog *GetCatalog() { return catalog_; }
        BufferPoolManager *GetBufferPoolManager() { return bpm_; }
        Transaction *GetTransaction() { return transaction_; }
        LogManager *GetLogManager() { return log_manager_; }
        LockManager *GetLockManager() { return lock_manager_; }
        
        // Allow updating when switching databases
        void SetCatalog(Catalog *catalog) { catalog_ = catalog; }
        void SetBufferPoolManager(BufferPoolManager *bpm) { bpm_ = bpm; }
        void SetLockManager(LockManager *lock_manager) { lock_manager_ = lock_manager; }

    private:
        BufferPoolManager *bpm_;
        Catalog *catalog_;
        Transaction *transaction_;
        LogManager *log_manager_;
        LockManager *lock_manager_;
    };

} // namespace francodb