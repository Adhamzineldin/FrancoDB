#pragma once

#include "recovery/recovery_manager.h"
#include "recovery/log_manager.h"
#include "recovery/checkpoint_manager.h"
#include "storage/table/table_heap.h"
#include "catalog/catalog.h"
#include "catalog/table_metadata.h"
#include "storage/storage_interface.h"
#include <memory>
#include <chrono>
#include <map>
#include <mutex>
#include <iostream>
#include <fstream>
#include <sstream>

namespace francodb {

    /**
     * Checkpoint-Based Snapshot Manager
     * 
     * PROPER FIX FOR BUG #6: O(N) Time-Travel Degradation
     * ====================================================
     * 
     * The Problem:
     * - Old approach: Replay from LSN 0 every time = O(N) where N is total log size
     * - Even with caching, cache misses cause full replay
     * 
     * The Solution:
     * - Each table tracks its last checkpoint LSN (stored in TableMetadata)
     * - The LIVE table heap IS the checkpoint snapshot (at checkpoint_lsn)
     * - Time travel = Clone live table + replay only the delta (checkpoint_lsn to target_time)
     * 
     * Complexity:
     * - O(D) where D = number of log records between checkpoint and target_time
     * - D << N for recent queries (which are the common case)
     * 
     * Example:
     * - Table checkpointed at LSN 10000, current LSN is 10500
     * - Query: SELECT * FROM users AS OF '5 minutes ago' (LSN ~10400)
     * - Old way: Replay LSN 0 to 10400 = 10400 records
     * - New way: Clone live table (at LSN 10000) + replay LSN 10000 to 10400 = 400 records
     * - 26x faster!
     */
    class SnapshotManager {
    public:
        /**
         * Build a snapshot using checkpoint-based optimization.
         * 
         * Algorithm:
         * 1. Get the table's last checkpoint LSN from metadata
         * 2. Clone the LIVE table heap (which represents state at checkpoint)
         * 3. Replay only log records from checkpoint_lsn to target_time
         * 4. Return the snapshot
         * 
         * This is O(delta) instead of O(total_log_size)
         */
        static std::unique_ptr<TableHeap> BuildSnapshot(
            const std::string& table_name,
            uint64_t target_time, 
            IBufferManager* bpm, 
            LogManager* log_manager,
            Catalog* catalog,
            const std::string& db_name = "") 
        {
            std::string target_db = db_name;
            if (target_db.empty() && log_manager) {
                target_db = log_manager->GetCurrentDatabase();
            }
            
            // Get table metadata
            auto* table_info = catalog->GetTable(table_name);
            if (!table_info) {
                std::cerr << "[SnapshotManager] Table not found: " << table_name << std::endl;
                return nullptr;
            }
            
            LogRecord::lsn_t checkpoint_lsn = table_info->GetCheckpointLSN();
            LogRecord::lsn_t current_lsn = log_manager ? log_manager->GetNextLSN() : 0;
            
            std::cout << "[SnapshotManager] Building snapshot for '" << table_name << "'" << std::endl;
            std::cout << "[SnapshotManager]   Checkpoint LSN: " << checkpoint_lsn 
                      << ", Current LSN: " << current_lsn << std::endl;
            
            // Determine the best strategy based on target_time
            uint64_t current_time = LogRecord::GetCurrentTimestamp();
            
            // ================================================================
            // OPTIMIZATION: Use checkpoint as base instead of replaying from 0
            // ================================================================
            
            if (checkpoint_lsn != LogRecord::INVALID_LSN && target_time >= GetCheckpointTimestamp(log_manager, target_db, checkpoint_lsn)) {
                // Target is AFTER the checkpoint - use checkpoint as base
                std::cout << "[SnapshotManager]   Using CHECKPOINT-BASED optimization" << std::endl;
                
                // Clone the live table (which is at checkpoint state)
                auto snapshot = CloneLiveTable(table_info, bpm);
                if (!snapshot) {
                    std::cerr << "[SnapshotManager] Failed to clone live table" << std::endl;
                    return nullptr;
                }
                
                // Replay only the delta: from checkpoint_lsn to target_time
                int delta_records = ReplayDelta(snapshot.get(), table_name, checkpoint_lsn, 
                                                 target_time, log_manager, catalog, target_db);
                
                std::cout << "[SnapshotManager]   Replayed " << delta_records 
                          << " delta records (instead of full replay)" << std::endl;
                
                return snapshot;
            } else {
                // Target is BEFORE the checkpoint - need full replay from 0
                // This is rare for "5 minutes ago" type queries
                std::cout << "[SnapshotManager]   Target before checkpoint, using FULL REPLAY" << std::endl;
                
                auto snapshot = std::make_unique<TableHeap>(bpm, nullptr);
                RecoveryManager recovery(log_manager, catalog, bpm, nullptr);
                recovery.ReplayIntoHeap(snapshot.get(), table_name, target_time, target_db);
                
                return snapshot;
            }
        }

        /**
         * Build snapshot from human-readable timestamp.
         */
        static std::unique_ptr<TableHeap> BuildSnapshotFromString(
            const std::string& table_name,
            const std::string& timestamp_str,
            IBufferManager* bpm,
            LogManager* log_manager,
            Catalog* catalog)
        {
            uint64_t target_time = ParseTimestamp(timestamp_str);
            return BuildSnapshot(table_name, target_time, bpm, log_manager, catalog);
        }

        /**
         * Build snapshot at relative time offset (e.g., "5 minutes ago").
         * This is the COMMON CASE and benefits most from checkpoint optimization.
         */
        static std::unique_ptr<TableHeap> BuildSnapshotSecondsAgo(
            const std::string& table_name,
            uint64_t seconds_ago,
            IBufferManager* bpm,
            LogManager* log_manager,
            Catalog* catalog)
        {
            uint64_t current = LogRecord::GetCurrentTimestamp();
            uint64_t target = current - (seconds_ago * 1000000ULL);
            return BuildSnapshot(table_name, target, bpm, log_manager, catalog);
        }

        static uint64_t GetCurrentTimestamp() {
            return LogRecord::GetCurrentTimestamp();
        }

        static std::string TimestampToString(uint64_t timestamp) {
            time_t seconds = static_cast<time_t>(timestamp / 1000000ULL);
            char buffer[64];
            struct tm* tm_info = localtime(&seconds);
            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
            return std::string(buffer);
        }

    private:
        /**
         * Clone the live table heap as our snapshot base.
         * This is the table state at the last checkpoint.
         */
        static std::unique_ptr<TableHeap> CloneLiveTable(TableMetadata* table_info, IBufferManager* bpm) {
            if (!table_info || !table_info->table_heap_) return nullptr;
            
            auto clone = std::make_unique<TableHeap>(bpm, nullptr);
            
            // Copy all tuples from live table to snapshot
            auto iter = table_info->table_heap_->Begin(nullptr);
            int count = 0;
            while (iter != table_info->table_heap_->End()) {
                Tuple tuple = *iter;
                RID rid;
                clone->InsertTuple(tuple, &rid, nullptr);
                ++iter;
                count++;
            }
            
            std::cout << "[SnapshotManager]   Cloned " << count << " tuples from live table" << std::endl;
            return clone;
        }
        
        /**
         * Replay only log records from start_lsn to target_time into the snapshot.
         * This is the KEY OPTIMIZATION - we only process the delta, not the full log.
         */
        static int ReplayDelta(
            TableHeap* snapshot,
            const std::string& table_name,
            LogRecord::lsn_t start_lsn,
            uint64_t target_time,
            LogManager* log_manager,
            Catalog* catalog,
            const std::string& db_name)
        {
            if (!snapshot || !log_manager) return 0;
            
            std::string log_path = log_manager->GetLogFilePath(db_name);
            std::ifstream log_file(log_path, std::ios::binary | std::ios::in);
            
            if (!log_file.is_open()) {
                std::cerr << "[SnapshotManager] Cannot open log: " << log_path << std::endl;
                return 0;
            }
            
            auto* table_info = catalog->GetTable(table_name);
            if (!table_info) return 0;
            
            int delta_count = 0;
            LogRecord record(0, 0, LogRecordType::INVALID);
            
            // Read through log, applying only records in our delta range
            while (ReadLogRecordSimple(log_file, record)) {
                // Skip records before our start LSN
                if (record.lsn_ <= start_lsn) continue;
                
                // Stop if we've passed target time
                if (target_time > 0 && record.timestamp_ > target_time) break;
                
                // Only process records for this table
                if (record.table_name_ != table_name) continue;
                
                // Apply the delta operation
                ApplyDeltaRecord(snapshot, record, table_info);
                delta_count++;
            }
            
            log_file.close();
            return delta_count;
        }
        
        /**
         * Apply a single log record to the snapshot heap.
         */
        static void ApplyDeltaRecord(TableHeap* heap, const LogRecord& record, TableMetadata* table_info) {
            if (!heap || !table_info) return;
            
            switch (record.log_record_type_) {
                case LogRecordType::INSERT: {
                    auto vals = ParseTupleValues(record.new_value_.ToString(), table_info);
                    if (!vals.empty()) {
                        Tuple tuple(vals, table_info->schema_);
                        RID rid;
                        heap->InsertTuple(tuple, &rid, nullptr);
                    }
                    break;
                }
                case LogRecordType::UPDATE: {
                    auto old_vals = ParseTupleValues(record.old_value_.ToString(), table_info);
                    auto new_vals = ParseTupleValues(record.new_value_.ToString(), table_info);
                    
                    // Find and update the matching tuple
                    auto iter = heap->Begin(nullptr);
                    while (iter != heap->End()) {
                        if (TupleMatches(*iter, old_vals, table_info)) {
                            heap->MarkDelete(iter.GetRID(), nullptr);
                            if (!new_vals.empty()) {
                                Tuple new_tuple(new_vals, table_info->schema_);
                                RID rid;
                                heap->InsertTuple(new_tuple, &rid, nullptr);
                            }
                            break;
                        }
                        ++iter;
                    }
                    break;
                }
                case LogRecordType::MARK_DELETE:
                case LogRecordType::APPLY_DELETE: {
                    auto old_vals = ParseTupleValues(record.old_value_.ToString(), table_info);
                    auto iter = heap->Begin(nullptr);
                    while (iter != heap->End()) {
                        if (TupleMatches(*iter, old_vals, table_info)) {
                            heap->MarkDelete(iter.GetRID(), nullptr);
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
        
        /**
         * Simple log record reader for delta replay.
         */
        static bool ReadLogRecordSimple(std::ifstream& log_file, LogRecord& record) {
            int32_t size = 0;
            log_file.read(reinterpret_cast<char*>(&size), sizeof(int32_t));
            if (log_file.gcount() != sizeof(int32_t) || size <= 0 || size > 10000000) return false;
            
            log_file.read(reinterpret_cast<char*>(&record.lsn_), sizeof(LogRecord::lsn_t));
            log_file.read(reinterpret_cast<char*>(&record.prev_lsn_), sizeof(LogRecord::lsn_t));
            log_file.read(reinterpret_cast<char*>(&record.undo_next_lsn_), sizeof(LogRecord::lsn_t));
            log_file.read(reinterpret_cast<char*>(&record.txn_id_), sizeof(LogRecord::txn_id_t));
            log_file.read(reinterpret_cast<char*>(&record.timestamp_), sizeof(LogRecord::timestamp_t));
            
            int log_type_int;
            log_file.read(reinterpret_cast<char*>(&log_type_int), sizeof(int));
            record.log_record_type_ = static_cast<LogRecordType>(log_type_int);
            
            // Read db_name
            uint32_t len = 0;
            log_file.read(reinterpret_cast<char*>(&len), sizeof(uint32_t));
            if (len > 0 && len < 10000) {
                std::vector<char> buf(len);
                log_file.read(buf.data(), len);
                record.db_name_ = std::string(buf.begin(), buf.end());
            }
            
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
                default:
                    // Skip to end of record for other types
                    break;
            }
            
            // Skip CRC at end
            uint32_t crc;
            log_file.read(reinterpret_cast<char*>(&crc), sizeof(uint32_t));
            
            return true;
        }
        
        static std::string ReadString(std::ifstream& in) {
            uint32_t len = 0;
            in.read(reinterpret_cast<char*>(&len), sizeof(uint32_t));
            if (in.gcount() != sizeof(uint32_t) || len > 10000000) return "";
            std::vector<char> buf(len);
            in.read(buf.data(), len);
            return std::string(buf.begin(), buf.end());
        }
        
        static Value ReadValue(std::ifstream& in) {
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
        
        /**
         * Get timestamp of a checkpoint LSN by scanning log.
         */
        static uint64_t GetCheckpointTimestamp(LogManager* log_manager, const std::string& db_name, LogRecord::lsn_t checkpoint_lsn) {
            if (!log_manager || checkpoint_lsn == LogRecord::INVALID_LSN) return 0;
            
            std::string log_path = log_manager->GetLogFilePath(db_name);
            std::ifstream log_file(log_path, std::ios::binary | std::ios::in);
            if (!log_file.is_open()) return 0;
            
            LogRecord record(0, 0, LogRecordType::INVALID);
            while (ReadLogRecordSimple(log_file, record)) {
                if (record.lsn_ == checkpoint_lsn) {
                    log_file.close();
                    return record.timestamp_;
                }
                if (record.lsn_ > checkpoint_lsn) break;
            }
            log_file.close();
            return 0;
        }
        
        static std::vector<Value> ParseTupleValues(const std::string& str, TableMetadata* table_info) {
            std::vector<Value> vals;
            if (!table_info) return vals;
            
            std::stringstream ss(str);
            std::string item;
            uint32_t col_idx = 0;
            
            while (std::getline(ss, item, '|') && col_idx < table_info->schema_.GetColumnCount()) {
                const Column& col = table_info->schema_.GetColumn(col_idx);
                TypeId type = col.GetType();
                
                if (type == TypeId::INTEGER) {
                    try { vals.emplace_back(type, std::stoi(item)); }
                    catch (...) { vals.emplace_back(type, 0); }
                } else if (type == TypeId::DECIMAL) {
                    try { vals.emplace_back(type, std::stod(item)); }
                    catch (...) { vals.emplace_back(type, 0.0); }
                } else {
                    vals.emplace_back(type, item);
                }
                col_idx++;
            }
            return vals;
        }
        
        static bool TupleMatches(const Tuple& tuple, const std::vector<Value>& vals, TableMetadata* table_info) {
            if (!table_info || vals.size() != table_info->schema_.GetColumnCount()) return false;
            for (uint32_t i = 0; i < vals.size(); i++) {
                if (tuple.GetValue(table_info->schema_, i).ToString() != vals[i].ToString()) {
                    return false;
                }
            }
            return true;
        }

        static uint64_t ParseTimestamp(const std::string& timestamp_str) {
            if (timestamp_str.find("ago") != std::string::npos) {
                return ParseRelativeTime(timestamp_str);
            }
            
            bool all_digits = true;
            for (char c : timestamp_str) {
                if (!std::isdigit(c)) { all_digits = false; break; }
            }
            if (all_digits && !timestamp_str.empty()) {
                return std::stoull(timestamp_str) * 1000000ULL;
            }
            
            return ParseISODateTime(timestamp_str);
        }

        static uint64_t ParseRelativeTime(const std::string& str) {
            uint64_t current = LogRecord::GetCurrentTimestamp();
            std::string num_str;
            for (char c : str) { if (std::isdigit(c)) num_str += c; }
            if (num_str.empty()) return current;
            
            uint64_t amount = std::stoull(num_str);
            uint64_t offset = 0;
            
            if (str.find("second") != std::string::npos) offset = amount * 1000000ULL;
            else if (str.find("minute") != std::string::npos) offset = amount * 60 * 1000000ULL;
            else if (str.find("hour") != std::string::npos) offset = amount * 3600 * 1000000ULL;
            else if (str.find("day") != std::string::npos) offset = amount * 86400 * 1000000ULL;
            
            return current - offset;
        }

        static uint64_t ParseISODateTime(const std::string& str) {
            struct tm tm_info = {};
            int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
            int parsed = 0;
            
            // Try format: YYYY-MM-DD HH:MM:SS (ISO format)
            parsed = sscanf(str.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second);
            if (parsed >= 3 && year > 1900) {
                tm_info.tm_year = year - 1900;
                tm_info.tm_mon = month - 1;
                tm_info.tm_mday = day;
                tm_info.tm_hour = hour;
                tm_info.tm_min = minute;
                tm_info.tm_sec = second;
                time_t epoch = mktime(&tm_info);
                return static_cast<uint64_t>(epoch) * 1000000ULL;
            }
            
            // Try format: DD/MM/YYYY HH:MM:SS (European format)
            parsed = sscanf(str.c_str(), "%d/%d/%d %d:%d:%d", &day, &month, &year, &hour, &minute, &second);
            if (parsed >= 3) {
                tm_info.tm_year = year - 1900;
                tm_info.tm_mon = month - 1;
                tm_info.tm_mday = day;
                tm_info.tm_hour = hour;
                tm_info.tm_min = minute;
                tm_info.tm_sec = second;
                time_t epoch = mktime(&tm_info);
                std::cout << "[SnapshotManager] Parsed timestamp: " << day << "/" << month << "/" << year
                          << " " << hour << ":" << minute << ":" << second << std::endl;
                return static_cast<uint64_t>(epoch) * 1000000ULL;
            }
            
            // Try format: MM/DD/YYYY HH:MM:SS (US format)
            parsed = sscanf(str.c_str(), "%d/%d/%d %d:%d:%d", &month, &day, &year, &hour, &minute, &second);
            if (parsed >= 3 && month <= 12) {
                tm_info.tm_year = year - 1900;
                tm_info.tm_mon = month - 1;
                tm_info.tm_mday = day;
                tm_info.tm_hour = hour;
                tm_info.tm_min = minute;
                tm_info.tm_sec = second;
                time_t epoch = mktime(&tm_info);
                return static_cast<uint64_t>(epoch) * 1000000ULL;
            }
            
            std::cerr << "[SnapshotManager] Failed to parse timestamp: " << str << std::endl;
            return LogRecord::GetCurrentTimestamp();
        }
    };

} // namespace francodb

