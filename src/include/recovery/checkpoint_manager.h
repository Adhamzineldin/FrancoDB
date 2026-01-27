#pragma once

#include "storage/storage_interface.h"  // For IBufferManager
#include "recovery/log_manager.h"
#include <mutex>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace chronosdb {

    // Forward declaration
    class Catalog;

    /**
     * Master Record Structure
     * 
     * Persisted to disk at data/system/master_record
     * Contains information needed to restart recovery from the correct point.
     */
    struct MasterRecord {
        LogRecord::lsn_t checkpoint_lsn;     // LSN of the last CHECKPOINT_END record
        std::streampos checkpoint_offset;     // File offset of the checkpoint record
        LogRecord::timestamp_t timestamp;     // Timestamp of the checkpoint
        uint32_t version;                     // Version for forward compatibility
        
        static const uint32_t CURRENT_VERSION = 1;
        
        MasterRecord() 
            : checkpoint_lsn(LogRecord::INVALID_LSN), 
              checkpoint_offset(0),
              timestamp(0),
              version(CURRENT_VERSION) {}
    };

    /**
     * ARIES-Compliant Checkpoint Manager
     * 
     * The "Git Tag" Analogy:
     * ======================
     * A checkpoint is like creating a Git tag at a known-good state.
     * When recovering from a crash, we can start from the last tag (checkpoint)
     * rather than replaying the entire history from the beginning.
     * 
     * Implementation:
     * ===============
     * 1. Writes CHECKPOINT_BEGIN log record
     * 2. Captures Active Transaction Table (ATT) - uncommitted transactions
     * 3. Captures Dirty Page Table (DPT) - modified pages in buffer pool
     * 4. Flushes all dirty pages to disk
     * 5. Writes CHECKPOINT_END log record with ATT and DPT
     * 6. Forces the log to disk
     * 7. Updates master_record file with checkpoint LSN
     * 
     * Background Checkpointing:
     * =========================
     * The checkpoint manager can run a background thread that periodically
     * takes checkpoints. This ensures recovery time stays bounded even under
     * heavy write workloads.
     */
    class CheckpointManager {
    public:
        /**
         * Constructor
         * 
         * @param bpm Buffer pool manager for flushing dirty pages
         * @param log_manager Log manager for writing checkpoint records
         * @param master_record_path Path to the master record file
         */
        CheckpointManager(IBufferManager* bpm, LogManager* log_manager,
                          const std::string& master_record_path = "data/system/master_record");
        
        /**
         * Destructor - stops background checkpointing if running
         */
        ~CheckpointManager();

        // ========================================================================
        // CORE CHECKPOINTING API
        // ========================================================================

        /**
         * Performs a Blocking (Fuzzy) Checkpoint.
         * 
         * Steps:
         * 1. Writes CHECKPOINT_BEGIN record
         * 2. Captures ATT and DPT
         * 3. Flushes all dirty pages to disk (Persistence)
         * 4. Writes CHECKPOINT_END record with ATT/DPT (Marker)
         * 5. Forces the log to disk
         * 6. Updates master_record file with checkpoint LSN
         * 
         * Note: This blocks new transactions briefly during the snapshot phase.
         */
        void BeginCheckpoint();

        /**
         * Performs a Non-Blocking (Fuzzy) Checkpoint.
         * 
         * Similar to BeginCheckpoint but doesn't block new transactions.
         * Uses copy-on-write semantics for the ATT/DPT snapshot.
         * 
         * Note: This is the production-preferred method.
         */
        void FuzzyCheckpoint();

        // ========================================================================
        // RECOVERY API
        // ========================================================================

        /**
         * Read the last checkpoint information from master_record file.
         * Returns the checkpoint LSN, or INVALID_LSN if no checkpoint exists.
         */
        LogRecord::lsn_t GetLastCheckpointLSN();

        /**
         * Get the complete master record
         */
        MasterRecord GetMasterRecord();

        /**
         * Get the file offset corresponding to the last checkpoint.
         * This is used to seek to the checkpoint position in the log file.
         */
        std::streampos GetCheckpointOffset() { 
            std::lock_guard<std::mutex> lock(checkpoint_mutex_);
            return checkpoint_offset_; 
        }

        /**
         * Get the timestamp of the last checkpoint
         */
        LogRecord::timestamp_t GetLastCheckpointTimestamp() {
            std::lock_guard<std::mutex> lock(checkpoint_mutex_);
            return last_checkpoint_timestamp_;
        }

        // ========================================================================
        // BACKGROUND CHECKPOINTING
        // ========================================================================

        /**
         * Start background checkpointing thread
         * 
         * @param interval_seconds How often to take checkpoints (in seconds)
         */
        void StartBackgroundCheckpointing(uint32_t interval_seconds = 300);

        /**
         * Stop background checkpointing thread
         */
        void StopBackgroundCheckpointing();

        /**
         * Check if background checkpointing is enabled
         */
        bool IsBackgroundCheckpointingEnabled() const { 
            return background_checkpointing_enabled_.load(); 
        }

        // ========================================================================
        // CONFIGURATION
        // ========================================================================

        /**
         * Set the checkpoint interval (for background checkpointing)
         * 
         * @param seconds Interval in seconds between checkpoints
         */
        void SetCheckpointInterval(uint32_t seconds) {
            checkpoint_interval_seconds_ = seconds;
        }

        /**
         * Get the current checkpoint interval
         */
        uint32_t GetCheckpointInterval() const { 
            return checkpoint_interval_seconds_; 
        }
        
        /**
         * Set operation-based checkpoint threshold.
         * A checkpoint will be triggered after this many log operations.
         * Set to 0 to disable operation-based checkpoints.
         * 
         * @param ops_threshold Number of operations before auto-checkpoint
         */
        void SetOperationThreshold(uint32_t ops_threshold) {
            ops_checkpoint_threshold_ = ops_threshold;
        }
        
        /**
         * Get the operation threshold for checkpoints
         */
        uint32_t GetOperationThreshold() const {
            return ops_checkpoint_threshold_;
        }
        
        /**
         * Called by LogManager after each operation to track operation count.
         * Triggers checkpoint if threshold is exceeded.
         */
        void OnLogOperation() {
            if (ops_checkpoint_threshold_ == 0) return;
            
            uint32_t count = ++ops_since_checkpoint_;
            if (count >= ops_checkpoint_threshold_) {
                ops_since_checkpoint_ = 0;
                // Trigger async checkpoint (don't block the writer)
                if (background_checkpointing_enabled_.load()) {
                    background_cv_.notify_one();
                }
            }
        }

        /**
         * Get the number of checkpoints taken since startup
         */
        uint64_t GetCheckpointCount() const { 
            return checkpoint_count_.load(); 
        }

    private:
        // ========================================================================
        // INTERNAL METHODS
        // ========================================================================

        /**
         * Write the checkpoint information to the master_record file.
         * This is atomic (write to temp file, then rename).
         */
        void WriteMasterRecord(LogRecord::lsn_t checkpoint_lsn, 
                              std::streampos offset,
                              LogRecord::timestamp_t timestamp);

        /**
         * Read the master record from disk
         */
        bool ReadMasterRecord(MasterRecord& record);

        /**
         * Background checkpointing thread loop
         */
        void BackgroundCheckpointThread();

        /**
         * Collect dirty page information from buffer pool
         * (Would need BPM to expose this - placeholder for now)
         */
        std::vector<DirtyPageEntry> CollectDirtyPages();

        // ========================================================================
        // DATA MEMBERS
        // ========================================================================

        IBufferManager* bpm_;
        LogManager* log_manager_;
        Catalog* catalog_;  // For updating table checkpoint LSNs
        std::string master_record_path_;
        
        // Thread safety
        std::mutex checkpoint_mutex_;
        
        // Checkpoint state
        std::streampos checkpoint_offset_;
        LogRecord::timestamp_t last_checkpoint_timestamp_;
        std::atomic<uint64_t> checkpoint_count_;
        
        // Background checkpointing
        std::thread background_thread_;
        std::atomic<bool> background_checkpointing_enabled_;
        std::atomic<bool> stop_background_thread_;
        std::condition_variable background_cv_;
        std::mutex background_mutex_;
        uint32_t checkpoint_interval_seconds_;
        
        // Operation-based checkpointing (Bug #6 optimization)
        std::atomic<uint32_t> ops_since_checkpoint_{0};
        uint32_t ops_checkpoint_threshold_{1000};  // Default: checkpoint every 1k ops
        
    public:
        /**
         * Set the catalog for updating table checkpoint LSNs
         */
        void SetCatalog(Catalog* catalog) { catalog_ = catalog; }
    };

} // namespace chronosdb

