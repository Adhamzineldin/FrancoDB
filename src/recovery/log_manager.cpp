#include "recovery/log_manager.h"
#include <cstring>
#include <iostream>
#include <chrono>

namespace francodb {
    
    // Helper: Write a standard string to buffer [4-byte Len][Data]
    void WriteString(std::vector<char> &buffer, const std::string &str) {
        uint32_t len = static_cast<uint32_t>(str.length());
        const char *len_ptr = reinterpret_cast<const char *>(&len);
        buffer.insert(buffer.end(), len_ptr, len_ptr + sizeof(uint32_t));
        buffer.insert(buffer.end(), str.begin(), str.end());
    }

    // Helper: Write a Value object to buffer
    void WriteValue(std::vector<char> &buffer, const Value &val) {
        try {
            // 1. Write Type
            int type_id = static_cast<int>(val.GetTypeId());
            const char *type_ptr = reinterpret_cast<const char *>(&type_id);
            buffer.insert(buffer.end(), type_ptr, type_ptr + sizeof(int));

            // 2. Write Data based on type
            std::string s_val;
            try {
                s_val = val.ToString();
            } catch (...) {
                s_val = "";
            }
            WriteString(buffer, s_val);
        } catch (...) {
            int type_id = 0;
            const char *type_ptr = reinterpret_cast<const char *>(&type_id);
            buffer.insert(buffer.end(), type_ptr, type_ptr + sizeof(int));
            WriteString(buffer, "");
        }
    }

    /*
     * Main API: The Transaction Manager calls this.
     */
    LogRecord::lsn_t LogManager::AppendLogRecord(LogRecord &log_record) {
        try {
            std::unique_lock<std::mutex> lock(latch_);

            // 1. Assign Unique LSN
            log_record.lsn_ = next_lsn_++;

            // 2. Calculate offsets for Header
            std::vector<char> record_buf;

            // --- HEADER ---
            // Placeholder for Size (we fill this at the end)
            int32_t size_placeholder = 0;
            const char *ptr = reinterpret_cast<const char *>(&size_placeholder);
            record_buf.insert(record_buf.end(), ptr, ptr + sizeof(int32_t));

            // LSN
            ptr = reinterpret_cast<const char *>(&log_record.lsn_);
            record_buf.insert(record_buf.end(), ptr, ptr + sizeof(LogRecord::lsn_t));

            // Prev LSN
            ptr = reinterpret_cast<const char *>(&log_record.prev_lsn_);
            record_buf.insert(record_buf.end(), ptr, ptr + sizeof(LogRecord::lsn_t));

            // Txn ID
            ptr = reinterpret_cast<const char *>(&log_record.txn_id_);
            record_buf.insert(record_buf.end(), ptr, ptr + sizeof(LogRecord::txn_id_t));

            // [NEW] Timestamp (8 Bytes) - Insert AFTER TxnID
            ptr = reinterpret_cast<const char *>(&log_record.timestamp_);
            record_buf.insert(record_buf.end(), ptr, ptr + sizeof(LogRecord::timestamp_t));

            // Log Type
            int type_int = static_cast<int>(log_record.log_record_type_);
            ptr = reinterpret_cast<const char *>(&type_int);
            record_buf.insert(record_buf.end(), ptr, ptr + sizeof(int));

            // --- BODY ---
            if (log_record.log_record_type_ == LogRecordType::INSERT) {
                WriteString(record_buf, log_record.table_name_);
                WriteValue(record_buf, log_record.new_value_);
            }
            else if (log_record.log_record_type_ == LogRecordType::UPDATE) {
                WriteString(record_buf, log_record.table_name_);
                WriteValue(record_buf, log_record.old_value_);
                WriteValue(record_buf, log_record.new_value_);
            }
            else if (log_record.log_record_type_ == LogRecordType::APPLY_DELETE ||
                     log_record.log_record_type_ == LogRecordType::MARK_DELETE) {
                WriteString(record_buf, log_record.table_name_);
                WriteValue(record_buf, log_record.old_value_);
            }

            // 3. Patch the Size in Header
            int32_t final_size = static_cast<int32_t>(record_buf.size());
            std::memcpy(record_buf.data(), &final_size, sizeof(int32_t));
            log_record.size_ = final_size;

            // 4. Append to Global Log Buffer
            log_buffer_.insert(log_buffer_.end(), record_buf.begin(), record_buf.end());

            return log_record.lsn_;
        } catch (const std::exception &e) {
            std::cerr << "[LogManager] AppendLogRecord failed: " << e.what() << std::endl;
            return LogRecord::INVALID_LSN;
        } catch (...) {
            std::cerr << "[LogManager] AppendLogRecord failed with unknown error" << std::endl;
            return LogRecord::INVALID_LSN;
        }
    }
    
    // Force a specific log record for checkpoints
    void LogManager::LogCheckpoint() {
        LogRecord rec(0, 0, LogRecordType::CHECKPOINT_END);
        AppendLogRecord(rec);
        Flush(true);
    }

    void LogManager::SwapBuffers() {
        std::swap(log_buffer_, flush_buffer_);
    }

    void LogManager::Flush(bool force) {
        std::unique_lock<std::mutex> lock(latch_);
        if (force) {
            cv_.notify_one();
        } else {
            cv_.notify_one();
        }
    }

    void LogManager::StopFlushThread() {
        // 1. Signal the thread to stop
        {
            std::unique_lock<std::mutex> lock(latch_);
            if (stop_flush_thread_) return;
            stop_flush_thread_ = true;
            cv_.notify_all();
        } 
        
        // 2. Wait for the background thread to die
        if (flush_thread_.joinable()) {
            flush_thread_.join();
        }
        
        // 3. [CRITICAL FIX] Flush whatever is left in the buffers!
        // The previous code cleared them; now we WRITE them.
        try {
            bool did_write = false;
            
            // Check the main buffer
            if (!log_buffer_.empty()) {
                log_file_.write(log_buffer_.data(), log_buffer_.size());
                did_write = true;
            }

            // Check the swap buffer (in case the thread was mid-swap)
            if (!flush_buffer_.empty()) {
                log_file_.write(flush_buffer_.data(), flush_buffer_.size());
                did_write = true;
            }

            if (did_write) {
                log_file_.flush();
            }
            
            log_file_.close();
        } catch (...) {
            // Best effort shutdown
        }
    }

    void LogManager::FlushThread() {
        try {
            while (true) {
                std::vector<char> local_flush_buffer;
                
                {
                    std::unique_lock<std::mutex> lock(latch_);

                    // Wait for 30ms or until buffer is large
                    cv_.wait_for(lock, std::chrono::milliseconds(30), [this] {
                        return stop_flush_thread_ || !log_buffer_.empty();
                    });

                    if (stop_flush_thread_) {
                        break; 
                    }

                    if (log_buffer_.empty()) {
                        continue;
                    }

                    SwapBuffers();
                    local_flush_buffer = std::move(flush_buffer_);
                } 

                // --- I/O OPERATION ---
                if (!local_flush_buffer.empty()) {
                    try {
                        if (log_file_.is_open() && log_file_.good()) {
                            log_file_.write(local_flush_buffer.data(), local_flush_buffer.size());
                            log_file_.flush();
                        }
                    } catch (...) {}
                }
            }
        } catch (...) {}
    }
    
    
    
    
    
} // namespace francodb