#include "recovery/recovery_manager.h"
#include "recovery/checkpoint_index.h"
#include "common/crc32.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <filesystem>
#include "storage/table/table_heap.h"
#include "catalog/catalog.h"
#include "common/type.h"
#include "common/value.h"

namespace chronosdb {

    // ========================================================================
    // CONSTRUCTOR
    // ========================================================================

    RecoveryManager::RecoveryManager(LogManager* log_manager, Catalog* catalog, 
                                     IBufferManager* bpm, CheckpointManager* checkpoint_mgr)
        : log_manager_(log_manager), catalog_(catalog), bpm_(bpm), checkpoint_mgr_(checkpoint_mgr) {
        std::cout << "[RecoveryManager] Initialized" << std::endl;
    }

    // ========================================================================
    // DESERIALIZATION HELPERS
    // ========================================================================

    std::string RecoveryManager::ReadString(std::ifstream& in) {
        uint32_t len = 0;
        in.read(reinterpret_cast<char*>(&len), sizeof(uint32_t));
        if (in.gcount() != sizeof(uint32_t)) return "";
        if (len > 10000000) return "";  // Sanity check
        std::vector<char> buf(len);
        in.read(buf.data(), len);
        return std::string(buf.begin(), buf.end());
    }

    int32_t RecoveryManager::ReadInt32(std::ifstream& in) {
        int32_t val = 0;
        in.read(reinterpret_cast<char*>(&val), sizeof(int32_t));
        return val;
    }

    uint64_t RecoveryManager::ReadUInt64(std::ifstream& in) {
        uint64_t val = 0;
        in.read(reinterpret_cast<char*>(&val), sizeof(uint64_t));
        return val;
    }

    Value RecoveryManager::ReadValue(std::ifstream& in) {
        int type_id = 0;
        in.read(reinterpret_cast<char*>(&type_id), sizeof(int));
        std::string s_val = ReadString(in);
        
        TypeId type = static_cast<TypeId>(type_id);
        if (type == TypeId::INTEGER) { 
            try { return Value(type, std::stoi(s_val)); } 
            catch (...) { return Value(type, 0); } 
        }
        if (type == TypeId::DECIMAL) { 
            try { return Value(type, std::stod(s_val)); } 
            catch (...) { return Value(type, 0.0); } 
        }
        return Value(type, s_val);
    }

    // ========================================================================
    // BINARY-SAFE TUPLE SERIALIZATION (Bug #4 Fix)
    // Uses length-prefixed encoding to safely handle pipe characters in data
    // ========================================================================
    
    // Binary format: [field_count:4][len1:4][data1][len2:4][data2]...
    // Magic header byte 0x02 identifies binary format vs legacy pipe format
    std::string RecoveryManager::SerializeTupleBinary(const std::vector<Value>& values) {
        std::string result;
        
        // Magic byte to identify binary format
        result.push_back(0x02);
        
        // Field count
        uint32_t count = static_cast<uint32_t>(values.size());
        result.append(reinterpret_cast<const char*>(&count), sizeof(uint32_t));
        
        // Each field: [length:4][data]
        for (const auto& val : values) {
            std::string field_data = val.ToString();
            uint32_t len = static_cast<uint32_t>(field_data.length());
            result.append(reinterpret_cast<const char*>(&len), sizeof(uint32_t));
            result.append(field_data);
        }
        
        return result;
    }
    
    std::vector<std::string> RecoveryManager::DeserializeTupleBinary(const std::string& data) {
        std::vector<std::string> result;
        
        if (data.empty()) return result;
        
        // Check for binary format magic byte
        if (static_cast<unsigned char>(data[0]) != 0x02) {
            // Legacy pipe format - use fallback parsing
            return result;  // Empty signals caller to use legacy parsing
        }
        
        size_t pos = 1;  // Skip magic byte
        
        // Read field count
        if (pos + sizeof(uint32_t) > data.size()) return result;
        uint32_t count = 0;
        std::memcpy(&count, data.data() + pos, sizeof(uint32_t));
        pos += sizeof(uint32_t);
        
        // Sanity check
        if (count > 1000) return result;
        
        // Read each field
        for (uint32_t i = 0; i < count; i++) {
            if (pos + sizeof(uint32_t) > data.size()) break;
            
            uint32_t len = 0;
            std::memcpy(&len, data.data() + pos, sizeof(uint32_t));
            pos += sizeof(uint32_t);
            
            if (pos + len > data.size()) break;
            
            result.push_back(data.substr(pos, len));
            pos += len;
        }
        
        return result;
    }
    
    std::vector<Value> RecoveryManager::ParseTupleStringSafe(const std::string& tuple_str,
                                                              const TableMetadata* table_info) const {
        std::vector<Value> vals;
        if (!table_info || tuple_str.empty()) return vals;
        
        // Try binary format first
        std::vector<std::string> fields = DeserializeTupleBinary(tuple_str);
        
        if (fields.empty()) {
            // Fallback to legacy pipe-separated format for backward compatibility
            std::stringstream ss(tuple_str);
            std::string item;
            while (std::getline(ss, item, '|')) {
                fields.push_back(item);
            }
        }
        
        // Convert string fields to Values based on schema
        uint32_t col_count = table_info->schema_.GetColumnCount();
        for (uint32_t col_idx = 0; col_idx < fields.size() && col_idx < col_count; col_idx++) {
            const Column& col = table_info->schema_.GetColumn(col_idx);
            TypeId type = col.GetType();
            const std::string& item = fields[col_idx];
            
            if (type == TypeId::INTEGER) {
                try { vals.push_back(Value(type, std::stoi(item))); }
                catch (...) { vals.push_back(Value(type, 0)); }
            } else if (type == TypeId::DECIMAL) {
                try { vals.push_back(Value(type, std::stod(item))); }
                catch (...) { vals.push_back(Value(type, 0.0)); }
            } else {
                vals.push_back(Value(type, item));
            }
        }
        
        // CRITICAL FIX: Pad with default values if we have fewer columns than schema
        // This ensures Tuple constructor doesn't fail due to count mismatch
        while (vals.size() < col_count) {
            const Column& col = table_info->schema_.GetColumn(vals.size());
            TypeId type = col.GetType();
            
            if (type == TypeId::INTEGER) {
                vals.push_back(Value(type, 0));
            } else if (type == TypeId::DECIMAL) {
                vals.push_back(Value(type, 0.0));
            } else {
                vals.push_back(Value(type, std::string("")));
            }
        }
        
        return vals;
    }

    // ========================================================================
    // LOG RECORD READING
    // ========================================================================

    bool RecoveryManager::ReadLogRecord(std::ifstream& log_file, LogRecord& record) {
        // Read size
        int32_t size = 0;
        log_file.read(reinterpret_cast<char*>(&size), sizeof(int32_t));
        if (log_file.gcount() != sizeof(int32_t)) return false;
        if (size <= 0 || size > 10000000) return false;

        // Read header fields
        log_file.read(reinterpret_cast<char*>(&record.lsn_), sizeof(LogRecord::lsn_t));
        log_file.read(reinterpret_cast<char*>(&record.prev_lsn_), sizeof(LogRecord::lsn_t));
        log_file.read(reinterpret_cast<char*>(&record.undo_next_lsn_), sizeof(LogRecord::lsn_t));
        log_file.read(reinterpret_cast<char*>(&record.txn_id_), sizeof(LogRecord::txn_id_t));
        log_file.read(reinterpret_cast<char*>(&record.timestamp_), sizeof(LogRecord::timestamp_t));
        
        int log_type_int;
        log_file.read(reinterpret_cast<char*>(&log_type_int), sizeof(int));
        record.log_record_type_ = static_cast<LogRecordType>(log_type_int);

        // Read db_name
        record.db_name_ = ReadString(log_file);

        // Read body based on type
        switch (record.log_record_type_) {
            case LogRecordType::INSERT:
                record.table_name_ = ReadString(log_file);
                record.new_value_ = ReadValue(log_file);
                break;
                
            case LogRecordType::UPDATE:
                record.table_name_ = ReadString(log_file);
                record.old_value_ = ReadValue(log_file);
                record.new_value_ = ReadValue(log_file);
                break;
                
            case LogRecordType::APPLY_DELETE:
            case LogRecordType::MARK_DELETE:
            case LogRecordType::ROLLBACK_DELETE:
                record.table_name_ = ReadString(log_file);
                record.old_value_ = ReadValue(log_file);
                break;

            case LogRecordType::CLR:
                record.table_name_ = ReadString(log_file);
                record.new_value_ = ReadValue(log_file);
                break;

            case LogRecordType::CREATE_TABLE:
            case LogRecordType::DROP_TABLE:
                record.table_name_ = ReadString(log_file);
                break;

            case LogRecordType::CHECKPOINT_BEGIN:
            case LogRecordType::CHECKPOINT_END: {
                // Read Active Transaction Table
                int32_t att_size = ReadInt32(log_file);
                record.active_transactions_.clear();
                for (int32_t i = 0; i < att_size; i++) {
                    ActiveTransactionEntry entry;
                    entry.txn_id = ReadInt32(log_file);
                    entry.last_lsn = ReadInt32(log_file);
                    entry.first_lsn = ReadInt32(log_file);
                    record.active_transactions_.push_back(entry);
                }
                
                // Read Dirty Page Table
                int32_t dpt_size = ReadInt32(log_file);
                record.dirty_pages_.clear();
                for (int32_t i = 0; i < dpt_size; i++) {
                    DirtyPageEntry entry;
                    entry.page_id = ReadInt32(log_file);
                    entry.recovery_lsn = ReadInt32(log_file);
                    record.dirty_pages_.push_back(entry);
                }
                break;
            }
                
            default:
                // BEGIN, COMMIT, ABORT, DDL - no additional data
                break;
        }

        // Read and verify CRC32 checksum
        uint32_t stored_crc = 0;
        log_file.read(reinterpret_cast<char*>(&stored_crc), sizeof(uint32_t));
        if (log_file.gcount() != sizeof(uint32_t)) {
            std::cerr << "[RECOVERY] Warning: Missing CRC for log record LSN=" 
                      << record.lsn_ << " (old format?)" << std::endl;
            // Allow old format records without CRC
        }
        // Note: Full CRC verification would require buffering the entire record
        // and recomputing. For now, we just read it to advance the file position.
        // TODO: Implement full CRC verification in production

        record.size_ = size;
        return true;
    }

    // ========================================================================
    // ARIES CRASH RECOVERY
    // ========================================================================

    void RecoveryManager::ARIES() {
        std::cout << "[ARIES] ========================================" << std::endl;
        std::cout << "[ARIES] Starting ARIES Crash Recovery" << std::endl;

        auto total_start = std::chrono::high_resolution_clock::now();
        last_recovery_stats_ = RecoveryStats();

        // ==============================
        // Phase 1: ANALYSIS
        // ==============================
        std::cout << "[ARIES] Phase 1: ANALYSIS" << std::endl;
        auto phase_start = std::chrono::high_resolution_clock::now();

        LogRecord::lsn_t checkpoint_lsn = LogRecord::INVALID_LSN;
        std::streampos start_offset = 0;

        if (checkpoint_mgr_ != nullptr) {
            checkpoint_lsn = checkpoint_mgr_->GetLastCheckpointLSN();
            if (checkpoint_lsn != LogRecord::INVALID_LSN) {
                start_offset = checkpoint_mgr_->GetCheckpointOffset();
                std::cout << "[ARIES]   Found checkpoint at LSN: " << checkpoint_lsn << std::endl;
                std::cout << "[ARIES]   Checkpoint file offset: " << start_offset << std::endl;
            }
        }

        if (checkpoint_lsn == LogRecord::INVALID_LSN) {
            std::cout << "[ARIES]   No checkpoint found. Will replay from beginning." << std::endl;
        }

        // Scan log to build ATT and DPT
        LogRecord::lsn_t redo_lsn = AnalysisPhase(checkpoint_lsn);
        last_recovery_stats_.start_lsn = redo_lsn;

        auto phase_end = std::chrono::high_resolution_clock::now();
        last_recovery_stats_.analysis_time = 
            std::chrono::duration_cast<std::chrono::milliseconds>(phase_end - phase_start);
        std::cout << "[ARIES]   Analysis complete. ATT size: " << active_transaction_table_.size()
                  << ", DPT size: " << dirty_page_table_.size() << std::endl;

        // ==============================
        // Phase 2: REDO (History Repeating)
        // ==============================
        std::cout << "[ARIES] Phase 2: REDO" << std::endl;
        phase_start = std::chrono::high_resolution_clock::now();

        // Recover system log first
        RunRecoveryLoop("system", 0, start_offset);

        // Then recover all database logs
        std::string data_dir = log_manager_->GetBaseDataDir();
        if (std::filesystem::exists(data_dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(data_dir)) {
                if (entry.is_directory()) {
                    std::string db_name = entry.path().filename().string();
                    if (db_name != "system") {
                        std::cout << "[ARIES]   Recovering database: " << db_name << std::endl;
                        RunRecoveryLoop(db_name, 0, 0);
                    }
                }
            }
        }

        phase_end = std::chrono::high_resolution_clock::now();
        last_recovery_stats_.redo_time = 
            std::chrono::duration_cast<std::chrono::milliseconds>(phase_end - phase_start);

        // ==============================
        // Phase 3: UNDO (Loser Rollback)
        // ==============================
        std::cout << "[ARIES] Phase 3: UNDO" << std::endl;
        phase_start = std::chrono::high_resolution_clock::now();

        // Identify loser transactions (in ATT but not committed)
        std::set<LogRecord::txn_id_t> losers;
        for (const auto& [txn_id, last_lsn] : active_transaction_table_) {
            if (committed_transactions_.find(txn_id) == committed_transactions_.end()) {
                losers.insert(txn_id);
            }
        }

        if (!losers.empty()) {
            std::cout << "[ARIES]   Found " << losers.size() << " loser transactions to undo" << std::endl;
            UndoPhase(losers);
        } else {
            std::cout << "[ARIES]   No loser transactions found" << std::endl;
        }

        phase_end = std::chrono::high_resolution_clock::now();
        last_recovery_stats_.undo_time = 
            std::chrono::duration_cast<std::chrono::milliseconds>(phase_end - phase_start);
        last_recovery_stats_.transactions_rolled_back = losers.size();

        // ==============================
        // Complete
        // ==============================
        auto total_end = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start);

        std::cout << "[ARIES] Recovery Complete!" << std::endl;
        std::cout << "[ARIES]   Total time: " << total_duration.count() << "ms" << std::endl;
        std::cout << "[ARIES]   Records read: " << last_recovery_stats_.records_read << std::endl;
        std::cout << "[ARIES]   Records redone: " << last_recovery_stats_.records_redone << std::endl;
        std::cout << "[ARIES]   Records undone: " << last_recovery_stats_.records_undone << std::endl;
        std::cout << "[ARIES] ========================================" << std::endl;
    }

    LogRecord::lsn_t RecoveryManager::AnalysisPhase(LogRecord::lsn_t checkpoint_lsn) {
        // Clear previous state
        active_transaction_table_.clear();
        dirty_page_table_.clear();
        committed_transactions_.clear();

        // Read system log to find all transactions
        std::string log_path = log_manager_->GetLogFilePath("system");
        std::ifstream log_file(log_path, std::ios::binary | std::ios::in);
        
        if (!log_file.is_open()) {
            std::cout << "[ANALYSIS] No system log found" << std::endl;
            return LogRecord::INVALID_LSN;
        }

        LogRecord::lsn_t min_lsn = LogRecord::INVALID_LSN;
        LogRecord record(0, 0, LogRecordType::INVALID);
        
        // =========================================================================
        // ARIES FIX: Two-pass approach for proper checkpoint handling
        // Pass 1: Find the last checkpoint and initialize ATT/DPT from it
        // Pass 2: Scan forward from checkpoint to update ATT with post-checkpoint changes
        // =========================================================================
        
        std::streampos checkpoint_offset = 0;
        bool found_checkpoint = false;
        
        // Pass 1: Find the LAST checkpoint and initialize ATT/DPT
        while (ReadLogRecord(log_file, record)) {
            if (record.log_record_type_ == LogRecordType::CHECKPOINT_END) {
                // Remember this checkpoint position - we'll restart from here
                checkpoint_offset = log_file.tellg();
                found_checkpoint = true;
                
                // Initialize ATT and DPT from checkpoint (ARIES standard)
                active_transaction_table_.clear();
                dirty_page_table_.clear();
                
                for (const auto& att_entry : record.active_transactions_) {
                    active_transaction_table_[att_entry.txn_id] = att_entry.last_lsn;
                }
                for (const auto& dpt_entry : record.dirty_pages_) {
                    dirty_page_table_[dpt_entry.page_id] = dpt_entry.recovery_lsn;
                }
                
                std::cout << "[ANALYSIS] Initialized from checkpoint LSN " << record.lsn_ 
                          << ": ATT=" << active_transaction_table_.size() 
                          << ", DPT=" << dirty_page_table_.size() << std::endl;
            }
        }
        
        // Pass 2: Scan forward from checkpoint (or beginning) to update state
        if (found_checkpoint) {
            // Seek back to just after the checkpoint
            log_file.clear();  // Clear EOF flag
            log_file.seekg(checkpoint_offset);
        } else {
            // No checkpoint found - start from beginning
            log_file.clear();
            log_file.seekg(0);
        }
        
        // Now scan forward and track transaction state changes AFTER the checkpoint
        while (ReadLogRecord(log_file, record)) {
            last_recovery_stats_.records_read++;

            // Track transactions - this updates ATT based on post-checkpoint activity
            if (record.log_record_type_ == LogRecordType::BEGIN) {
                active_transaction_table_[record.txn_id_] = record.lsn_;
            } else if (record.log_record_type_ == LogRecordType::COMMIT) {
                // CRITICAL: Mark as committed and remove from ATT
                // This prevents double-rollback of committed transactions
                committed_transactions_.insert(record.txn_id_);
                active_transaction_table_.erase(record.txn_id_);
            } else if (record.log_record_type_ == LogRecordType::ABORT) {
                active_transaction_table_.erase(record.txn_id_);
            }

            // Track minimum LSN for dirty pages
            if (record.IsDataModification()) {
                if (min_lsn == LogRecord::INVALID_LSN || record.lsn_ < min_lsn) {
                    min_lsn = record.lsn_;
                }
            }

            last_recovery_stats_.end_lsn = record.lsn_;
        }

        log_file.close();
        return min_lsn;
    }

    void RecoveryManager::RedoPhase() {
        LogRecord::lsn_t checkpoint_lsn = LogRecord::INVALID_LSN;
        if (checkpoint_mgr_ != nullptr) {
            checkpoint_lsn = checkpoint_mgr_->GetLastCheckpointLSN();
        }
        RedoPhase(checkpoint_lsn, 0);
    }

    void RecoveryManager::RedoPhase(LogRecord::lsn_t start_lsn, uint64_t stop_at_time) {
        // Recover all database logs
        std::string data_dir = log_manager_->GetBaseDataDir();
        if (std::filesystem::exists(data_dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(data_dir)) {
                if (entry.is_directory()) {
                    std::string db_name = entry.path().filename().string();
                    RunRecoveryLoop(db_name, stop_at_time, 0);
                }
            }
        }
    }

    void RecoveryManager::UndoPhase() {
        std::set<LogRecord::txn_id_t> losers;
        for (const auto& [txn_id, last_lsn] : active_transaction_table_) {
            if (committed_transactions_.find(txn_id) == committed_transactions_.end()) {
                losers.insert(txn_id);
            }
        }
        UndoPhase(losers);
    }

    void RecoveryManager::UndoPhase(const std::set<LogRecord::txn_id_t>& losers) {
        if (losers.empty()) return;

        std::string db_name = log_manager_->GetCurrentDatabase();
        
        // Use streaming undo to avoid memory exhaustion on large logs (Bug #5 fix)
        StreamingUndoPhase(losers, db_name);
    }
    
    std::map<LogRecord::lsn_t, std::streampos> RecoveryManager::BuildLSNIndex(
        const std::string& db_name,
        const std::set<LogRecord::txn_id_t>& target_txns) {
        
        std::map<LogRecord::lsn_t, std::streampos> lsn_index;
        std::string log_path = log_manager_->GetLogFilePath(db_name);
        std::ifstream log_file(log_path, std::ios::binary | std::ios::in);
        
        if (!log_file.is_open()) {
            return lsn_index;
        }
        
        LogRecord record(0, 0, LogRecordType::INVALID);
        
        while (true) {
            std::streampos offset = log_file.tellg();
            if (!ReadLogRecord(log_file, record)) {
                break;
            }
            
            // Only index records for target transactions (memory optimization)
            if (target_txns.find(record.txn_id_) != target_txns.end() && 
                record.IsDataModification()) {
                lsn_index[record.lsn_] = offset;
            }
        }
        
        log_file.close();
        return lsn_index;
    }
    
    void RecoveryManager::StreamingUndoPhase(const std::set<LogRecord::txn_id_t>& losers,
                                              const std::string& db_name) {
        if (losers.empty()) return;
        
        std::cout << "[UNDO] Starting streaming undo for " << losers.size() 
                  << " loser transactions" << std::endl;
        
        // Step 1: Build LSN index only for loser transactions (bounded memory)
        std::map<LogRecord::lsn_t, std::streampos> lsn_index = BuildLSNIndex(db_name, losers);
        
        if (lsn_index.empty()) {
            std::cout << "[UNDO] No records to undo" << std::endl;
            return;
        }
        
        std::cout << "[UNDO] Indexed " << lsn_index.size() << " records for undo" << std::endl;
        
        // Step 2: Collect starting LSNs (last LSN for each loser transaction)
        // These come from the Active Transaction Table
        std::set<LogRecord::lsn_t> undo_queue;  // LSNs to process (sorted descending by iteration)
        for (const auto& txn_id : losers) {
            auto it = active_transaction_table_.find(txn_id);
            if (it != active_transaction_table_.end() && it->second != LogRecord::INVALID_LSN) {
                undo_queue.insert(it->second);
            }
        }
        
        // Also add all indexed LSNs as starting points (defensive)
        for (const auto& [lsn, offset] : lsn_index) {
            undo_queue.insert(lsn);
        }
        
        // Step 3: Process in reverse LSN order (highest first)
        std::string log_path = log_manager_->GetLogFilePath(db_name);
        std::ifstream log_file(log_path, std::ios::binary | std::ios::in);
        
        if (!log_file.is_open()) {
            std::cerr << "[UNDO] Cannot open log file: " << log_path << std::endl;
            return;
        }
        
        std::set<LogRecord::lsn_t> processed_lsns;  // Track what we've undone
        
        // Process in reverse order (rbegin iterates from highest to lowest)
        for (auto it = undo_queue.rbegin(); it != undo_queue.rend(); ++it) {
            LogRecord::lsn_t current_lsn = *it;
            
            // Skip if already processed
            if (processed_lsns.find(current_lsn) != processed_lsns.end()) {
                continue;
            }
            
            // Find offset in index
            auto offset_it = lsn_index.find(current_lsn);
            if (offset_it == lsn_index.end()) {
                continue;
            }
            
            // Seek to the record
            log_file.clear();
            log_file.seekg(offset_it->second);
            
            LogRecord record(0, 0, LogRecordType::INVALID);
            if (!ReadLogRecord(log_file, record)) {
                continue;
            }
            
            // Verify this is a loser transaction record
            if (losers.find(record.txn_id_) == losers.end()) {
                continue;
            }
            
            // Undo the record
            if (record.IsDataModification()) {
                UndoLogRecord(record);
                last_recovery_stats_.records_undone++;
                processed_lsns.insert(current_lsn);
            }
        }
        
        log_file.close();
        std::cout << "[UNDO] Completed streaming undo: " << processed_lsns.size() 
                  << " records undone" << std::endl;
    }

    // ========================================================================
    // RECOVERY LOOP
    // ========================================================================

    void RecoveryManager::RunRecoveryLoop(const std::string& db_name, 
                                          uint64_t stop_at_time, 
                                          std::streampos start_offset) {
        std::string log_path = log_manager_->GetLogFilePath(db_name);
        std::ifstream log_file(log_path, std::ios::binary | std::ios::in);
        
        if (!log_file.is_open()) {
            std::cout << "[RECOVERY] No log file found for database '" << db_name << "'" << std::endl;
            return;
        }

        // Seek to start offset if specified
        if (start_offset > 0) {
            log_file.seekg(start_offset);
        }

        std::cout << "[RECOVERY] Starting recovery for database '" << db_name << "'" << std::endl;
        int record_count = 0;

        LogRecord record(0, 0, LogRecordType::INVALID);
        while (ReadLogRecord(log_file, record)) {
            // Stop if we've passed the target time
            if (stop_at_time > 0 && record.timestamp_ > stop_at_time) {
                break;
            }

            // Apply the log record (Redo)
            RedoLogRecord(record);
            record_count++;
            last_recovery_stats_.records_redone++;
        }

        log_file.close();
        std::cout << "[RECOVERY] Complete. Replayed " << record_count << " records." << std::endl;
    }

    void RecoveryManager::RecoverDatabase(const std::string& db_name) {
        std::cout << "[RECOVERY] Recovering database: " << db_name << std::endl;
        RunRecoveryLoop(db_name, 0, 0);
    }

    // ========================================================================
    // LOG RECORD OPERATIONS
    // ========================================================================

    void RecoveryManager::RedoLogRecord(const LogRecord& record) {
        // ARIES PageLSN Idempotency Check:
        // Skip redo if the page has already been updated past this record's LSN.
        // This prevents double-applying changes during recovery.

        // Guard against null catalog (can happen in unit tests)
        if (catalog_ == nullptr) {
            return;
        }

        auto table_info = catalog_->GetTable(record.table_name_);
        
        // For data modification operations, check if we should skip based on page LSN
        if (table_info && record.IsDataModification() && record.lsn_ != LogRecord::INVALID_LSN) {
            // Get the first page of the table to check its LSN
            // Note: In a more sophisticated system, we'd track page_id per log record
            page_id_t first_page_id = table_info->table_heap_->GetFirstPageId();
            if (first_page_id != INVALID_PAGE_ID && bpm_ != nullptr) {
                Page* page = bpm_->FetchPage(first_page_id);
                if (page != nullptr) {
                    lsn_t page_lsn = page->GetPageLSN();
                    bpm_->UnpinPage(first_page_id, false);
                    
                    // ARIES idempotency: Skip if page already has this or later update
                    if (page_lsn != INVALID_LSN && page_lsn >= record.lsn_) {
                        // Page already reflects this or later changes - skip redo
                        return;
                    }
                }
            }
        }
        
        // Helper lambda to parse tuple strings with binary-safe deserialization (Bug #4 fix)
        // Uses ParseTupleStringSafe which handles both binary and legacy pipe formats
        auto parseTupleString = [&](const std::string& tuple_str) -> std::vector<Value> {
            return ParseTupleStringSafe(tuple_str, table_info);
        };

        // Helper lambda to compare a tuple with parsed values
        auto tuplesMatch = [&](const Tuple& tuple, const std::vector<Value>& vals) -> bool {
            if (!table_info || vals.size() != table_info->schema_.GetColumnCount()) return false;
            
            for (uint32_t i = 0; i < vals.size(); i++) {
                Value tuple_val = tuple.GetValue(table_info->schema_, i);
                if (tuple_val.ToString() != vals[i].ToString()) {
                    return false;
                }
            }
            return true;
        };

        // Helper lambda to update page LSN after successful redo (ARIES compliance)
        auto updatePageLSN = [&](page_id_t page_id) {
            if (page_id != INVALID_PAGE_ID && bpm_ != nullptr && record.lsn_ != LogRecord::INVALID_LSN) {
                Page* page = bpm_->FetchPage(page_id);
                if (page != nullptr) {
                    page->SetPageLSN(record.lsn_);
                    bpm_->UnpinPage(page_id, true);  // Mark dirty since we updated LSN
                }
            }
        };

        switch (record.log_record_type_) {
            case LogRecordType::INSERT: {
                if (table_info) {
                    std::vector<Value> values = parseTupleString(record.new_value_.ToString());
                    if (values.size() == table_info->schema_.GetColumnCount()) {
                        Tuple tuple(values, table_info->schema_);
                        RID rid;
                        if (table_info->table_heap_->InsertTuple(tuple, &rid, nullptr)) {
                            // Update page LSN for ARIES compliance
                            updatePageLSN(rid.GetPageId());
                        }
                    }
                }
                break;
            }
            
            case LogRecordType::UPDATE: {
                if (table_info) {
                    std::vector<Value> old_vals = parseTupleString(record.old_value_.ToString());
                    std::vector<Value> new_vals = parseTupleString(record.new_value_.ToString());
                    
                    auto iter = table_info->table_heap_->Begin(nullptr);
                    while (iter != table_info->table_heap_->End()) {
                        if (tuplesMatch(*iter, old_vals)) { 
                            // Delete old and insert new
                            RID old_rid = iter.GetRID();
                            page_id_t affected_page = old_rid.GetPageId();
                            table_info->table_heap_->MarkDelete(old_rid, nullptr);
                            
                            if (new_vals.size() == table_info->schema_.GetColumnCount()) {
                                Tuple new_tuple(new_vals, table_info->schema_);
                                RID new_rid;
                                if (table_info->table_heap_->InsertTuple(new_tuple, &new_rid, nullptr)) {
                                    // Update page LSN for ARIES compliance
                                    updatePageLSN(new_rid.GetPageId());
                                    if (new_rid.GetPageId() != affected_page) {
                                        updatePageLSN(affected_page);
                                    }
                                }
                            }
                            break;
                        }
                        ++iter;
                    }
                }
                break;
            }
            
            case LogRecordType::APPLY_DELETE:
            case LogRecordType::MARK_DELETE: {
                if (table_info) {
                    std::vector<Value> old_vals = parseTupleString(record.old_value_.ToString());
                    
                    auto iter = table_info->table_heap_->Begin(nullptr);
                    while (iter != table_info->table_heap_->End()) {
                        if (tuplesMatch(*iter, old_vals)) {
                            RID delete_rid = iter.GetRID();
                            if (table_info->table_heap_->MarkDelete(delete_rid, nullptr)) {
                                // Update page LSN for ARIES compliance
                                updatePageLSN(delete_rid.GetPageId());
                            }
                            break;
                        }
                        ++iter;
                    }
                }
                break;
            }

            case LogRecordType::ROLLBACK_DELETE: {
                // Re-insert the deleted row
                if (table_info) {
                    std::vector<Value> vals = parseTupleString(record.old_value_.ToString());
                    if (vals.size() == table_info->schema_.GetColumnCount()) {
                        Tuple t(vals, table_info->schema_);
                        RID rid;
                        if (table_info->table_heap_->InsertTuple(t, &rid, nullptr)) {
                            // Update page LSN for ARIES compliance
                            updatePageLSN(rid.GetPageId());
                        }
                    }
                }
                break;
            }
            
            case LogRecordType::DROP_DB: {
                // Handle DROP DATABASE during recovery
                std::string db_dir = log_manager_->GetBaseDataDir() + "/" + record.db_name_;
                try {
                    std::filesystem::remove_all(db_dir);
                    std::cout << "[RECOVERY] Dropped database: " << record.db_name_ << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "[RECOVERY] Failed to drop database " << record.db_name_ 
                              << ": " << e.what() << std::endl;
                }
                break;
            }
            
            default:
                // CHECKPOINT, BEGIN, COMMIT, ABORT, etc. - no action needed
                break;
        }
    }

    LogRecord::lsn_t RecoveryManager::UndoLogRecord(const LogRecord& record) {
        // Guard against null catalog (can happen in unit tests)
        if (catalog_ == nullptr) {
            return record.prev_lsn_;
        }

        auto table_info = catalog_->GetTable(record.table_name_);
        if (!table_info) {
            std::cerr << "[UNDO] Table not found: " << record.table_name_ << std::endl;
            return record.prev_lsn_;
        }

        // Helper lambda to parse tuple strings with binary-safe deserialization (Bug #4 fix)
        auto parseTupleString = [&](const std::string& tuple_str) -> std::vector<Value> {
            return ParseTupleStringSafe(tuple_str, table_info);
        };

        // Helper lambda to compare a tuple with parsed values
        auto tuplesMatch = [&](const Tuple& tuple, const std::vector<Value>& vals) -> bool {
            if (vals.size() != table_info->schema_.GetColumnCount()) return false;
            
            for (uint32_t i = 0; i < vals.size(); i++) {
                Value tuple_val = tuple.GetValue(table_info->schema_, i);
                if (tuple_val.ToString() != vals[i].ToString()) {
                    return false;
                }
            }
            return true;
        };

        switch (record.log_record_type_) {
            case LogRecordType::INSERT: {
                // UNDO INSERT = DELETE the inserted row
                std::vector<Value> new_vals = parseTupleString(record.new_value_.ToString());
                
                std::cout << "[UNDO] Undoing INSERT on table '" << record.table_name_ 
                          << "' - deleting: " << record.new_value_.ToString() << std::endl;
                
                auto iter = table_info->table_heap_->Begin(nullptr);
                while (iter != table_info->table_heap_->End()) {
                    if (tuplesMatch(*iter, new_vals)) {
                        table_info->table_heap_->MarkDelete(iter.GetRID(), nullptr);
                        std::cout << "[UNDO] Deleted row at RID(" << iter.GetRID().GetPageId() 
                                  << "," << iter.GetRID().GetSlotId() << ")" << std::endl;
                        break;
                    }
                    ++iter;
                }
                break;
            }
            
            case LogRecordType::UPDATE: {
                // UNDO UPDATE = Replace new values with old values
                std::vector<Value> old_vals = parseTupleString(record.old_value_.ToString());
                std::vector<Value> new_vals = parseTupleString(record.new_value_.ToString());
                
                std::cout << "[UNDO] Undoing UPDATE on table '" << record.table_name_ 
                          << "' - reverting from: " << record.new_value_.ToString()
                          << " to: " << record.old_value_.ToString() << std::endl;
                
                auto iter = table_info->table_heap_->Begin(nullptr);
                while (iter != table_info->table_heap_->End()) {
                    if (tuplesMatch(*iter, new_vals)) {
                        // Build old tuple and replace
                        if (old_vals.size() == table_info->schema_.GetColumnCount()) {
                            Tuple old_tuple(old_vals, table_info->schema_);
                            
                            // Delete current and insert old
                            RID old_rid = iter.GetRID();
                            table_info->table_heap_->MarkDelete(old_rid, nullptr);
                            
                            RID new_rid;
                            table_info->table_heap_->InsertTuple(old_tuple, &new_rid, nullptr);
                            
                            std::cout << "[UNDO] Reverted row" << std::endl;
                        }
                        break;
                    }
                    ++iter;
                }
                break;
            }
            
            case LogRecordType::APPLY_DELETE:
            case LogRecordType::MARK_DELETE: {
                // UNDO DELETE = Re-insert the deleted row
                std::vector<Value> old_vals = parseTupleString(record.old_value_.ToString());
                
                std::cout << "[UNDO] Undoing DELETE on table '" << record.table_name_ 
                          << "' - re-inserting: " << record.old_value_.ToString() << std::endl;
                
                if (old_vals.size() == table_info->schema_.GetColumnCount()) {
                    Tuple t(old_vals, table_info->schema_);
                    RID rid;
                    table_info->table_heap_->InsertTuple(t, &rid, nullptr);
                    std::cout << "[UNDO] Re-inserted row at RID(" << rid.GetPageId() 
                              << "," << rid.GetSlotId() << ")" << std::endl;
                }
                break;
            }
            
            default:
                break;
        }

        return record.prev_lsn_;
    }

    void RecoveryManager::ApplyLogRecord(const LogRecord& record, bool is_undo) {
        if (is_undo) {
            UndoLogRecord(record);
        } else {
            RedoLogRecord(record);
        }
    }

    // ========================================================================
    // TIME TRAVEL
    // ========================================================================

    void RecoveryManager::RollbackToTime(uint64_t target_time) {
        std::string db_name = log_manager_->GetCurrentDatabase();
        std::string log_path = log_manager_->GetLogFilePath(db_name);
        std::ifstream log_file(log_path, std::ios::binary | std::ios::in);
        
        if (!log_file.is_open()) {
            std::cerr << "[ROLLBACK] Cannot open log file" << std::endl;
            return;
        }

        std::cout << "[ROLLBACK] Rolling back to timestamp: " << target_time << std::endl;
        std::cout << "[ROLLBACK] Using UNDO strategy (git reset --hard)" << std::endl;

        // 1. Scan forward to find all records after target_time
        std::vector<LogRecord> future_records;
        LogRecord record(0, 0, LogRecordType::INVALID);
        
        while (ReadLogRecord(log_file, record)) {
            if (record.timestamp_ > target_time && record.IsDataModification()) {
                future_records.push_back(record);
            }
        }

        log_file.close();

        std::cout << "[ROLLBACK] Found " << future_records.size() << " operations to UNDO" << std::endl;

        // 2. Apply undo operations in reverse order
        for (auto it = future_records.rbegin(); it != future_records.rend(); ++it) {
            UndoLogRecord(*it);
        }

        std::cout << "[ROLLBACK] Complete." << std::endl;
    }

    void RecoveryManager::RecoverToTime(uint64_t target_time) {
        std::cout << "[RECOVER_TO] Recovering to timestamp: " << target_time << std::endl;
        
        std::string db_name = log_manager_->GetCurrentDatabase();
        std::cout << "[RECOVER_TO] Current database: " << db_name << std::endl;
        
        // Special case: target_time = 0 or target_time = UINT64_MAX means "recover to latest"
        uint64_t current_time = LogRecord::GetCurrentTimestamp();
        bool recover_to_latest = (target_time == 0) || 
                                 (target_time == UINT64_MAX) ||
                                 (target_time >= current_time);
        
        if (recover_to_latest) {
            target_time = UINT64_MAX;
            std::cout << "[RECOVER_TO] Recovering to LATEST state (all log records)" << std::endl;
        }
        
        // Get all tables in the catalog
        auto all_tables = catalog_->GetAllTables();
        
        if (all_tables.empty()) {
            std::cout << "[RECOVER_TO] No tables to recover" << std::endl;
            return;
        }
        
        for (auto* table_info : all_tables) {
            if (!table_info || !table_info->table_heap_) {
                continue;
            }
            
            std::string table_name = table_info->name_;
            
            // Skip system tables
            if (table_name == "chronos_users" || table_name.find("sys_") == 0) {
                continue;
            }
            
            std::cout << "[RECOVER_TO] Rebuilding table: " << table_name << std::endl;
            
            // ================================================================
            // CHECKPOINT-BASED OPTIMIZATION (Bug #6 fix for RECOVER TO)
            // Instead of replaying from LSN 0, use checkpoint as base
            // ================================================================
            
            std::unique_ptr<TableHeap> snapshot_heap;

            // ================================================================
            // CHECKPOINT INDEX OPTIMIZATION (O(log K) + O(D) instead of O(N))
            //
            // Use the checkpoint index to find the nearest checkpoint BEFORE
            // target_time, then replay only from that offset.
            // ================================================================

            snapshot_heap = std::make_unique<TableHeap>(bpm_, nullptr);
            bool used_optimization = false;

            if (checkpoint_mgr_ != nullptr) {
                CheckpointIndex* checkpoint_index = checkpoint_mgr_->GetCheckpointIndex();

                if (checkpoint_index != nullptr) {
                    const CheckpointEntry* nearest = checkpoint_index->FindNearestBefore(target_time);

                    if (nearest != nullptr && nearest->timestamp > 0) {
                        // Found a checkpoint before target_time - use optimized path
                        std::cout << "[RECOVER_TO]   OPTIMIZATION: Using checkpoint at timestamp "
                                  << nearest->timestamp << " (LSN " << nearest->lsn
                                  << ", offset " << nearest->log_offset << ")" << std::endl;

                        ReplayIntoHeapFromOffset(
                            snapshot_heap.get(),
                            table_name,
                            nearest->log_offset,  // Start from checkpoint position
                            target_time,          // Stop at target
                            db_name
                        );

                        used_optimization = true;
                    }
                }
            }

            if (!used_optimization) {
                // Fallback: Full replay from beginning
                std::cout << "[RECOVER_TO]   Replaying from LSN 0 to target time" << std::endl;
                ReplayIntoHeap(snapshot_heap.get(), table_name, target_time, db_name);
            }
            
            // Count tuples in snapshot
            int snapshot_count = 0;
            page_id_t snap_page_id = snapshot_heap->GetFirstPageId();
            while (snap_page_id != INVALID_PAGE_ID) {
                Page* raw_page = bpm_->FetchPage(snap_page_id);
                if (!raw_page) break;
                auto* table_page = reinterpret_cast<TablePage*>(raw_page->GetData());
                snapshot_count += table_page->GetTupleCount();
                page_id_t next = table_page->GetNextPageId();
                bpm_->UnpinPage(snap_page_id, false);
                snap_page_id = next;
            }
            std::cout << "[RECOVER_TO] Snapshot contains " << snapshot_count << " tuples" << std::endl;
            
            // ================================================================
            // CRITICAL FIX: Read ALL snapshot tuples into memory FIRST
            // This avoids buffer pool conflicts between snapshot and live table
            // ================================================================
            std::vector<Tuple> snapshot_tuples;
            snapshot_tuples.reserve(snapshot_count);
            
            {
                auto iter = snapshot_heap->Begin(nullptr);
                while (iter != snapshot_heap->End()) {
                    snapshot_tuples.push_back(*iter);
                    ++iter;
                }
            }
            std::cout << "[RECOVER_TO] Loaded " << snapshot_tuples.size() << " tuples into memory" << std::endl;
            
            // Now we can safely release snapshot heap - we have all data in memory
            snapshot_heap.reset();
            
            // Step 2: Clear the current table by marking all tuples as deleted
            TableHeap* current_heap = table_info->table_heap_.get();
            page_id_t page_id = current_heap->GetFirstPageId();
            int deleted_count = 0;
            
            while (page_id != INVALID_PAGE_ID) {
                Page* raw_page = bpm_->FetchPage(page_id);
                if (!raw_page) break;
                
                auto* table_page = reinterpret_cast<TablePage*>(raw_page->GetData());
                uint32_t tuple_count = table_page->GetTupleCount();
                
                for (uint32_t slot = 0; slot < tuple_count; slot++) {
                    RID rid(page_id, slot);
                    if (current_heap->MarkDelete(rid, nullptr)) {
                        deleted_count++;
                    }
                }
                
                page_id_t next_page = table_page->GetNextPageId();
                bpm_->UnpinPage(page_id, true);
                page_id = next_page;
            }
            std::cout << "[RECOVER_TO] Deleted " << deleted_count << " existing tuples" << std::endl;
            
            // Step 3: Insert tuples from memory into current table
            int restored_count = 0;
            for (const auto& tuple : snapshot_tuples) {
                RID new_rid;
                if (current_heap->InsertTuple(tuple, &new_rid, nullptr)) {
                    restored_count++;
                }
            }
            
            // Clear the in-memory vector
            snapshot_tuples.clear();
            snapshot_tuples.shrink_to_fit();
            
            // Update the table's checkpoint LSN to reflect current state
            // (The table is now at target_time, but we need a new checkpoint)
            table_info->SetCheckpointLSN(LogRecord::INVALID_LSN);
            
            std::cout << "[RECOVER_TO] Restored " << restored_count << " tuples for table: " << table_name << std::endl;
        }
        
        // Flush changes to disk
        bpm_->FlushAllPages();
        
        // Take a checkpoint after recovery to establish new baseline
        if (checkpoint_mgr_ != nullptr) {
            std::cout << "[RECOVER_TO] Taking checkpoint after recovery..." << std::endl;
            checkpoint_mgr_->BeginCheckpoint();
        }
        
        std::cout << "[RECOVER_TO] Time travel complete." << std::endl;
    }

    void RecoveryManager::RecoverToLSN(LogRecord::lsn_t target_lsn) {
        std::cout << "[RECOVER_TO_LSN] Recovering to LSN: " << target_lsn << std::endl;
        
        std::string db_name = log_manager_->GetCurrentDatabase();
        std::string log_path = log_manager_->GetLogFilePath(db_name);
        std::ifstream log_file(log_path, std::ios::binary | std::ios::in);
        
        if (!log_file.is_open()) {
            std::cerr << "[RECOVER_TO_LSN] Cannot open log file" << std::endl;
            return;
        }

        LogRecord record(0, 0, LogRecordType::INVALID);
        while (ReadLogRecord(log_file, record)) {
            if (record.lsn_ > target_lsn) {
                break;
            }
            RedoLogRecord(record);
        }

        log_file.close();
        std::cout << "[RECOVER_TO_LSN] Complete." << std::endl;
    }

    bool RecoveryManager::ShouldUseUndoStrategy(uint64_t target_time) {
        // Heuristic: Use Undo if target is within the last 1000 records
        // Otherwise, use Redo from checkpoint
        
        // Get current time
        uint64_t current_time = LogRecord::GetCurrentTimestamp();
        uint64_t time_diff = current_time - target_time;
        
        // If target is within the last hour, use Undo
        // 1 hour = 3,600,000,000 microseconds
        return time_diff < 3600000000ULL;
    }

    // ========================================================================
    // SNAPSHOT (SELECT AS OF)
    // ========================================================================

    void RecoveryManager::ReplayIntoHeap(TableHeap* target_heap, 
                                         const std::string& target_table_name, 
                                         uint64_t target_time,
                                         const std::string& db_name) {
        // Determine which database to read from
        std::string actual_db = db_name;
        if (actual_db.empty() && log_manager_) {
            actual_db = log_manager_->GetCurrentDatabase();
        }
        
        std::string log_path = log_manager_->GetLogFilePath(actual_db);
        
        std::cout << "[SNAPSHOT] Reading from database: " << actual_db << std::endl;
        std::cout << "[SNAPSHOT] Log file path: " << log_path << std::endl;
        
        std::ifstream log_file(log_path, std::ios::binary | std::ios::in);
        
        if (!log_file.is_open()) {
            std::cerr << "[SNAPSHOT] Cannot open log file: " << log_path << std::endl;
            return;
        }

        // Check file size
        log_file.seekg(0, std::ios::end);
        auto file_size = log_file.tellg();
        std::cout << "[SNAPSHOT] Log file size: " << file_size << " bytes" << std::endl;
        
        log_file.seekg(0, std::ios::beg);
        
        // Calculate estimated progress for large files
        bool show_progress = (file_size > 1000000);  // Show progress for >1MB logs
        int64_t bytes_per_percent = file_size / 100;
        int last_percent = -1;
        
        // Start time for performance measurement
        auto start_time = std::chrono::high_resolution_clock::now();

        std::cout << "[SNAPSHOT] Building snapshot of '" << target_table_name 
                  << "' at timestamp: " << target_time << std::endl;
        if (show_progress) {
            std::cout << "[SNAPSHOT] Large log file (" << (file_size / 1024 / 1024) 
                      << " MB) - progress will be shown every 10%" << std::endl;
        }
        std::cout << "[SNAPSHOT] This is a 'git checkout --detached' operation" << std::endl;

        int record_count = 0;
        int matching_records = 0;
        int total_records = 0;
        int update_count = 0;  // Track updates separately
        LogRecord record(0, 0, LogRecordType::INVALID);

        while (ReadLogRecord(log_file, record)) {
            total_records++;
            
            // Progress tracking for large files
            if (show_progress && bytes_per_percent > 0) {
                int64_t current_pos = log_file.tellg();
                int current_percent = static_cast<int>(current_pos / bytes_per_percent);
                if (current_percent > last_percent && current_percent % 10 == 0) {
                    last_percent = current_percent;
                    std::cout << "[SNAPSHOT] Progress: " << current_percent << "% (" 
                              << total_records << " records, " 
                              << record_count << " applied)" << std::endl;
                }
            }
            
            // Debug: Show first few records (reduced for large files)
            if (!show_progress && total_records <= 10) {
                std::cout << "[SNAPSHOT] Record " << total_records 
                          << ": Type=" << LogRecordTypeToString(record.log_record_type_)
                          << ", Table='" << record.table_name_ << "'"
                          << ", DB='" << record.db_name_ << "'"
                          << ", LSN=" << record.lsn_
                          << ", Timestamp=" << record.timestamp_ << std::endl;
            }

            // Stop if we've passed the target time
            if (target_time > 0 && record.timestamp_ > target_time) {
                std::cout << "[SNAPSHOT] Reached target time at record " << total_records << std::endl;
                break;
            }

            // Only process records for this table
            if (record.table_name_ != target_table_name) {
                continue;
            }
            
            matching_records++;

            auto table_info = catalog_->GetTable(target_table_name);
            if (!table_info) {
                std::cerr << "[SNAPSHOT] WARNING: Table '" << target_table_name 
                          << "' not in catalog" << std::endl;
                continue;
            }

            switch (record.log_record_type_) {
                case LogRecordType::INSERT: {
                    // Parse the pipe-separated tuple values
                    std::string tuple_str = record.new_value_.ToString();
                    std::vector<Value> vals;
                    
                    // Split by pipe character
                    std::stringstream ss(tuple_str);
                    std::string item;
                    uint32_t col_idx = 0;
                    uint32_t col_count = table_info->schema_.GetColumnCount();
                    
                    while (std::getline(ss, item, '|') && col_idx < col_count) {
                        const Column& col = table_info->schema_.GetColumn(col_idx);
                        TypeId type = col.GetType();
                        
                        if (type == TypeId::INTEGER) {
                            try {
                                vals.push_back(Value(type, std::stoi(item)));
                            } catch (...) {
                                vals.push_back(Value(type, 0));
                            }
                        } else if (type == TypeId::DECIMAL) {
                            try {
                                vals.push_back(Value(type, std::stod(item)));
                            } catch (...) {
                                vals.push_back(Value(type, 0.0));
                            }
                        } else {
                            vals.push_back(Value(type, item));
                        }
                        col_idx++;
                    }
                    
                    // CRITICAL FIX: Pad with default values if we have fewer columns
                    // This ensures Tuple constructor doesn't fail due to count mismatch
                    while (vals.size() < col_count) {
                        const Column& col = table_info->schema_.GetColumn(vals.size());
                        TypeId type = col.GetType();
                        
                        if (type == TypeId::INTEGER) {
                            vals.push_back(Value(type, 0));
                        } else if (type == TypeId::DECIMAL) {
                            vals.push_back(Value(type, 0.0));
                        } else {
                            vals.push_back(Value(type, std::string("")));
                        }
                    }
                    
                    if (!vals.empty() && vals.size() == col_count) {
                        Tuple t(vals, table_info->schema_);
                        RID rid;
                        target_heap->InsertTuple(t, &rid, nullptr);
                        record_count++;
                        // Only log first few inserts to avoid spam
                        if (record_count <= 5) {
                            std::cout << "[SNAPSHOT] Applied INSERT for table '" << record.table_name_ 
                                      << "', values: " << tuple_str << std::endl;
                        } else if (record_count == 6) {
                            std::cout << "[SNAPSHOT] (further INSERT logs suppressed...)" << std::endl;
                        }
                    }
                    break;
                }
                
                case LogRecordType::APPLY_DELETE:
                case LogRecordType::MARK_DELETE: {
                    auto iter = target_heap->Begin(nullptr);
                    while (iter != target_heap->End()) {
                        if ((*iter).GetValue(table_info->schema_, 0).ToString() == 
                            record.old_value_.ToString()) {
                            target_heap->MarkDelete(iter.GetRID(), nullptr);
                            break;
                        }
                        ++iter;
                    }
                    break;
                }
                
                case LogRecordType::UPDATE: {
                    // Parse the old and new values (pipe-separated for multi-column tables)
                    std::string old_str = record.old_value_.ToString();
                    std::string new_str = record.new_value_.ToString();
                    
                    // Parse old values
                    std::vector<Value> old_vals;
                    {
                        std::stringstream ss(old_str);
                        std::string item;
                        uint32_t col_idx = 0;
                        while (std::getline(ss, item, '|') && col_idx < table_info->schema_.GetColumnCount()) {
                            const Column& col = table_info->schema_.GetColumn(col_idx);
                            TypeId type = col.GetType();
                            if (type == TypeId::INTEGER) {
                                try { old_vals.push_back(Value(type, std::stoi(item))); }
                                catch (...) { old_vals.push_back(Value(type, 0)); }
                            } else if (type == TypeId::DECIMAL) {
                                try { old_vals.push_back(Value(type, std::stod(item))); }
                                catch (...) { old_vals.push_back(Value(type, 0.0)); }
                            } else {
                                old_vals.push_back(Value(type, item));
                            }
                            col_idx++;
                        }
                    }
                    
                    // Parse new values
                    std::vector<Value> new_vals;
                    {
                        std::stringstream ss(new_str);
                        std::string item;
                        uint32_t col_idx = 0;
                        while (std::getline(ss, item, '|') && col_idx < table_info->schema_.GetColumnCount()) {
                            const Column& col = table_info->schema_.GetColumn(col_idx);
                            TypeId type = col.GetType();
                            if (type == TypeId::INTEGER) {
                                try { new_vals.push_back(Value(type, std::stoi(item))); }
                                catch (...) { new_vals.push_back(Value(type, 0)); }
                            } else if (type == TypeId::DECIMAL) {
                                try { new_vals.push_back(Value(type, std::stod(item))); }
                                catch (...) { new_vals.push_back(Value(type, 0.0)); }
                            } else {
                                new_vals.push_back(Value(type, item));
                            }
                            col_idx++;
                        }
                    }
                    
                    // Find and update the matching tuple
                    auto iter = target_heap->Begin(nullptr);
                    while (iter != target_heap->End()) {
                        // Check if all values match
                        bool match = true;
                        if (old_vals.size() == table_info->schema_.GetColumnCount()) {
                            for (uint32_t i = 0; i < old_vals.size() && match; i++) {
                                if ((*iter).GetValue(table_info->schema_, i).ToString() != 
                                    old_vals[i].ToString()) {
                                    match = false;
                                }
                            }
                        } else {
                            // Fallback to first column comparison
                            match = (*iter).GetValue(table_info->schema_, 0).ToString() == old_str;
                        }
                        
                        if (match) {
                            // Delete old and insert new (UPDATE = DELETE + INSERT)
                            target_heap->MarkDelete(iter.GetRID(), nullptr);
                            if (new_vals.size() == table_info->schema_.GetColumnCount()) {
                                Tuple new_tuple(new_vals, table_info->schema_);
                                RID new_rid;
                                target_heap->InsertTuple(new_tuple, &new_rid, nullptr);
                                update_count++;
                                // Only log first few updates to avoid spam
                                if (update_count <= 3) {
                                    std::cout << "[SNAPSHOT] Applied UPDATE for table '" << record.table_name_ 
                                              << "': " << old_str << " -> " << new_str << std::endl;
                                } else if (update_count == 4) {
                                    std::cout << "[SNAPSHOT] (further UPDATE logs suppressed...)" << std::endl;
                                }
                            }
                            break;
                        }
                        ++iter;
                    }
                    break;
                }
                
                default:
                    break;
            }
        }

        log_file.close();
        
        // Report timing
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        std::cout << "[SNAPSHOT] Complete. Total records: " << total_records
                  << ", matching '" << target_table_name << "': " << matching_records 
                  << ", inserted: " << record_count 
                  << ", updated: " << update_count
                  << " (elapsed: " << duration.count() << "ms)" << std::endl;
    }

    // ========================================================================
    // CHECKPOINT-OPTIMIZED REPLAY (Skips to offset for efficiency)
    // ========================================================================
    
    void RecoveryManager::ReplayIntoHeapFromOffset(TableHeap* target_heap,
                                                    const std::string& target_table_name,
                                                    std::streampos start_offset,
                                                    uint64_t target_time,
                                                    const std::string& db_name) {
        if (!target_heap || !log_manager_) return;
        
        std::string actual_db = db_name;
        if (actual_db.empty() && log_manager_) {
            actual_db = log_manager_->GetCurrentDatabase();
        }
        
        std::string log_path = log_manager_->GetLogFilePath(actual_db);
        std::ifstream log_file(log_path, std::ios::binary | std::ios::in);
        
        if (!log_file.is_open()) {
            std::cerr << "[SNAPSHOT] Cannot open log file: " << log_path << std::endl;
            return;
        }
        
        // Seek to the start offset (checkpoint position)
        if (start_offset > 0) {
            log_file.seekg(start_offset);
            std::cout << "[SNAPSHOT] Skipped to offset " << start_offset << " (checkpoint optimization)" << std::endl;
        }
        
        auto table_info = catalog_->GetTable(target_table_name);
        if (!table_info) {
            std::cerr << "[SNAPSHOT] Table not found: " << target_table_name << std::endl;
            return;
        }
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        int record_count = 0;
        int total_records = 0;
        LogRecord record(0, 0, LogRecordType::INVALID);
        
        while (ReadLogRecord(log_file, record)) {
            total_records++;
            
            // Stop if we've passed the target time
            if (target_time > 0 && record.timestamp_ > target_time) {
                std::cout << "[SNAPSHOT] Reached target time at record " << total_records << std::endl;
                break;
            }
            
            // Only process records for this table
            if (record.table_name_ != target_table_name) {
                continue;
            }
            
            // Apply the operation (simplified - same logic as ReplayIntoHeap)
            switch (record.log_record_type_) {
                case LogRecordType::INSERT: {
                    std::vector<Value> vals = ParseTupleStringSafe(record.new_value_.ToString(), table_info);
                    if (vals.size() == table_info->schema_.GetColumnCount()) {
                        Tuple t(vals, table_info->schema_);
                        RID rid;
                        target_heap->InsertTuple(t, &rid, nullptr);
                        record_count++;
                    }
                    break;
                }
                case LogRecordType::APPLY_DELETE:
                case LogRecordType::MARK_DELETE: {
                    std::vector<Value> old_vals = ParseTupleStringSafe(record.old_value_.ToString(), table_info);
                    auto iter = target_heap->Begin(nullptr);
                    while (iter != target_heap->End()) {
                        bool match = true;
                        for (uint32_t i = 0; i < old_vals.size() && match; i++) {
                            if ((*iter).GetValue(table_info->schema_, i).ToString() != 
                                old_vals[i].ToString()) {
                                match = false;
                            }
                        }
                        if (match) {
                            target_heap->MarkDelete(iter.GetRID(), nullptr);
                            break;
                        }
                        ++iter;
                    }
                    break;
                }
                case LogRecordType::UPDATE: {
                    std::vector<Value> old_vals = ParseTupleStringSafe(record.old_value_.ToString(), table_info);
                    std::vector<Value> new_vals = ParseTupleStringSafe(record.new_value_.ToString(), table_info);
                    
                    auto iter = target_heap->Begin(nullptr);
                    while (iter != target_heap->End()) {
                        bool match = true;
                        for (uint32_t i = 0; i < old_vals.size() && match; i++) {
                            if ((*iter).GetValue(table_info->schema_, i).ToString() != 
                                old_vals[i].ToString()) {
                                match = false;
                            }
                        }
                        if (match) {
                            target_heap->MarkDelete(iter.GetRID(), nullptr);
                            if (new_vals.size() == table_info->schema_.GetColumnCount()) {
                                Tuple new_tuple(new_vals, table_info->schema_);
                                RID new_rid;
                                target_heap->InsertTuple(new_tuple, &new_rid, nullptr);
                            }
                            break;
                        }
                        ++iter;
                    }
                    break;
                }
                default:
                    break;
            }
        }
        
        log_file.close();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        std::cout << "[SNAPSHOT] Complete (from offset). Records processed: " << total_records
                  << ", applied: " << record_count 
                  << " (elapsed: " << duration.count() << "ms)" << std::endl;
    }

    TableHeap* RecoveryManager::BuildTableSnapshot(const std::string& table_name, 
                                                   uint64_t target_time) {
        auto table_info = catalog_->GetTable(table_name);
        if (!table_info) {
            std::cerr << "[SNAPSHOT] Table not found: " << table_name << std::endl;
            return nullptr;
        }

        std::cout << "[SNAPSHOT] Creating shadow heap for table '" << table_name << "'" << std::endl;

        // Create a temporary heap (Shadow Heap)
        TableHeap* snapshot_heap = new TableHeap(bpm_);
        
        // Replay history into it
        ReplayIntoHeap(snapshot_heap, table_name, target_time);
        
        return snapshot_heap;
    }

    // ========================================================================
    // DELTA REPLAY (Checkpoint-based optimization for RECOVER TO)
    // ========================================================================

    int RecoveryManager::ReplayDeltaIntoHeap(TableHeap* target_heap,
                                              const std::string& target_table_name,
                                              LogRecord::lsn_t start_lsn,
                                              uint64_t target_time,
                                              const std::string& db_name) {
        if (!target_heap || !log_manager_) return 0;
        
        std::string log_path = log_manager_->GetLogFilePath(db_name);
        std::ifstream log_file(log_path, std::ios::binary | std::ios::in);
        
        if (!log_file.is_open()) {
            std::cerr << "[DELTA] Cannot open log file: " << log_path << std::endl;
            return 0;
        }
        
        auto table_info = catalog_->GetTable(target_table_name);
        if (!table_info) {
            std::cerr << "[DELTA] Table not found: " << target_table_name << std::endl;
            return 0;
        }
        
        int delta_count = 0;
        LogRecord record(0, 0, LogRecordType::INVALID);
        
        // Helper to parse tuple values
        auto parseTupleString = [&](const std::string& tuple_str) -> std::vector<Value> {
            return ParseTupleStringSafe(tuple_str, table_info);
        };
        
        auto tuplesMatch = [&](const Tuple& tuple, const std::vector<Value>& vals) -> bool {
            if (vals.size() != table_info->schema_.GetColumnCount()) return false;
            for (uint32_t i = 0; i < vals.size(); i++) {
                if (tuple.GetValue(table_info->schema_, i).ToString() != vals[i].ToString()) {
                    return false;
                }
            }
            return true;
        };
        
        while (ReadLogRecord(log_file, record)) {
            // Skip records at or before start LSN
            if (record.lsn_ <= start_lsn) continue;
            
            // Stop if past target time
            if (target_time > 0 && target_time != UINT64_MAX && record.timestamp_ > target_time) break;
            
            // Only process records for this table
            if (record.table_name_ != target_table_name) continue;
            
            // Apply the delta operation
            switch (record.log_record_type_) {
                case LogRecordType::INSERT: {
                    auto vals = parseTupleString(record.new_value_.ToString());
                    if (vals.size() == table_info->schema_.GetColumnCount()) {
                        Tuple tuple(vals, table_info->schema_);
                        RID rid;
                        target_heap->InsertTuple(tuple, &rid, nullptr);
                        delta_count++;
                    }
                    break;
                }
                case LogRecordType::UPDATE: {
                    auto old_vals = parseTupleString(record.old_value_.ToString());
                    auto new_vals = parseTupleString(record.new_value_.ToString());
                    
                    auto iter = target_heap->Begin(nullptr);
                    while (iter != target_heap->End()) {
                        if (tuplesMatch(*iter, old_vals)) {
                            target_heap->MarkDelete(iter.GetRID(), nullptr);
                            if (new_vals.size() == table_info->schema_.GetColumnCount()) {
                                Tuple new_tuple(new_vals, table_info->schema_);
                                RID rid;
                                target_heap->InsertTuple(new_tuple, &rid, nullptr);
                            }
                            delta_count++;
                            break;
                        }
                        ++iter;
                    }
                    break;
                }
                case LogRecordType::MARK_DELETE:
                case LogRecordType::APPLY_DELETE: {
                    auto old_vals = parseTupleString(record.old_value_.ToString());
                    auto iter = target_heap->Begin(nullptr);
                    while (iter != target_heap->End()) {
                        if (tuplesMatch(*iter, old_vals)) {
                            target_heap->MarkDelete(iter.GetRID(), nullptr);
                            delta_count++;
                            break;
                        }
                        ++iter;
                    }
                    break;
                }
                default:
                    break;
            }
        }
        
        log_file.close();
        return delta_count;
    }

    // ========================================================================
    // HELPERS
    // ========================================================================

    std::vector<LogRecord> RecoveryManager::CollectLogRecords(const std::string& db_name) {
        std::vector<LogRecord> records;
        std::string log_path = log_manager_->GetLogFilePath(db_name);
        std::ifstream log_file(log_path, std::ios::binary | std::ios::in);
        
        if (!log_file.is_open()) {
            return records;
        }

        LogRecord record(0, 0, LogRecordType::INVALID);
        while (ReadLogRecord(log_file, record)) {
            records.push_back(record);
        }

        log_file.close();
        return records;
    }

    std::streampos RecoveryManager::FindLSNOffset(std::ifstream& log_file, LogRecord::lsn_t target_lsn) {
        log_file.seekg(0, std::ios::beg);
        
        LogRecord record(0, 0, LogRecordType::INVALID);
        std::streampos offset = 0;
        
        while (true) {
            offset = log_file.tellg();
            if (!ReadLogRecord(log_file, record)) {
                break;
            }
            if (record.lsn_ == target_lsn) {
                return offset;
            }
        }
        
        return -1;
    }

} // namespace chronosdb

