#pragma once

#include "recovery/log_manager.h"
#include "recovery/log_record.h"
#include "recovery/checkpoint_manager.h"
#include "catalog/catalog.h"
#include "buffer/buffer_pool_manager.h"
#include <map>
#include <set>
#include <filesystem>
#include <fstream>
#include <memory>
#include <functional>

namespace francodb {

    // Forward declaration
    class TableHeap;

    /**
     * Recovery Statistics for monitoring and debugging
     */
    struct RecoveryStats {
        uint64_t records_read = 0;
        uint64_t records_redone = 0;
        uint64_t records_undone = 0;
        uint64_t transactions_recovered = 0;
        uint64_t transactions_rolled_back = 0;
        std::chrono::milliseconds analysis_time{0};
        std::chrono::milliseconds redo_time{0};
        std::chrono::milliseconds undo_time{0};
        LogRecord::lsn_t start_lsn = LogRecord::INVALID_LSN;
        LogRecord::lsn_t end_lsn = LogRecord::INVALID_LSN;
    };

    /**
     * ARIES-Compliant Recovery Manager
     * 
     * The "Git for Data" Mental Model:
     * =================================
     * 
     * 1. COMMIT LOG (WAL): The Write-Ahead Log is like Git history.
     *    Every transaction commit is a "commit" with:
     *    - LSN (Log Sequence Number) = Git commit hash
     *    - Timestamp = Commit date
     *    - prev_lsn = Parent commit (for Undo chain traversal)
     * 
     * 2. CHECKOUT (SELECT ... AS OF): This is like `git checkout <hash> --detached`.
     *    It provides a read-only view of data at a specific timestamp WITHOUT
     *    modifying the live database. Implementation: Build a "Shadow Heap" by
     *    replaying log records up to that time.
     * 
     * 3. HARD RESET (RECOVER TO): This is like `git reset --hard <hash>`.
     *    It forces the live database back to a specific state, discarding newer history.
     *    Implementation:
     *    - Short Jump (Undo): If target is recent, walk the Undo chain backward
     *    - Long Jump (Redo): If target is far back, load checkpoint and Redo forward
     * 
     * ARIES Recovery Protocol:
     * ========================
     * 
     * Phase 1 - ANALYSIS:
     *   - Read master_record to find last checkpoint
     *   - Scan log from checkpoint to end
     *   - Build Active Transaction Table (ATT) - uncommitted transactions
     *   - Build Dirty Page Table (DPT) - pages that may need redo
     * 
     * Phase 2 - REDO (History Repeating):
     *   - Replay all operations from checkpoint forward
     *   - Ensures all committed transactions are applied
     *   - "The database is restored to its pre-crash state"
     * 
     * Phase 3 - UNDO (Loser Rollback):
     *   - Roll back all uncommitted transactions
     *   - Uses prev_lsn chain to walk backward
     *   - Writes CLR (Compensation Log Records) for crash safety
     */
    class RecoveryManager {
    public:
        /**
         * Constructor
         * 
         * @param log_manager Log manager for reading/writing logs
         * @param catalog Catalog for table access
         * @param bpm Buffer pool manager
         * @param checkpoint_mgr Checkpoint manager for checkpoint info
         */
        RecoveryManager(LogManager* log_manager, Catalog* catalog, 
                       BufferPoolManager* bpm, CheckpointManager* checkpoint_mgr);

        // ========================================================================
        // ARIES CRASH RECOVERY
        // ========================================================================

        /**
         * Full ARIES Crash Recovery Protocol
         * 
         * Call this on database startup to recover from any crash.
         * 
         * Steps:
         * 1. Analysis: Find last checkpoint, build ATT and DPT
         * 2. Redo: Replay history from checkpoint forward
         * 3. Undo: Roll back uncommitted transactions
         */
        void ARIES();

        /**
         * Recover a specific database from its log
         * Used when switching to a database or after CREATE DATABASE
         * 
         * @param db_name Name of the database to recover
         */
        void RecoverDatabase(const std::string& db_name);

        /**
         * Perform only the Redo phase
         * Useful for bringing a replica up to date
         */
        void RedoPhase();

        /**
         * Perform only the Undo phase
         * Useful for rolling back after a partial recovery
         */
        void UndoPhase();

        // ========================================================================
        // TIME TRAVEL - "GIT RESET --HARD" (RECOVER TO)
        // ========================================================================

        /**
         * Rollback database to a specific timestamp using Undo approach.
         * 
         * This is like `git reset --hard <hash>`:
         * - Walks the Undo chain backward
         * - Applies inverse operations
         * - Modifies the LIVE database
         * 
         * Best for: Recent timestamps (within the last few transactions)
         * 
         * @param target_time Target timestamp in microseconds since epoch
         */
        void RollbackToTime(uint64_t target_time);

        /**
         * Recover database to a specific timestamp.
         * 
         * Automatically chooses the best strategy:
         * - Short Jump (Undo): If target is recent, use Undo chain
         * - Long Jump (Redo): If target is far back, use checkpoint + Redo
         * 
         * @param target_time Target timestamp in microseconds since epoch
         */
        void RecoverToTime(uint64_t target_time);

        /**
         * Force recover to a specific LSN.
         * 
         * @param target_lsn Target LSN to recover to
         */
        void RecoverToLSN(LogRecord::lsn_t target_lsn);

        // ========================================================================
        // TIME TRAVEL - "GIT CHECKOUT --DETACHED" (SELECT AS OF)
        // ========================================================================

        /**
         * Build a read-only snapshot of a table at a specific time.
         * 
         * This is like `git checkout <hash> --detached`:
         * - Creates a TEMPORARY "Shadow Heap" in memory
         * - Replays log records up to target_time into the shadow heap
         * - Does NOT modify the live database
         * 
         * Used for: SELECT * FROM table AS OF TIMESTAMP '2025-01-01'
         * 
         * @param table_name Name of the table to snapshot
         * @param target_time Target timestamp in microseconds since epoch
         * @return New TableHeap containing the historical data (caller owns)
         */
        TableHeap* BuildTableSnapshot(const std::string& table_name, uint64_t target_time);

        /**
         * Replay log records into a specific heap.
         * 
         * @param target_heap The heap to populate
         * @param target_table_name Table name to filter records
         * @param target_time Stop replaying at this timestamp
         * @param db_name Database to read logs from (empty = use current)
         */
        void ReplayIntoHeap(TableHeap* target_heap, 
                           const std::string& target_table_name, 
                           uint64_t target_time,
                           const std::string& db_name = "");

        // ========================================================================
        // STATISTICS AND MONITORING
        // ========================================================================

        /**
         * Get recovery statistics from the last recovery operation
         */
        RecoveryStats GetLastRecoveryStats() const { return last_recovery_stats_; }

        /**
         * Get the Active Transaction Table
         * (Transactions that were in-flight during a crash)
         */
        std::map<LogRecord::txn_id_t, LogRecord::lsn_t> GetActiveTransactionTable() const { 
            return active_transaction_table_; 
        }

        /**
         * Get the Dirty Page Table
         */
        std::map<int32_t, LogRecord::lsn_t> GetDirtyPageTable() const { 
            return dirty_page_table_; 
        }

    private:
        // ========================================================================
        // ARIES PHASES
        // ========================================================================

        /**
         * Analysis Phase: Build ATT and DPT from log
         * 
         * @param start_lsn LSN to start analysis from (from checkpoint)
         * @return The minimum recovery LSN (oldest dirty page or active transaction)
         */
        LogRecord::lsn_t AnalysisPhase(LogRecord::lsn_t start_lsn);

        /**
         * Redo Phase: Replay history from a starting point
         * 
         * @param start_lsn LSN to start redo from
         * @param stop_at_time Optional: Stop at this timestamp (0 = no limit)
         */
        void RedoPhase(LogRecord::lsn_t start_lsn, uint64_t stop_at_time = 0);

        /**
         * Undo Phase: Roll back uncommitted transactions
         */
        void UndoPhase(const std::set<LogRecord::txn_id_t>& losers);

        // ========================================================================
        // LOG RECORD OPERATIONS
        // ========================================================================

        /**
         * Core recovery loop: replays log records from a specific database log
         * 
         * @param db_name Database to recover
         * @param stop_at_time Stop replaying at this timestamp (0 = replay all)
         * @param start_offset File offset to start reading from (0 = start of file)
         */
        void RunRecoveryLoop(const std::string& db_name, 
                            uint64_t stop_at_time, 
                            std::streampos start_offset);

        /**
         * Apply a single log record (Redo direction)
         * 
         * @param record The log record to apply
         */
        void RedoLogRecord(const LogRecord& record);

        /**
         * Undo a single log record (Undo direction)
         * 
         * @param record The log record to undo
         * @return The prev_lsn to continue the undo chain, or INVALID_LSN if done
         */
        LogRecord::lsn_t UndoLogRecord(const LogRecord& record);

        /**
         * Apply a log record (legacy method for compatibility)
         */
        void ApplyLogRecord(const LogRecord& record, bool is_undo = false);

        /**
         * Read a complete log record from a file stream
         * 
         * @param log_file Input file stream
         * @param record Output record
         * @return true if record was read successfully
         */
        bool ReadLogRecord(std::ifstream& log_file, LogRecord& record);

        /**
         * Scan log file to find a specific LSN
         * 
         * @param log_file Input file stream
         * @param target_lsn LSN to find
         * @return File offset of the record, or -1 if not found
         */
        std::streampos FindLSNOffset(std::ifstream& log_file, LogRecord::lsn_t target_lsn);

        // ========================================================================
        // HELPER METHODS
        // ========================================================================

        /**
         * Determine the best recovery strategy for a target time
         * 
         * @param target_time Target timestamp
         * @return true if Undo is preferred, false if Redo from checkpoint is preferred
         */
        bool ShouldUseUndoStrategy(uint64_t target_time);

        /**
         * Collect all log records for a specific database
         * 
         * @param db_name Database name
         * @return Vector of log records
         */
        std::vector<LogRecord> CollectLogRecords(const std::string& db_name);

        /**
         * Read helpers for deserialization
         */
        static std::string ReadString(std::ifstream& in);
        static Value ReadValue(std::ifstream& in);
        static int32_t ReadInt32(std::ifstream& in);
        static uint64_t ReadUInt64(std::ifstream& in);

        // ========================================================================
        // DATA MEMBERS
        // ========================================================================

        LogManager* log_manager_;
        Catalog* catalog_;             
        BufferPoolManager* bpm_;
        CheckpointManager* checkpoint_mgr_;

        // ARIES Tables (built during Analysis phase)
        std::map<LogRecord::txn_id_t, LogRecord::lsn_t> active_transaction_table_;  // ATT
        std::map<int32_t, LogRecord::lsn_t> dirty_page_table_;                       // DPT

        // Set of committed transactions (for Redo filtering)
        std::set<LogRecord::txn_id_t> committed_transactions_;

        // Statistics
        RecoveryStats last_recovery_stats_;
    };

} // namespace francodb

