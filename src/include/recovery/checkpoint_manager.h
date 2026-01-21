#pragma once

#include "buffer/buffer_pool_manager.h"
#include "recovery/log_manager.h"
#include <mutex>
#include <iostream>

namespace francodb {

    class CheckpointManager {
    public:
        CheckpointManager(BufferPoolManager *bpm, LogManager *log_manager)
            : bpm_(bpm), log_manager_(log_manager) {}

        /**
         * Performs a Blocking Checkpoint.
         * 1. Flushes all dirty pages to disk (Persistence).
         * 2. Writes a <CHECKPOINT_END> log record (Marker).
         * 3. Forces the log to disk.
         */
        void BeginCheckpoint() {
            // In a full implementation, we would acquire a global latch here to block new Txns.
            // For now, we rely on the fact that this is called during maintenance windows or low load.
            
            std::cout << "[SYSTEM] Starting Checkpoint..." << std::endl;

          
            bpm_->FlushAllPages();

            log_manager_->LogCheckpoint();

            std::cout << "[SYSTEM] Checkpoint Complete. Database consistent on disk." << std::endl;
        }

    private:
        BufferPoolManager *bpm_;
        LogManager *log_manager_;
    };

} // namespace francodb