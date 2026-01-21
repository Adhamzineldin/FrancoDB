#include "recovery/log_manager.h"
#include <cstring>
#include <iostream>
#include <chrono>
#include <filesystem>

namespace francodb {

    // ========================================================================
    // SERIALIZATION HELPERS
    // ========================================================================

    void LogManager::WriteInt32(std::vector<char>& buffer, int32_t val) {
        const char* ptr = reinterpret_cast<const char*>(&val);
        buffer.insert(buffer.end(), ptr, ptr + sizeof(int32_t));
    }

    void LogManager::WriteInt64(std::vector<char>& buffer, int64_t val) {
        const char* ptr = reinterpret_cast<const char*>(&val);
        buffer.insert(buffer.end(), ptr, ptr + sizeof(int64_t));
    }

    void LogManager::WriteUInt64(std::vector<char>& buffer, uint64_t val) {
        const char* ptr = reinterpret_cast<const char*>(&val);
        buffer.insert(buffer.end(), ptr, ptr + sizeof(uint64_t));
    }

    void LogManager::WriteString(std::vector<char>& buffer, const std::string& str) {
        uint32_t len = static_cast<uint32_t>(str.length());
        const char* len_ptr = reinterpret_cast<const char*>(&len);
        buffer.insert(buffer.end(), len_ptr, len_ptr + sizeof(uint32_t));
        buffer.insert(buffer.end(), str.begin(), str.end());
    }

    void LogManager::WriteValue(std::vector<char>& buffer, const Value& val) {
        try {
            // 1. Write Type
            int type_id = static_cast<int>(val.GetTypeId());
            const char* type_ptr = reinterpret_cast<const char*>(&type_id);
            buffer.insert(buffer.end(), type_ptr, type_ptr + sizeof(int));

            // 2. Write Data based on type (serialized as string for simplicity)
            std::string s_val;
            try {
                s_val = val.ToString();
            } catch (...) {
                s_val = "";
            }
            WriteString(buffer, s_val);
        } catch (...) {
            // Fallback: write invalid type
            int type_id = 0;
            const char* type_ptr = reinterpret_cast<const char*>(&type_id);
            buffer.insert(buffer.end(), type_ptr, type_ptr + sizeof(int));
            WriteString(buffer, "");
        }
    }

    // ========================================================================
    // CONSTRUCTOR / DESTRUCTOR
    // ========================================================================

    LogManager::LogManager(const std::string& base_data_dir)
        : next_lsn_(0), 
          persistent_lsn_(LogRecord::INVALID_LSN),
          base_data_dir_(base_data_dir), 
          current_db_("system"),
          current_file_offset_(0),
          stop_flush_thread_(false) {
        
        // Create base directories if they don't exist
        std::filesystem::create_directories(base_data_dir_);
        std::filesystem::create_directories(base_data_dir_ + "/system");
        
        std::cout << "[LogManager] Initializing with base directory: " << base_data_dir_ << std::endl;
        
        // Open system log by default
        OpenLogFile("system");
        
        // Start the background flush thread
        flush_thread_ = std::thread(&LogManager::FlushThread, this);
        
        std::cout << "[LogManager] Initialized successfully" << std::endl;
    }

    LogManager::~LogManager() {
        StopFlushThread();
    }

    // ========================================================================
    // CORE LOGGING API
    // ========================================================================

    LogRecord::lsn_t LogManager::AppendLogRecord(LogRecord& log_record) {
        try {
            std::unique_lock<std::mutex> lock(latch_);

            // ==============================================================
            // [AUTO-REOPEN LOGIC]
            // If the log file was closed (e.g., by Time Travel reading or database switch),
            // we must re-open it now to allow new writes.
            // ==============================================================
            if (!log_file_.is_open()) {
                OpenLogFile(current_db_);

                // Restart the background flush thread if it was stopped
                if (stop_flush_thread_) {
                    stop_flush_thread_ = false;
                    if (flush_thread_.joinable()) flush_thread_.join();
                    flush_thread_ = std::thread(&LogManager::FlushThread, this);
                }
            }

            // 1. Assign Unique LSN (like a Git commit hash)
            log_record.lsn_ = next_lsn_++;

            // 2. Set database context if not already set
            if (log_record.db_name_.empty()) {
                log_record.db_name_ = current_db_;
            }

            // 3. Update transaction tracking (for Undo Chain)
            {
                std::lock_guard<std::mutex> txn_lock(txn_latch_);
                auto it = active_transactions_.find(log_record.txn_id_);
                if (it != active_transactions_.end()) {
                    // Update prev_lsn to build the chain
                    log_record.prev_lsn_ = it->second.last_lsn;
                    it->second.last_lsn = log_record.lsn_;
                }
            }

            // 4. Serialize the log record
            std::vector<char> record_buf;

            // --- HEADER ---
            // Size placeholder (fill at end)
            int32_t size_placeholder = 0;
            WriteInt32(record_buf, size_placeholder);

            // LSN
            WriteInt32(record_buf, log_record.lsn_);

            // Prev LSN (for Undo Chain - like Git parent commit)
            WriteInt32(record_buf, log_record.prev_lsn_);

            // Undo Next LSN (for CLR records)
            WriteInt32(record_buf, log_record.undo_next_lsn_);

            // Transaction ID
            WriteInt32(record_buf, log_record.txn_id_);

            // Timestamp (for Time Travel/PITR)
            WriteUInt64(record_buf, log_record.timestamp_);

            // Log Type
            WriteInt32(record_buf, static_cast<int>(log_record.log_record_type_));

            // Database Name
            WriteString(record_buf, log_record.db_name_);

            // --- BODY (varies by record type) ---
            switch (log_record.log_record_type_) {
                case LogRecordType::INSERT:
                    WriteString(record_buf, log_record.table_name_);
                    WriteValue(record_buf, log_record.new_value_);
                    break;
                    
                case LogRecordType::UPDATE:
                    WriteString(record_buf, log_record.table_name_);
                    WriteValue(record_buf, log_record.old_value_);
                    WriteValue(record_buf, log_record.new_value_);
                    break;
                    
                case LogRecordType::APPLY_DELETE:
                case LogRecordType::MARK_DELETE:
                case LogRecordType::ROLLBACK_DELETE:
                    WriteString(record_buf, log_record.table_name_);
                    WriteValue(record_buf, log_record.old_value_);
                    break;

                case LogRecordType::CLR:
                    WriteString(record_buf, log_record.table_name_);
                    WriteValue(record_buf, log_record.new_value_);  // Compensation value
                    break;

                case LogRecordType::CREATE_TABLE:
                case LogRecordType::DROP_TABLE:
                    WriteString(record_buf, log_record.table_name_);
                    break;
                    
                case LogRecordType::CREATE_DB:
                case LogRecordType::DROP_DB:
                case LogRecordType::SWITCH_DB:
                    // db_name already written in header
                    break;

                case LogRecordType::CHECKPOINT_BEGIN:
                case LogRecordType::CHECKPOINT_END: {
                    // Write Active Transaction Table (ATT)
                    uint32_t att_size = static_cast<uint32_t>(log_record.active_transactions_.size());
                    WriteInt32(record_buf, att_size);
                    for (const auto& entry : log_record.active_transactions_) {
                        WriteInt32(record_buf, entry.txn_id);
                        WriteInt32(record_buf, entry.last_lsn);
                        WriteInt32(record_buf, entry.first_lsn);
                    }
                    
                    // Write Dirty Page Table (DPT)
                    uint32_t dpt_size = static_cast<uint32_t>(log_record.dirty_pages_.size());
                    WriteInt32(record_buf, dpt_size);
                    for (const auto& entry : log_record.dirty_pages_) {
                        WriteInt32(record_buf, entry.page_id);
                        WriteInt32(record_buf, entry.recovery_lsn);
                    }
                    break;
                }
                    
                default:
                    // BEGIN, COMMIT, ABORT - no additional data
                    break;
            }

            // 5. Patch the Size in Header
            int32_t final_size = static_cast<int32_t>(record_buf.size());
            std::memcpy(record_buf.data(), &final_size, sizeof(int32_t));
            log_record.size_ = final_size;

            // 6. Append to Log Buffer
            log_buffer_.insert(log_buffer_.end(), record_buf.begin(), record_buf.end());

            // 7. Update file offset tracking
            current_file_offset_ += final_size;

            return log_record.lsn_;
        } catch (const std::exception& e) {
            std::cerr << "[LogManager] AppendLogRecord failed: " << e.what() << std::endl;
            return LogRecord::INVALID_LSN;
        } catch (...) {
            std::cerr << "[LogManager] AppendLogRecord failed with unknown error" << std::endl;
            return LogRecord::INVALID_LSN;
        }
    }

    void LogManager::LogCheckpoint() {
        LogRecord rec(0, LogRecord::INVALID_LSN, LogRecordType::CHECKPOINT_END);
        AppendLogRecord(rec);
        Flush(true);
    }

    void LogManager::LogCheckpointWithTables(const std::vector<ActiveTransactionEntry>& active_txns,
                                              const std::vector<DirtyPageEntry>& dirty_pages) {
        LogRecord rec(LogRecordType::CHECKPOINT_END, active_txns, dirty_pages);
        AppendLogRecord(rec);
        Flush(true);
    }

    // ========================================================================
    // BUFFER MANAGEMENT
    // ========================================================================

    void LogManager::SwapBuffers() {
        std::swap(log_buffer_, flush_buffer_);
    }

    void LogManager::Flush(bool force) {
        if (force) {
            // Synchronous flush - write directly to disk
            std::unique_lock<std::mutex> lock(latch_);
            
            if (!log_buffer_.empty() && log_file_.is_open()) {
                log_file_.write(log_buffer_.data(), static_cast<std::streamsize>(log_buffer_.size()));
                log_file_.flush();
                log_buffer_.clear();
                persistent_lsn_.store(next_lsn_.load() - 1);
            }
        } else {
            // Asynchronous flush - just notify the background thread
            std::unique_lock<std::mutex> lock(latch_);
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

        // 3. Flush remaining buffers to disk
        try {
            bool did_write = false;

            if (!log_buffer_.empty() && log_file_.is_open()) {
                log_file_.write(log_buffer_.data(), static_cast<std::streamsize>(log_buffer_.size()));
                log_buffer_.clear();
                did_write = true;
            }

            if (!flush_buffer_.empty() && log_file_.is_open()) {
                log_file_.write(flush_buffer_.data(), static_cast<std::streamsize>(flush_buffer_.size()));
                flush_buffer_.clear();
                did_write = true;
            }

            if (did_write && log_file_.is_open()) {
                log_file_.flush();
            }

            CloseCurrentLog();
        } catch (const std::exception& e) {
            std::cerr << "[LogManager] Error during shutdown flush: " << e.what() << std::endl;
        } catch (...) {
            // Best effort shutdown
        }

        std::cout << "[LogManager] Shutdown complete" << std::endl;
    }

    void LogManager::FlushThread() {
        std::cout << "[LogManager] Flush thread started" << std::endl;
        
        try {
            while (true) {
                std::vector<char> local_flush_buffer;
                {
                    std::unique_lock<std::mutex> lock(latch_);

                    // Wait for 30ms or until buffer is large or stop signal
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
                    flush_buffer_.clear();
                }

                // --- I/O OPERATION (outside lock) ---
                if (!local_flush_buffer.empty()) {
                    try {
                        std::unique_lock<std::mutex> file_lock(latch_);
                        if (log_file_.is_open() && log_file_.good()) {
                            log_file_.write(local_flush_buffer.data(), 
                                          static_cast<std::streamsize>(local_flush_buffer.size()));
                            log_file_.flush();  // Ensure durability
                            
                            // Update persistent LSN
                            persistent_lsn_.store(next_lsn_.load() - 1);
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "[LogManager] Flush error: " << e.what() << std::endl;
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[LogManager] Flush thread error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[LogManager] Flush thread unknown error" << std::endl;
        }
        
        std::cout << "[LogManager] Flush thread stopped" << std::endl;
    }

    // ========================================================================
    // MULTI-DATABASE MANAGEMENT
    // ========================================================================

    void LogManager::SwitchDatabase(const std::string& db_name) {
        std::unique_lock<std::mutex> lock(latch_);
        
        if (db_name == current_db_) {
            return;  // Already on this database
        }

        std::cout << "[LogManager] Switching from '" << current_db_ 
                  << "' to '" << db_name << "'" << std::endl;

        // 1. Flush current buffer to disk
        if (!log_buffer_.empty() && log_file_.is_open()) {
            log_file_.write(log_buffer_.data(), static_cast<std::streamsize>(log_buffer_.size()));
            log_file_.flush();
            log_buffer_.clear();
        }

        // 2. Close current log file
        CloseCurrentLog();

        // 3. Open new log file
        current_db_ = db_name;
        current_file_offset_ = 0;
        OpenLogFile(db_name);

        // 4. Log the context switch in the new log
        LogRecord switch_record(0, LogRecord::INVALID_LSN, LogRecordType::SWITCH_DB, db_name);
        lock.unlock();
        AppendLogRecord(switch_record);
    }

    void LogManager::CreateDatabaseLog(const std::string& db_name) {
        std::unique_lock<std::mutex> lock(latch_);
        
        // Create database directory
        std::string db_dir = base_data_dir_ + "/" + db_name;
        std::filesystem::create_directories(db_dir);
        
        std::cout << "[LogManager] Created log directory: " << db_dir << std::endl;

        // Log the creation in system log
        if (current_db_ != "system") {
            lock.unlock();
            SwitchDatabase("system");
            lock.lock();
        }

        LogRecord create_record(0, LogRecord::INVALID_LSN, LogRecordType::CREATE_DB, db_name);
        lock.unlock();
        AppendLogRecord(create_record);
        Flush(true);
    }

    void LogManager::DropDatabaseLog(const std::string& db_name) {
        std::unique_lock<std::mutex> lock(latch_);
        
        std::cout << "[LogManager] Dropping database log: " << db_name << std::endl;

        // If we're currently on this database, switch to system
        if (current_db_ == db_name) {
            lock.unlock();
            SwitchDatabase("system");
            lock.lock();
        }

        // Log the drop in system log
        LogRecord drop_record(0, LogRecord::INVALID_LSN, LogRecordType::DROP_DB, db_name);
        lock.unlock();
        AppendLogRecord(drop_record);
        Flush(true);
        lock.lock();

        // Delete the database directory
        std::string db_dir = base_data_dir_ + "/" + db_name;
        try {
            std::filesystem::remove_all(db_dir);
            std::cout << "[LogManager] Deleted directory: " << db_dir << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[LogManager] Failed to delete directory " << db_dir 
                      << ": " << e.what() << std::endl;
        }

        // Remove from log streams map
        log_streams_.erase(db_name);
    }

    // ========================================================================
    // TRANSACTION TRACKING
    // ========================================================================

    void LogManager::BeginTransaction(LogRecord::txn_id_t txn_id) {
        std::lock_guard<std::mutex> lock(txn_latch_);
        TransactionLogEntry entry;
        entry.txn_id = txn_id;
        entry.first_lsn = LogRecord::INVALID_LSN;
        entry.last_lsn = LogRecord::INVALID_LSN;
        entry.is_committed = false;
        active_transactions_[txn_id] = entry;
    }

    void LogManager::CommitTransaction(LogRecord::txn_id_t txn_id) {
        std::lock_guard<std::mutex> lock(txn_latch_);
        auto it = active_transactions_.find(txn_id);
        if (it != active_transactions_.end()) {
            it->second.is_committed = true;
            active_transactions_.erase(it);  // Remove from active list
        }
    }

    void LogManager::AbortTransaction(LogRecord::txn_id_t txn_id) {
        std::lock_guard<std::mutex> lock(txn_latch_);
        active_transactions_.erase(txn_id);
    }

    LogRecord::lsn_t LogManager::GetTransactionLastLSN(LogRecord::txn_id_t txn_id) {
        std::lock_guard<std::mutex> lock(txn_latch_);
        auto it = active_transactions_.find(txn_id);
        if (it != active_transactions_.end()) {
            return it->second.last_lsn;
        }
        return LogRecord::INVALID_LSN;
    }

    std::vector<ActiveTransactionEntry> LogManager::GetActiveTransactions() {
        std::lock_guard<std::mutex> lock(txn_latch_);
        std::vector<ActiveTransactionEntry> result;
        for (const auto& [txn_id, entry] : active_transactions_) {
            if (!entry.is_committed) {
                ActiveTransactionEntry att_entry;
                att_entry.txn_id = entry.txn_id;
                att_entry.last_lsn = entry.last_lsn;
                att_entry.first_lsn = entry.first_lsn;
                result.push_back(att_entry);
            }
        }
        return result;
    }

    // ========================================================================
    // FILE MANAGEMENT
    // ========================================================================

    std::string LogManager::GetLogFileName() const {
        return GetLogFilePath(current_db_);
    }

    std::string LogManager::GetLogFilePath(const std::string& db_name) const {
        if (db_name == "system") {
            return base_data_dir_ + "/system/sys.log";
        }
        return base_data_dir_ + "/" + db_name + "/wal.log";
    }

    std::streampos LogManager::GetCurrentOffset() const {
        std::lock_guard<std::mutex> lock(latch_);
        return current_file_offset_;
    }

    void LogManager::OpenLogFile(const std::string& db_name) {
        // Close existing file if open
        if (log_file_.is_open()) {
            log_file_.flush();
            log_file_.close();
        }

        std::string log_path = GetLogFilePath(db_name);
        
        // Ensure directory exists
        std::filesystem::path path(log_path);
        std::filesystem::create_directories(path.parent_path());

        // Open in append + binary mode
        log_file_.open(log_path, std::ios::binary | std::ios::out | std::ios::app);
        
        if (!log_file_.is_open()) {
            std::cerr << "[LogManager] ERROR: Failed to open log file: " << log_path << std::endl;
        } else {
            std::cout << "[LogManager] Opened log file: " << log_path << std::endl;
            
            // Get current file position
            log_file_.seekp(0, std::ios::end);
            current_file_offset_ = log_file_.tellp();
        }
    }

    void LogManager::CloseCurrentLog() {
        if (log_file_.is_open()) {
            log_file_.flush();
            log_file_.close();
            std::cout << "[LogManager] Closed log file" << std::endl;
        }
    }

} // namespace francodb
