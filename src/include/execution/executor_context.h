#pragma once

#include "catalog/catalog.h"
#include "buffer/buffer_pool_manager.h"
#include "concurrency/transaction.h"
#include "recovery/log_manager.h" // [ACID] Include LogManager

namespace francodb {

    /**
     * ExecutorContext holds global state (Catalog, BPM, Transaction, LogManager) that all executors need.
     */
    class ExecutorContext {
    public:
        // [ACID] Updated Constructor to accept LogManager
        ExecutorContext(BufferPoolManager *bpm, Catalog *catalog, Transaction *txn, LogManager *log_manager)
            : bpm_(bpm), catalog_(catalog), transaction_(txn), log_manager_(log_manager) {}

        Catalog *GetCatalog() { return catalog_; }
        BufferPoolManager *GetBufferPoolManager() { return bpm_; }
        Transaction *GetTransaction() { return transaction_; }
        
        // [ACID] Getter
        LogManager *GetLogManager() { return log_manager_; }
        
        // Allow updating catalog/bpm when switching databases
        void SetCatalog(Catalog *catalog) { catalog_ = catalog; }
        void SetBufferPoolManager(BufferPoolManager *bpm) { bpm_ = bpm; }

    private:
        BufferPoolManager *bpm_;
        Catalog *catalog_;
        Transaction *transaction_;
        LogManager *log_manager_; // [ACID]
    };

} // namespace francodb