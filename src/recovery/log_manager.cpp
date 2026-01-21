#include "recovery/log_manager.h"
#include <cstring>
#include <iostream>
#include <chrono>

namespace francodb {

    // Helper: Write a standard string to buffer [4-byte Len][Data]
    void WriteString(std::vector<char>& buffer, const std::string& str) {
        uint32_t len = static_cast<uint32_t>(str.length());
        const char* len_ptr = reinterpret_cast<const char*>(&len);
        buffer.insert(buffer.end(), len_ptr, len_ptr + sizeof(uint32_t));
        buffer.insert(buffer.end(), str.begin(), str.end());
    }

    // Helper: Write a Value object to buffer (Simplified for Phase 1)
    // Format: [4-byte TypeID] [4-byte Len] [Data]
    void WriteValue(std::vector<char>& buffer, const Value& val) {
        // 1. Write Type
        int type_id = static_cast<int>(val.GetTypeId());
        const char* type_ptr = reinterpret_cast<const char*>(&type_id);
        buffer.insert(buffer.end(), type_ptr, type_ptr + sizeof(int));

        // 2. Write Data based on type
        // Note: For now, we will rely on Value::ToString() for serialization safety.
        // In Phase 3 (Performance), we will serialize raw bytes (Int/Double directly).
        std::string s_val = val.ToString();
        WriteString(buffer, s_val);
    }

    /*
     * Main API: The Transaction Manager calls this.
     * 1. Assigns LSN.
     * 2. Serializes data.
     * 3. Puts it in the RAM buffer.
     */
    LogRecord::lsn_t LogManager::AppendLogRecord(LogRecord& log_record) {
        std::unique_lock<std::mutex> lock(latch_);

        // 1. Assign Unique LSN
        log_record.lsn_ = next_lsn_++;
        
        // 2. Calculate offsets for Header
        // We will construct the binary packet in a temporary buffer first
        std::vector<char> record_buf;
        
        // --- HEADER ---
        // Placeholder for Size (we fill this at the end)
        int32_t size_placeholder = 0;
        const char* ptr = reinterpret_cast<const char*>(&size_placeholder);
        record_buf.insert(record_buf.end(), ptr, ptr + sizeof(int32_t));

        // LSN
        ptr = reinterpret_cast<const char*>(&log_record.lsn_);
        record_buf.insert(record_buf.end(), ptr, ptr + sizeof(LogRecord::lsn_t));

        // Prev LSN
        ptr = reinterpret_cast<const char*>(&log_record.prev_lsn_);
        record_buf.insert(record_buf.end(), ptr, ptr + sizeof(LogRecord::lsn_t));

        // Txn ID
        ptr = reinterpret_cast<const char*>(&log_record.txn_id_);
        record_buf.insert(record_buf.end(), ptr, ptr + sizeof(LogRecord::txn_id_t));

        // Log Type
        int type_int = static_cast<int>(log_record.log_record_type_);
        ptr = reinterpret_cast<const char*>(&type_int);
        record_buf.insert(record_buf.end(), ptr, ptr + sizeof(int));

        // --- BODY ---
        // For Insert: Table + NewVal
        if (log_record.log_record_type_ == LogRecordType::INSERT) {
            WriteString(record_buf, log_record.table_name_);
            WriteValue(record_buf, log_record.new_value_);
        }
        // For Update: Table + OldVal + NewVal
        else if (log_record.log_record_type_ == LogRecordType::UPDATE) {
            WriteString(record_buf, log_record.table_name_);
            WriteValue(record_buf, log_record.old_value_);
            WriteValue(record_buf, log_record.new_value_);
        }
        // For Delete: Table + OldVal
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
    }

    void LogManager::SwapBuffers() {
        // Assume Lock is held by caller
        std::swap(log_buffer_, flush_buffer_);
        // log_buffer_ is now empty and ready for new writes
    }

    void LogManager::Flush(bool force) {
        std::unique_lock<std::mutex> lock(latch_);
        if (force) {
            // Wake up thread immediately
            cv_.notify_one(); 
            // In a real system, we might wait here until persistence is confirmed.
            // For now, notifying is enough.
        } else {
            cv_.notify_one();
        }
    }

    void LogManager::StopFlushThread() {
        if (stop_flush_thread_) return; // Already stopped
        
        stop_flush_thread_ = true;
        cv_.notify_all();
        if (flush_thread_.joinable()) {
            flush_thread_.join();
        }
        // Final flush of anything remaining
        if (!log_buffer_.empty()) {
            std::swap(log_buffer_, flush_buffer_);
            log_file_.write(flush_buffer_.data(), flush_buffer_.size());
            log_file_.flush();
        }
        log_file_.close();
    }

    /*
     * The Background Thread
     * 1. Waits for 30ms OR a signal.
     * 2. Swaps buffers.
     * 3. Writes to disk.
     */
    void LogManager::FlushThread() {
        while (!stop_flush_thread_) {
            {
                std::unique_lock<std::mutex> lock(latch_);
                
                // Wait for 30ms (Group Commit window) or until buffer is large
                cv_.wait_for(lock, std::chrono::milliseconds(30), [this] { 
                    return stop_flush_thread_ || !log_buffer_.empty(); 
                });

                if (log_buffer_.empty() && !stop_flush_thread_) {
                    continue;
                }

                SwapBuffers();
            } // Lock released here, so other threads can keep appending to log_buffer_

            // --- I/O OPERATION (Slow) ---
            if (!flush_buffer_.empty()) {
                // Calculate the max LSN in this batch (hacky parse of last record)
                // In a robust system, SwapBuffers would return the max LSN.
                // Here we just write blindly.
                
                log_file_.write(flush_buffer_.data(), flush_buffer_.size());
                log_file_.flush(); // Essential: Force OS to flush cache to physical disk
                
                // Clear for next use
                flush_buffer_.clear();
            }
        }
    }

} // namespace francodb