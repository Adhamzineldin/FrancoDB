#include "recovery/recovery_manager.h"
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

namespace francodb {

    // ========================================================================
    // CONSTRUCTOR
    // ========================================================================

    RecoveryManager::RecoveryManager(LogManager* log_manager, Catalog* catalog, 
                                     BufferPoolManager* bpm, CheckpointManager* checkpoint_mgr)
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

        while (ReadLogRecord(log_file, record)) {
            last_recovery_stats_.records_read++;

            // Track transactions
            if (record.log_record_type_ == LogRecordType::BEGIN) {
                active_transaction_table_[record.txn_id_] = record.lsn_;
            } else if (record.log_record_type_ == LogRecordType::COMMIT) {
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

            // If we found a checkpoint, extract its ATT and DPT
            if (record.log_record_type_ == LogRecordType::CHECKPOINT_END) {
                for (const auto& att_entry : record.active_transactions_) {
                    active_transaction_table_[att_entry.txn_id] = att_entry.last_lsn;
                }
                for (const auto& dpt_entry : record.dirty_pages_) {
                    dirty_page_table_[dpt_entry.page_id] = dpt_entry.recovery_lsn;
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

        // Collect log records and undo in reverse order
        std::string db_name = log_manager_->GetCurrentDatabase();
        std::vector<LogRecord> records = CollectLogRecords(db_name);

        // Filter to loser transactions and reverse
        std::vector<LogRecord> loser_records;
        for (const auto& record : records) {
            if (losers.find(record.txn_id_) != losers.end() && record.IsDataModification()) {
                loser_records.push_back(record);
            }
        }

        // Undo in reverse order
        for (auto it = loser_records.rbegin(); it != loser_records.rend(); ++it) {
            UndoLogRecord(*it);
            last_recovery_stats_.records_undone++;
        }
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
        auto table_info = catalog_->GetTable(record.table_name_);
        
        // Helper lambda to parse pipe-separated tuple string into Values
        auto parseTupleString = [&](const std::string& tuple_str) -> std::vector<Value> {
            std::vector<Value> vals;
            if (!table_info) return vals;
            
            std::stringstream ss(tuple_str);
            std::string item;
            uint32_t col_idx = 0;
            
            while (std::getline(ss, item, '|') && col_idx < table_info->schema_.GetColumnCount()) {
                const Column& col = table_info->schema_.GetColumn(col_idx);
                TypeId type = col.GetType();
                
                if (type == TypeId::INTEGER) {
                    try { vals.push_back(Value(type, std::stoi(item))); } 
                    catch (...) { vals.push_back(Value(type, 0)); }
                } else if (type == TypeId::DECIMAL) {
                    try { vals.push_back(Value(type, std::stod(item))); } 
                    catch (...) { vals.push_back(Value(type, 0.0)); }
                } else {
                    vals.push_back(Value(type, item));
                }
                col_idx++;
            }
            return vals;
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

        switch (record.log_record_type_) {
            case LogRecordType::INSERT: {
                if (table_info) {
                    std::vector<Value> values = parseTupleString(record.new_value_.ToString());
                    if (values.size() == table_info->schema_.GetColumnCount()) {
                        Tuple tuple(values, table_info->schema_);
                        RID rid;
                        table_info->table_heap_->InsertTuple(tuple, &rid, nullptr);
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
                            table_info->table_heap_->MarkDelete(old_rid, nullptr);
                            
                            if (new_vals.size() == table_info->schema_.GetColumnCount()) {
                                Tuple new_tuple(new_vals, table_info->schema_);
                                RID new_rid;
                                table_info->table_heap_->InsertTuple(new_tuple, &new_rid, nullptr);
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
                            table_info->table_heap_->MarkDelete(iter.GetRID(), nullptr);
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
                        table_info->table_heap_->InsertTuple(t, &rid, nullptr);
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
        auto table_info = catalog_->GetTable(record.table_name_);
        if (!table_info) {
            std::cerr << "[UNDO] Table not found: " << record.table_name_ << std::endl;
            return record.prev_lsn_;
        }

        // Helper lambda to parse pipe-separated tuple string into Values
        auto parseTupleString = [&](const std::string& tuple_str) -> std::vector<Value> {
            std::vector<Value> vals;
            std::stringstream ss(tuple_str);
            std::string item;
            uint32_t col_idx = 0;
            
            while (std::getline(ss, item, '|') && col_idx < table_info->schema_.GetColumnCount()) {
                const Column& col = table_info->schema_.GetColumn(col_idx);
                TypeId type = col.GetType();
                
                if (type == TypeId::INTEGER) {
                    try { vals.push_back(Value(type, std::stoi(item))); } 
                    catch (...) { vals.push_back(Value(type, 0)); }
                } else if (type == TypeId::DECIMAL) {
                    try { vals.push_back(Value(type, std::stod(item))); } 
                    catch (...) { vals.push_back(Value(type, 0.0)); }
                } else {
                    vals.push_back(Value(type, item));
                }
                col_idx++;
            }
            return vals;
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

        // Determine strategy: Undo (short jump) or Redo from checkpoint (long jump)
        if (ShouldUseUndoStrategy(target_time)) {
            std::cout << "[RECOVER_TO] Using UNDO strategy (short jump)" << std::endl;
            RollbackToTime(target_time);
        } else {
            std::cout << "[RECOVER_TO] Using REDO strategy (long jump from checkpoint)" << std::endl;
            
            // Find the nearest checkpoint before target_time
            // For now, just replay from beginning to target_time
            std::string db_name = log_manager_->GetCurrentDatabase();
            
            // Clear current state (in production, we'd truncate the table)
            // Then replay up to target_time
            RunRecoveryLoop(db_name, target_time, 0);
        }
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
        log_file.seekg(0, std::ios::beg);
        std::cout << "[SNAPSHOT] Log file size: " << file_size << " bytes" << std::endl;

        std::cout << "[SNAPSHOT] Building snapshot of '" << target_table_name 
                  << "' at timestamp: " << target_time << std::endl;
        std::cout << "[SNAPSHOT] This is a 'git checkout --detached' operation" << std::endl;

        int record_count = 0;
        int matching_records = 0;
        int total_records = 0;
        LogRecord record(0, 0, LogRecordType::INVALID);

        while (ReadLogRecord(log_file, record)) {
            total_records++;
            
            // Debug: Show first few records
            if (total_records <= 10) {
                std::cout << "[SNAPSHOT] Record " << total_records 
                          << ": Type=" << LogRecordTypeToString(record.log_record_type_)
                          << ", Table='" << record.table_name_ << "'"
                          << ", DB='" << record.db_name_ << "'"
                          << ", LSN=" << record.lsn_
                          << ", Timestamp=" << record.timestamp_ << std::endl;
            }

            // Stop if we've passed the target time
            if (target_time > 0 && record.timestamp_ > target_time) {
                std::cout << "[SNAPSHOT] Stopping at record " << total_records 
                          << " (timestamp " << record.timestamp_ 
                          << " > target " << target_time << ")" << std::endl;
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
                    
                    while (std::getline(ss, item, '|') && col_idx < table_info->schema_.GetColumnCount()) {
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
                    
                    // If we didn't get all columns, something is wrong
                    if (vals.size() != table_info->schema_.GetColumnCount()) {
                        std::cerr << "[SNAPSHOT] Warning: Column count mismatch for INSERT. "
                                  << "Expected " << table_info->schema_.GetColumnCount() 
                                  << " but got " << vals.size() << std::endl;
                        // Fallback: use the raw value for single-column tables
                        if (table_info->schema_.GetColumnCount() == 1) {
                            vals.clear();
                            vals.push_back(record.new_value_);
                        }
                    }
                    
                    if (!vals.empty()) {
                        Tuple t(vals, table_info->schema_);
                        RID rid;
                        target_heap->InsertTuple(t, &rid, nullptr);
                        record_count++;
                        std::cout << "[SNAPSHOT] Applied INSERT for table '" << record.table_name_ 
                                  << "', values: " << tuple_str << std::endl;
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
                                std::cout << "[SNAPSHOT] Applied UPDATE for table '" << record.table_name_ 
                                          << "': " << old_str << " -> " << new_str << std::endl;
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
        std::cout << "[SNAPSHOT] Complete. Total records: " << total_records
                  << ", matching '" << target_table_name << "': " << matching_records 
                  << ", inserted: " << record_count << std::endl;
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

} // namespace francodb

