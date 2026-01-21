#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <chrono> // [NEW] Required for time travel

#include "common/value.h"

namespace francodb {

    // Types of logs we write
    enum class LogRecordType {
        INVALID = 0,
        INSERT,
        MARK_DELETE,
        APPLY_DELETE,
        ROLLBACK_DELETE,
        UPDATE,
        BEGIN,
        COMMIT,
        ABORT,
        CHECKPOINT_BEGIN,
        CHECKPOINT_END
    };

    /**
     * Header format (Serialized):
     * [size_t size] [LSN] [TransID] [TIMESTAMP] [Type]
     */
    class LogRecord {
    public:
        using txn_id_t = int32_t;
        using lsn_t = int32_t; 
        using timestamp_t = uint64_t; // [NEW] Time Travel Type

        // --- CONSTRUCTORS ---
        
        // 1. Transaction Lifecycle Logs (BEGIN/COMMIT/ABORT)
        LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType log_type)
            : size_(0), lsn_(INVALID_LSN), prev_lsn_(prev_lsn), txn_id_(txn_id), log_record_type_(log_type) {
            // [NEW] Capture Timestamp
            timestamp_ = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }

        // 2. [MERGED FIX] Single-Value Log (Used for both INSERT and DELETE)
        LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType log_type, const std::string& table_name, const Value& val)
            : size_(0), lsn_(INVALID_LSN), prev_lsn_(prev_lsn), txn_id_(txn_id), log_record_type_(log_type),
              table_name_(table_name) {
            
            // [NEW] Capture Timestamp
            timestamp_ = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            if (log_type == LogRecordType::INSERT) {
                new_value_ = val; // Inserts create a NEW value (Redo info)
            } else {
                old_value_ = val; // Deletes destroy an OLD value (Undo info)
            }
        }

        // 3. Update Log (Needs: Key/RID, Old Value (Undo), New Value (Redo))
        LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType log_type, const std::string& table_name, const Value& old_val, const Value& new_val)
            : size_(0), lsn_(INVALID_LSN), prev_lsn_(prev_lsn), txn_id_(txn_id), log_record_type_(log_type),
              table_name_(table_name), old_value_(old_val), new_value_(new_val) {
            
            // [NEW] Capture Timestamp
            timestamp_ = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }

        ~LogRecord() = default;

        // --- PUBLIC ACCESSORS ---
        int32_t GetSize() const { return size_; }
        lsn_t GetLSN() const { return lsn_; }
        txn_id_t GetTxnId() const { return txn_id_; }
        LogRecordType GetLogRecordType() const { return log_record_type_; }
        timestamp_t GetTimestamp() const { return timestamp_; } // [NEW]
        
        std::string ToString() const {
            std::string type_str;
            switch (log_record_type_) {
                case LogRecordType::INSERT: type_str = "INSERT"; break;
                case LogRecordType::UPDATE: type_str = "UPDATE"; break;
                case LogRecordType::APPLY_DELETE: type_str = "DELETE"; break;
                case LogRecordType::BEGIN:  type_str = "BEGIN"; break;
                case LogRecordType::COMMIT: type_str = "COMMIT"; break;
                case LogRecordType::ABORT:  type_str = "ABORT"; break;
                default: type_str = "UNKNOWN";
            }
            return "LSN:" + std::to_string(lsn_) + " TXN:" + std::to_string(txn_id_) + " TIME:" + std::to_string(timestamp_) + " TYPE:" + type_str; 
        }

        static const int HEADER_SIZE = 28; // Increased for Timestamp (20 + 8)
        static const lsn_t INVALID_LSN = -1;

    public:
        // --- DATA FIELDS ---
        int32_t size_;        
        lsn_t lsn_;            
        lsn_t prev_lsn_;       
        txn_id_t txn_id_;
        timestamp_t timestamp_; // [NEW] Added Field
        LogRecordType log_record_type_;

        std::string table_name_;
        Value old_value_; // UNDO info (Used by Update & Delete)
        Value new_value_; // REDO info (Used by Insert & Update)
    };

} // namespace francodb