#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <chrono>
#include <unordered_set>
#include <unordered_map>

#include "common/value.h"

namespace francodb {

    /**
     * ARIES-Compliant Log Record Types
     * 
     * Types are categorized as:
     * - Transaction Lifecycle: BEGIN, COMMIT, ABORT
     * - Data Modification: INSERT, UPDATE, DELETE variants
     * - DDL Operations: CREATE_DB, DROP_DB, CREATE_TABLE, DROP_TABLE
     * - Checkpointing: CHECKPOINT_BEGIN, CHECKPOINT_END
     * - Context Management: SWITCH_DB
     * - Compensation: CLR (Compensation Log Record) for Undo operations
     */
    enum class LogRecordType {
        INVALID = 0,
        
        // Transaction Lifecycle
        BEGIN,
        COMMIT,
        ABORT,
        
        // Data Modification (DML)
        INSERT,
        UPDATE,
        MARK_DELETE,
        APPLY_DELETE,
        ROLLBACK_DELETE,
        
        // DDL Operations
        CREATE_DB,
        DROP_DB,
        CREATE_TABLE,
        DROP_TABLE,
        SWITCH_DB,
        
        // Checkpointing (ARIES)
        CHECKPOINT_BEGIN,
        CHECKPOINT_END,
        
        // Compensation Log Record (for Undo operations)
        CLR
    };

    /**
     * Convert LogRecordType to string for debugging
     */
    inline std::string LogRecordTypeToString(LogRecordType type) {
        switch (type) {
            case LogRecordType::INVALID: return "INVALID";
            case LogRecordType::BEGIN: return "BEGIN";
            case LogRecordType::COMMIT: return "COMMIT";
            case LogRecordType::ABORT: return "ABORT";
            case LogRecordType::INSERT: return "INSERT";
            case LogRecordType::UPDATE: return "UPDATE";
            case LogRecordType::MARK_DELETE: return "MARK_DELETE";
            case LogRecordType::APPLY_DELETE: return "APPLY_DELETE";
            case LogRecordType::ROLLBACK_DELETE: return "ROLLBACK_DELETE";
            case LogRecordType::CREATE_DB: return "CREATE_DB";
            case LogRecordType::DROP_DB: return "DROP_DB";
            case LogRecordType::CREATE_TABLE: return "CREATE_TABLE";
            case LogRecordType::DROP_TABLE: return "DROP_TABLE";
            case LogRecordType::SWITCH_DB: return "SWITCH_DB";
            case LogRecordType::CHECKPOINT_BEGIN: return "CHECKPOINT_BEGIN";
            case LogRecordType::CHECKPOINT_END: return "CHECKPOINT_END";
            case LogRecordType::CLR: return "CLR";
            default: return "UNKNOWN";
        }
    }

    /**
     * Active Transaction Entry for Checkpoint
     * Stores information about uncommitted transactions at checkpoint time
     */
    struct ActiveTransactionEntry {
        int32_t txn_id;
        int32_t last_lsn;      // Last LSN written by this transaction
        int32_t first_lsn;     // First LSN of this transaction (for undo chain)
    };

    /**
     * Dirty Page Entry for Checkpoint
     * Stores information about dirty pages in buffer pool at checkpoint time
     */
    struct DirtyPageEntry {
        int32_t page_id;
        int32_t recovery_lsn;  // First LSN that made this page dirty
    };

    /**
     * ARIES-Compliant Log Record
     * 
     * Header Format (Serialized):
     * [size:4][lsn:4][prev_lsn:4][txn_id:4][timestamp:8][type:4][db_name_len:4][db_name:var]
     * 
     * The "Git for Data" Mental Model:
     * - LSN = Commit Hash
     * - prev_lsn = Parent Commit (for Undo Chain traversal)
     * - timestamp = Commit Date (for Point-in-Time Recovery)
     * 
     * This structure enables:
     * - Forward traversal (Redo): Read LSN 0 -> N
     * - Backward traversal (Undo): Follow prev_lsn chain
     * - Time Travel: Filter by timestamp
     */
    class LogRecord {
    public:
        using txn_id_t = int32_t;
        using lsn_t = int32_t;
        using timestamp_t = uint64_t;

        // --- CONSTRUCTORS ---
        
        /**
         * 1. Transaction Lifecycle Logs (BEGIN/COMMIT/ABORT)
         */
        LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType log_type)
            : size_(0), lsn_(INVALID_LSN), prev_lsn_(prev_lsn), undo_next_lsn_(INVALID_LSN),
              txn_id_(txn_id), log_record_type_(log_type), db_name_("") {
            timestamp_ = GetCurrentTimestamp();
        }

        /**
         * 2. Single-Value Log (INSERT / DELETE)
         * For INSERT: val is stored as new_value_ (Redo info)
         * For DELETE: val is stored as old_value_ (Undo info)
         */
        LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType log_type, 
                  const std::string& table_name, const Value& val)
            : size_(0), lsn_(INVALID_LSN), prev_lsn_(prev_lsn), undo_next_lsn_(INVALID_LSN),
              txn_id_(txn_id), log_record_type_(log_type),
              db_name_(""), table_name_(table_name) {
            
            timestamp_ = GetCurrentTimestamp();

            if (log_type == LogRecordType::INSERT) {
                new_value_ = val;  // Inserts create a NEW value (Redo info)
            } else {
                old_value_ = val;  // Deletes destroy an OLD value (Undo info)
            }
        }

        /**
         * 3. Update Log (Needs: Old Value for Undo, New Value for Redo)
         */
        LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType log_type, 
                  const std::string& table_name, const Value& old_val, const Value& new_val)
            : size_(0), lsn_(INVALID_LSN), prev_lsn_(prev_lsn), undo_next_lsn_(INVALID_LSN),
              txn_id_(txn_id), log_record_type_(log_type),
              db_name_(""), table_name_(table_name), old_value_(old_val), new_value_(new_val) {
            
            timestamp_ = GetCurrentTimestamp();
        }

        /**
         * 4. DDL Constructor (CREATE_DB, DROP_DB, SWITCH_DB, CREATE_TABLE, DROP_TABLE)
         */
        LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType log_type, 
                  const std::string& db_or_table_name)
            : size_(0), lsn_(INVALID_LSN), prev_lsn_(prev_lsn), undo_next_lsn_(INVALID_LSN),
              txn_id_(txn_id), log_record_type_(log_type),
              db_name_(db_or_table_name), table_name_("") {
            
            // For table operations, the name goes in table_name_
            if (log_type == LogRecordType::CREATE_TABLE || 
                log_type == LogRecordType::DROP_TABLE) {
                table_name_ = db_or_table_name;
                db_name_ = "";
            }
            
            timestamp_ = GetCurrentTimestamp();
        }

        /**
         * 5. Checkpoint Constructor (CHECKPOINT_BEGIN / CHECKPOINT_END)
         * Stores Active Transaction Table (ATT) and Dirty Page Table (DPT)
         */
        LogRecord(LogRecordType log_type,
                  const std::vector<ActiveTransactionEntry>& active_txns,
                  const std::vector<DirtyPageEntry>& dirty_pages)
            : size_(0), lsn_(INVALID_LSN), prev_lsn_(INVALID_LSN), undo_next_lsn_(INVALID_LSN),
              txn_id_(0), log_record_type_(log_type),
              db_name_("system"), active_transactions_(active_txns), dirty_pages_(dirty_pages) {
            
            timestamp_ = GetCurrentTimestamp();
        }

        /**
         * 6. CLR Constructor (Compensation Log Record)
         * Used during Undo to log the compensation action
         * undo_next_lsn points to the next record to undo in the chain
         */
        LogRecord(txn_id_t txn_id, lsn_t prev_lsn, lsn_t undo_next_lsn,
                  const std::string& table_name, const Value& compensation_value)
            : size_(0), lsn_(INVALID_LSN), prev_lsn_(prev_lsn), undo_next_lsn_(undo_next_lsn),
              txn_id_(txn_id), log_record_type_(LogRecordType::CLR),
              db_name_(""), table_name_(table_name), new_value_(compensation_value) {
            
            timestamp_ = GetCurrentTimestamp();
        }

        ~LogRecord() = default;

        // --- PUBLIC ACCESSORS ---
        int32_t GetSize() const { return size_; }
        lsn_t GetLSN() const { return lsn_; }
        lsn_t GetPrevLSN() const { return prev_lsn_; }
        lsn_t GetUndoNextLSN() const { return undo_next_lsn_; }
        txn_id_t GetTxnId() const { return txn_id_; }
        LogRecordType GetLogRecordType() const { return log_record_type_; }
        timestamp_t GetTimestamp() const { return timestamp_; }
        std::string GetDbName() const { return db_name_; }
        std::string GetTableName() const { return table_name_; }
        Value GetOldValue() const { return old_value_; }
        Value GetNewValue() const { return new_value_; }
        
        // Checkpoint data accessors
        const std::vector<ActiveTransactionEntry>& GetActiveTransactions() const { 
            return active_transactions_; 
        }
        const std::vector<DirtyPageEntry>& GetDirtyPages() const { 
            return dirty_pages_; 
        }

        // --- SETTERS (for deserialization) ---
        void SetDbName(const std::string& db_name) { db_name_ = db_name; }
        void SetTableName(const std::string& table_name) { table_name_ = table_name; }
        void SetOldValue(const Value& val) { old_value_ = val; }
        void SetNewValue(const Value& val) { new_value_ = val; }
        void SetActiveTransactions(const std::vector<ActiveTransactionEntry>& txns) { 
            active_transactions_ = txns; 
        }
        void SetDirtyPages(const std::vector<DirtyPageEntry>& pages) { 
            dirty_pages_ = pages; 
        }
        
        /**
         * Get current timestamp in microseconds since epoch
         */
        static timestamp_t GetCurrentTimestamp() {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }

        /**
         * Debug string representation
         */
        std::string ToString() const {
            return "LSN:" + std::to_string(lsn_) + 
                   " PrevLSN:" + std::to_string(prev_lsn_) +
                   " TXN:" + std::to_string(txn_id_) + 
                   " TIME:" + std::to_string(timestamp_) + 
                   " TYPE:" + LogRecordTypeToString(log_record_type_) +
                   " DB:" + db_name_ +
                   " TABLE:" + table_name_;
        }

        /**
         * Check if this record type modifies data (used for Redo/Undo decisions)
         */
        bool IsDataModification() const {
            return log_record_type_ == LogRecordType::INSERT ||
                   log_record_type_ == LogRecordType::UPDATE ||
                   log_record_type_ == LogRecordType::MARK_DELETE ||
                   log_record_type_ == LogRecordType::APPLY_DELETE ||
                   log_record_type_ == LogRecordType::ROLLBACK_DELETE;
        }

        /**
         * Check if this is a checkpoint record
         */
        bool IsCheckpoint() const {
            return log_record_type_ == LogRecordType::CHECKPOINT_BEGIN ||
                   log_record_type_ == LogRecordType::CHECKPOINT_END;
        }

        // Header size (fixed portion)
        // [size:4][lsn:4][prev_lsn:4][undo_next_lsn:4][txn_id:4][timestamp:8][type:4] = 32 bytes
        static const int HEADER_SIZE = 32;
        static const lsn_t INVALID_LSN = -1;

    public:
        // --- DATA FIELDS (public for serialization access) ---
        int32_t size_;              // Total serialized size
        lsn_t lsn_;                 // Log Sequence Number (like Git commit hash)
        lsn_t prev_lsn_;            // Previous LSN for this transaction (Undo chain)
        lsn_t undo_next_lsn_;       // For CLR: next record to undo
        txn_id_t txn_id_;           // Transaction ID
        timestamp_t timestamp_;     // Timestamp for Time Travel/PITR
        LogRecordType log_record_type_;

        // Multi-database context
        std::string db_name_;       // Database this record belongs to
        std::string table_name_;    // Table name for DML operations
        
        // Data for Undo/Redo
        Value old_value_;           // UNDO info (original state)
        Value new_value_;           // REDO info (new state)

        // Checkpoint metadata (ARIES ATT & DPT)
        std::vector<ActiveTransactionEntry> active_transactions_;
        std::vector<DirtyPageEntry> dirty_pages_;
    };

} // namespace francodb







