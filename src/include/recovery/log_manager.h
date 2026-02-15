#pragma once

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <memory>
#include <thread>
#include "recovery/log_record.h"

namespace chronosdb {

    /**
     * Log Stream: Represents a single WAL file for a database
     * 
     * Each database has its own log stream to ensure isolation.
     * System log tracks DDL (CREATE/DROP DATABASE), while database
     * logs track DML (INSERT/UPDATE/DELETE).
     */
    struct LogStream {
        std::string db_name;
        std::string log_path;
        std::ofstream file;
        std::vector<char> buffer;
        bool is_open = false;
        LogRecord::lsn_t last_flushed_lsn = LogRecord::INVALID_LSN;
        std::streampos current_offset = 0;

        LogStream(const std::string& name, const std::string& path)
            : db_name(name), log_path(path) {}
        
        LogStream() = default;
    };

    /**
     * Transaction Log Entry for tracking per-transaction LSN chains
     * Used for building the Undo chain during recovery
     */
    struct TransactionLogEntry {
        LogRecord::txn_id_t txn_id;
        LogRecord::lsn_t first_lsn;     // First LSN of this transaction
        LogRecord::lsn_t last_lsn;      // Last LSN written (prev_lsn for next record)
        bool is_committed = false;
    };

    /**
     * Multi-File Write-Ahead Log Manager
     * 
     * Production-Grade Architecture:
     * =============================
     * - System Log: data/system/sys.log (DDL operations - CREATE/DROP DATABASE)
     * - Database Logs: data/<db_name>/wal.log (DML operations per database)
     * 
     * The "Git for Data" Mental Model:
     * ================================
     * - Each log record is a "commit" with LSN (hash), timestamp, and prev_lsn (parent)
     * - SwitchDatabase is like checking out a different repository
     * - The transaction manager tracks prev_lsn to build the Undo chain
     * 
     * Key Features:
     * =============
     * - Multi-stream support for database isolation
     * - Double buffering for write-ahead logging
     * - Background flush thread for durability
     * - Transaction LSN chain tracking for ARIES Undo
     */
    class LogManager {
    public:
        /**
         * Constructor - Initializes the log manager with base data directory
         * 
         * @param base_data_dir Base directory for all database data (default: "data")
         */
        explicit LogManager(const std::string& base_data_dir = "data");

        /**
         * Destructor - Ensures all buffers are flushed and files are closed
         */
        ~LogManager();

        // ========================================================================
        // CORE LOGGING API
        // ========================================================================

        /**
         * Append a log record to the active database log
         * 
         * @param log_record The record to append (modified with assigned LSN)
         * @return The assigned LSN, or INVALID_LSN on failure
         */
        LogRecord::lsn_t AppendLogRecord(LogRecord& log_record);

        /**
         * Force write CHECKPOINT record to the log
         * Used by CheckpointManager during checkpointing
         */
        void LogCheckpoint();

        /**
         * Log a CHECKPOINT record with Active Transaction Table and Dirty Page Table
         * This is the full ARIES checkpoint record
         */
        void LogCheckpointWithTables(const std::vector<ActiveTransactionEntry>& active_txns,
                                     const std::vector<DirtyPageEntry>& dirty_pages);

        /**
         * Flush the log buffer to disk
         * 
         * @param force If true, blocks until flush completes
         */
        void Flush(bool force = true);

        /**
         * Flush the log up to a specific LSN (for WAL protocol)
         * 
         * This is CRITICAL for data integrity. Before writing any data page
         * to disk, we MUST ensure all log records up to the page's LSN
         * are persisted. This method blocks until the specified LSN is
         * safely on disk.
         * 
         * @param target_lsn The minimum LSN that must be on disk
         */
        void FlushToLSN(LogRecord::lsn_t target_lsn);

        // ========================================================================
        // MULTI-DATABASE MANAGEMENT
        // ========================================================================

        /**
         * Switch the active database context
         * Flushes current log and opens the new database's log
         * 
         * This is like "git checkout <repository>" - we're now working
         * in a different database's commit history.
         * 
         * @param db_name Name of the database to switch to
         */
        void SwitchDatabase(const std::string& db_name);

        /**
         * Create a new database log file
         * Called during CREATE DATABASE
         * 
         * @param db_name Name of the new database
         */
        void CreateDatabaseLog(const std::string& db_name);

        /**
         * Delete a database log file
         * Called during DROP DATABASE
         * 
         * @param db_name Name of the database to drop
         */
        void DropDatabaseLog(const std::string& db_name);

        // ========================================================================
        // TRANSACTION TRACKING (for ARIES Undo Chain)
        // ========================================================================

        /**
         * Begin tracking a transaction
         * @param txn_id Transaction ID
         */
        void BeginTransaction(LogRecord::txn_id_t txn_id);

        /**
         * Mark a transaction as committed
         * @param txn_id Transaction ID
         */
        void CommitTransaction(LogRecord::txn_id_t txn_id);

        /**
         * Mark a transaction as aborted
         * @param txn_id Transaction ID
         */
        void AbortTransaction(LogRecord::txn_id_t txn_id);

        /**
         * Get the last LSN written by a transaction (for prev_lsn chain)
         * @param txn_id Transaction ID
         * @return Last LSN or INVALID_LSN if not found
         */
        LogRecord::lsn_t GetTransactionLastLSN(LogRecord::txn_id_t txn_id);

        /**
         * Get all active (uncommitted) transactions
         * Used during checkpoint and recovery
         */
        std::vector<ActiveTransactionEntry> GetActiveTransactions();

        // ========================================================================
        // ACCESSORS
        // ========================================================================

        /**
         * Get the highest LSN safely written to disk
         */
        LogRecord::lsn_t GetPersistentLSN() { return persistent_lsn_.load(); }

        /**
         * Get the next LSN that will be assigned
         */
        LogRecord::lsn_t GetNextLSN() { return next_lsn_.load(); }

        /**
         * Get the current active log file path
         */
        std::string GetLogFileName() const;

        /**
         * Get log file path for a specific database (main WAL)
         *
         * @param db_name Database name
         * @return Full path to the WAL file
         */
        std::string GetLogFilePath(const std::string& db_name) const;

        /**
         * Get per-table log file path for fast time travel
         * Each table has its own WAL file: data/<db>/wal/<table>.wal
         *
         * @param db_name Database name
         * @param table_name Table name
         * @return Full path to the per-table WAL file
         */
        std::string GetTableLogFilePath(const std::string& db_name, const std::string& table_name) const;

        /**
         * Check if a per-table WAL file exists for fast time travel
         */
        bool HasTableLog(const std::string& db_name, const std::string& table_name) const;

        /**
         * Get current database context
         */
        std::string GetCurrentDatabase() const { 
            std::lock_guard<std::mutex> lock(latch_);
            return current_db_; 
        }

        /**
         * Get the base data directory
         */
        std::string GetBaseDataDir() const { return base_data_dir_; }

        /**
         * Get current file offset for the active log
         */
        std::streampos GetCurrentOffset() const;

        /**
         * Stop the background flush thread
         * Called during shutdown
         */
        void StopFlushThread();

        /**
         * Check if log manager is running
         */
        bool IsRunning() const { return !stop_flush_thread_.load(); }
        
        /**
         * Set the checkpoint manager for operation-based checkpoint triggering.
         * Called after both LogManager and CheckpointManager are created.
         */
        void SetCheckpointManager(class CheckpointManager* checkpoint_mgr) {
            checkpoint_mgr_ = checkpoint_mgr;
        }

    private:
        // ========================================================================
        // INTERNAL METHODS
        // ========================================================================

        /**
         * Background worker loop for flushing log buffers
         */
        void FlushThread();

        /**
         * Swap the write and flush buffers
         */
        void SwapBuffers();

        /**
         * Open a log file for a specific database
         */
        void OpenLogFile(const std::string& db_name);

        /**
         * Close the current log file
         */
        void CloseCurrentLog();

        /**
         * Write serialized record to per-table WAL file (dual-write for fast time travel)
         * Called under latch_ for DML records with a table_name
         */
        void WriteToTableLog(const std::string& db_name, const std::string& table_name,
                            const std::vector<char>& record_buf);

        /**
         * Write helpers for serialization
         */
        static void WriteString(std::vector<char>& buffer, const std::string& str);
        static void WriteValue(std::vector<char>& buffer, const Value& val);
        static void WriteInt32(std::vector<char>& buffer, int32_t val);
        static void WriteInt64(std::vector<char>& buffer, int64_t val);
        static void WriteUInt64(std::vector<char>& buffer, uint64_t val);

        // ========================================================================
        // DATA MEMBERS
        // ========================================================================

        // LSN Management
        std::atomic<LogRecord::lsn_t> next_lsn_;        // Next LSN to assign
        std::atomic<LogRecord::lsn_t> persistent_lsn_;  // Last LSN safely on disk

        // Double Buffering (Write to one, Flush the other)
        std::vector<char> log_buffer_;       // Active write buffer
        std::vector<char> flush_buffer_;     // Buffer being flushed to disk

        // Thread Safety - Separate locks for different concerns (SOLID: Single Responsibility)
        mutable std::mutex latch_;           // Protects log_buffer_ and in-memory state
        mutable std::mutex write_latch_;     // Protects disk write ordering (prevents race condition)
        std::condition_variable cv_;         // Wakes up flush thread
        
        // LSN ordering tracking for write serialization
        std::atomic<LogRecord::lsn_t> last_written_lsn_{LogRecord::INVALID_LSN};  // Last LSN written to disk
        LogRecord::lsn_t buffer_start_lsn_{LogRecord::INVALID_LSN};               // First LSN in current buffer
        LogRecord::lsn_t buffer_end_lsn_{LogRecord::INVALID_LSN};                 // Last LSN in current buffer

        // Multi-Database Support
        std::string base_data_dir_;          // Base directory (e.g., "data")
        std::string current_db_;             // Currently active database
        std::ofstream log_file_;             // Current active log file stream
        std::streampos current_file_offset_; // Track file position

        // Log Stream Map (optional: for keeping multiple streams open)
        std::unordered_map<std::string, std::unique_ptr<LogStream>> log_streams_;

        // Per-Table WAL Files (dual-write for fast time travel)
        // Key: "db_name/table_name", Value: open ofstream for per-table WAL
        std::unordered_map<std::string, std::ofstream> table_log_files_;

        // Transaction Tracking (for ARIES Undo Chain)
        std::unordered_map<LogRecord::txn_id_t, TransactionLogEntry> active_transactions_;
        std::mutex txn_latch_;               // Protects transaction map

        // Background Flush Thread
        std::thread flush_thread_;
        std::atomic<bool> stop_flush_thread_;
        
        // Operation-based checkpoint triggering
        class CheckpointManager* checkpoint_mgr_{nullptr};
    };

} // namespace chronosdb

