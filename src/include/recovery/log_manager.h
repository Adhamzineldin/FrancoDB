#pragma once

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <vector>
#include <string>
#include <fstream>
#include "recovery/log_record.h"

namespace francodb {

    class LogManager {
    public:
        explicit LogManager(const std::string& log_file_name) 
            : next_lsn_(0), persistent_lsn_(LogRecord::INVALID_LSN), 
              log_file_name_(log_file_name), stop_flush_thread_(false) {
        
            // Open log file in Append + Binary mode
            log_file_.open(log_file_name_, std::ios::binary | std::ios::out | std::ios::app);
        
            // Start the background flush thread
            flush_thread_ = std::thread(&LogManager::FlushThread, this);
        }

        ~LogManager() {
            StopFlushThread();
        }

        // Called by TransactionManager to add an event
        // Returns the LSN assigned to this record
        LogRecord::lsn_t AppendLogRecord(LogRecord& log_record);

        // Forces a write to disk (Used at Commit)
        void Flush(bool force = true);

        // Stops the background thread
        void StopFlushThread();

        // Get the highest LSN written to disk
        LogRecord::lsn_t GetPersistentLSN() { return persistent_lsn_; }

        std::string GetLogFileName() {return log_file_name_ ;};

    private:
        // Background worker loop
        void FlushThread();
    
        // Internal helper to swap buffers
        void SwapBuffers();

    private:
        std::atomic<LogRecord::lsn_t> next_lsn_;       // Atomic counter for LSNs
        std::atomic<LogRecord::lsn_t> persistent_lsn_; // Last LSN safely on disk

        // Double buffering strategy (Write to one, Flush the other)
        std::vector<char> log_buffer_;
        std::vector<char> flush_buffer_;
    
        std::mutex latch_; // Protects log_buffer_
        std::condition_variable cv_; // Wakes up flush thread

        std::string log_file_name_;
        std::ofstream log_file_;
        std::thread flush_thread_;
        std::atomic<bool> stop_flush_thread_;
    };

} // namespace francodb