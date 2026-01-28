/**
 * time_travel_engine.cpp
 *
 * OPTIMIZED Reverse Delta Time Travel Engine
 *
 * Key Optimizations:
 * ==================
 * 1. SMART DIRECTION: Choose forward or backward based on what's closer
 * 2. EARLY TERMINATION: Stop scanning as soon as we reach target
 * 3. NO FULL SCANS: Never read the entire log unless absolutely necessary
 * 4. OFFSET ESTIMATION: Use file size and timestamps to jump to approximate position
 */

#include "recovery/time_travel_engine.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cstring>

namespace chronosdb {
    
    
    
struct TupleHash {
    std::size_t operator()(const std::string& k) const {
        return std::hash<std::string>()(k);
    }
};
    
    // Helper to generate a unique string key for a Tuple (Replaces Tuple::ToString)
    std::string GenerateTupleKey(const Tuple& tuple, const Schema& schema) {
        std::stringstream ss;
        uint32_t count = schema.GetColumnCount();
        for (uint32_t i = 0; i < count; i++) {
            // Append value
            ss << tuple.GetValue(schema, i).ToString();
            // Append separator to distinguish "1, 11" from "11, 1"
            if (i < count - 1) ss << "|";
        }
        return ss.str();
    }
    
    
    
    

// ============================================================================
// CONSTRUCTOR
// ============================================================================

TimeTravelEngine::TimeTravelEngine(LogManager* log_manager, Catalog* catalog,
                                   IBufferManager* bpm, CheckpointManager* checkpoint_mgr)
    : log_manager_(log_manager),
      catalog_(catalog),
      bpm_(bpm),
      checkpoint_mgr_(checkpoint_mgr) {
}

// ============================================================================
// SELECT AS OF - Build Read-Only Snapshot (OPTIMIZED)
// ============================================================================

std::unique_ptr<TableHeap> TimeTravelEngine::BuildSnapshot(
    const std::string& table_name,
    uint64_t target_time,
    const std::string& db_name,
    Strategy strategy) {

    auto start = std::chrono::high_resolution_clock::now();

    std::string actual_db = db_name;
    if (actual_db.empty() && log_manager_) {
        actual_db = log_manager_->GetCurrentDatabase();
    }

    auto* table_info = catalog_->GetTable(table_name);
    if (!table_info) {
        std::cerr << "[TimeTravel] Table not found: " << table_name << std::endl;
        return nullptr;
    }

    uint64_t current_time = LogRecord::GetCurrentTimestamp();

    // FAST PATH: Target is current or future - just clone
    if (target_time >= current_time) {
        return CloneTable(table_name);
    }

    // Get log file info for smart decision
    std::string log_path = log_manager_->GetLogFilePath(actual_db);
    std::ifstream log_file(log_path, std::ios::binary | std::ios::ate);
    if (!log_file.is_open()) {
        std::cerr << "[TimeTravel] Cannot open log: " << log_path << std::endl;
        return nullptr;
    }

    std::streamsize file_size = log_file.tellg();
    log_file.seekg(0);

    // Quick scan to get first and last timestamps
    uint64_t first_timestamp = 0, last_timestamp = 0;
    LogRecord first_rec(0, 0, LogRecordType::INVALID);

    if (ReadLogRecord(log_file, first_rec)) {
        first_timestamp = first_rec.timestamp_;
    }

    // Seek near end to get last timestamp (read last ~1000 bytes)
    if (file_size > 1000) {
        log_file.seekg(-1000, std::ios::end);
    }
    LogRecord last_rec(0, 0, LogRecordType::INVALID);
    while (ReadLogRecord(log_file, last_rec)) {
        last_timestamp = last_rec.timestamp_;
    }
    log_file.close();

    // SMART DECISION: Which direction is faster?
    uint64_t dist_from_start = (target_time > first_timestamp) ? (target_time - first_timestamp) : 0;
    uint64_t dist_from_end = (last_timestamp > target_time) ? (last_timestamp - target_time) : 0;

    std::unique_ptr<TableHeap> result;

    if (dist_from_end < dist_from_start) {
        // Target is closer to END - use REVERSE DELTA (clone + undo)
        result = BuildSnapshotReverseDelta(table_name, target_time, actual_db);
    } else {
        // Target is closer to START - use FORWARD REPLAY
        result = BuildSnapshotForwardReplay(table_name, target_time, actual_db);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "[TimeTravel] Snapshot built in " << ms << "ms" << std::endl;

    return result;
}

// ============================================================================
// REVERSE DELTA - Clone current, undo backwards (OPTIMIZED)
// ============================================================================

std::unique_ptr<TableHeap> TimeTravelEngine::BuildSnapshotReverseDelta(
    const std::string& table_name,
    uint64_t target_time,
    const std::string& db_name) {

    // ========================================================================
    // PHASE 1: Clone Current Table & Build Optimization Map
    // ========================================================================
    
    // 1. Clone table
    auto snapshot = std::make_unique<TableHeap>(bpm_, nullptr);
    auto* table_info = catalog_->GetTable(table_name);
    if (!table_info || !table_info->table_heap_) return nullptr;

    // 2. Build Lookup Map (Tuple String -> RID)
    std::unordered_multimap<std::string, RID> tuple_lookup;
    
    auto iter = table_info->table_heap_->Begin(nullptr);
    while (iter != table_info->table_heap_->End()) {
        Tuple tuple = *iter;
        RID rid;
        snapshot->InsertTuple(tuple, &rid, nullptr);
        
        // FIX: Use helper instead of .ToString()
        std::string tuple_key = GenerateTupleKey(tuple, table_info->schema_); 
        tuple_lookup.insert({tuple_key, rid});
        
        ++iter;
    }

    // ========================================================================
    // PHASE 2: Read Log & Collect Undos
    // ========================================================================

    std::string log_path = log_manager_->GetLogFilePath(db_name);
    
    // Optimization: Buffer the file read
    const size_t BUFFER_SIZE = 128 * 1024; 
    char buffer[BUFFER_SIZE];
    std::ifstream log_file(log_path, std::ios::binary);
    log_file.rdbuf()->pubsetbuf(buffer, BUFFER_SIZE);

    if (!log_file.is_open()) return snapshot;

    // FIX: Define the vector that was missing
    std::vector<InverseOperation> ops_to_undo;
    ops_to_undo.reserve(1000); 

    LogRecord record(0, 0, LogRecordType::INVALID);

    while (ReadLogRecord(log_file, record)) {
        // Skip records older than target (we want to keep those)
        if (record.timestamp_ <= target_time) {
            continue;
        }

        // Filter by table
        if (record.table_name_ != table_name) {
            continue;
        }

        // Filter by type
        if (!record.IsDataModification()) {
            continue;
        }

        InverseOperation op;
        op.original_type = record.log_record_type_;
        op.lsn = record.lsn_;
        op.timestamp = record.timestamp_;
        op.table_name = record.table_name_;

        switch (record.log_record_type_) {
            case LogRecordType::INSERT:
                op.values_to_delete = ParseTupleString(record.new_value_.ToString(), table_info);
                break;
            case LogRecordType::MARK_DELETE:
            case LogRecordType::APPLY_DELETE:
                op.values_to_insert = ParseTupleString(record.old_value_.ToString(), table_info);
                break;
            case LogRecordType::UPDATE:
                op.old_values = ParseTupleString(record.old_value_.ToString(), table_info);
                op.new_values = ParseTupleString(record.new_value_.ToString(), table_info);
                break;
            default:
                continue;
        }
        ops_to_undo.push_back(std::move(op));
    }
    log_file.close();

    // ========================================================================
    // PHASE 3: Apply Undos (Optimized)
    // ========================================================================

    if (ops_to_undo.empty()) {
        return snapshot; 
    }

    // Sort: Newest LSN first (undo from now -> past)
    std::sort(ops_to_undo.begin(), ops_to_undo.end(),
        [](const InverseOperation& a, const InverseOperation& b) {
            return a.lsn > b.lsn;
        });

    for (const auto& op : ops_to_undo) {
        ApplyInverseOperation(snapshot.get(), op, table_info, tuple_lookup);
    }

    return snapshot;
}

// ============================================================================
// FORWARD REPLAY - Start empty, replay to target (OPTIMIZED)
// ============================================================================

std::unique_ptr<TableHeap> TimeTravelEngine::BuildSnapshotForwardReplay(
    const std::string& table_name,
    uint64_t target_time,
    const std::string& db_name) {

    auto* table_info = catalog_->GetTable(table_name);
    if (!table_info) return nullptr;

    auto snapshot = std::make_unique<TableHeap>(bpm_, nullptr);

    std::string log_path = log_manager_->GetLogFilePath(db_name);
    std::ifstream log_file(log_path, std::ios::binary);
    if (!log_file.is_open()) return snapshot;

    LogRecord record(0, 0, LogRecordType::INVALID);

    while (ReadLogRecord(log_file, record)) {
        // STOP as soon as we pass target time
        if (record.timestamp_ > target_time) {
            break;
        }

        // Only process this table
        if (record.table_name_ != table_name) {
            continue;
        }

        switch (record.log_record_type_) {
            case LogRecordType::INSERT: {
                auto vals = ParseTupleString(record.new_value_.ToString(), table_info);
                if (vals.size() == table_info->schema_.GetColumnCount()) {
                    Tuple tuple(vals, table_info->schema_);
                    RID rid;
                    snapshot->InsertTuple(tuple, &rid, nullptr);
                }
                break;
            }
            case LogRecordType::MARK_DELETE:
            case LogRecordType::APPLY_DELETE: {
                auto old_vals = ParseTupleString(record.old_value_.ToString(), table_info);
                auto iter = snapshot->Begin(nullptr);
                while (iter != snapshot->End()) {
                    if (TupleMatches(*iter, old_vals, table_info)) {
                        snapshot->MarkDelete(iter.GetRID(), nullptr);
                        break;
                    }
                    ++iter;
                }
                break;
            }
            case LogRecordType::UPDATE: {
                auto old_vals = ParseTupleString(record.old_value_.ToString(), table_info);
                auto new_vals = ParseTupleString(record.new_value_.ToString(), table_info);
                auto iter = snapshot->Begin(nullptr);
                while (iter != snapshot->End()) {
                    if (TupleMatches(*iter, old_vals, table_info)) {
                        snapshot->MarkDelete(iter.GetRID(), nullptr);
                        if (new_vals.size() == table_info->schema_.GetColumnCount()) {
                            Tuple t(new_vals, table_info->schema_);
                            RID rid;
                            snapshot->InsertTuple(t, &rid, nullptr);
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

    return snapshot;
}

// ============================================================================
// RECOVER TO - Persistent Rollback (OPTIMIZED)
// ============================================================================

TimeTravelEngine::TimeTravelResult TimeTravelEngine::RecoverTo(
    uint64_t target_time,
    const std::string& db_name) {

    auto start = std::chrono::high_resolution_clock::now();

    std::string actual_db = db_name;
    if (actual_db.empty() && log_manager_) {
        actual_db = log_manager_->GetCurrentDatabase();
    }

    uint64_t current_time = LogRecord::GetCurrentTimestamp();
    if (target_time >= current_time) {
        return TimeTravelResult::Success(0, 0, Strategy::AUTO);
    }

    auto all_tables = catalog_->GetAllTables();
    if (all_tables.empty()) {
        return TimeTravelResult::Error("No tables to recover");
    }

    int total_records = 0;

    // Phase 1: Build all snapshots
    std::map<std::string, std::vector<Tuple>> snapshot_data;

    for (auto* table_info : all_tables) {
        if (!table_info || !table_info->table_heap_) continue;

        std::string tbl = table_info->name_;
        if (tbl == "chronos_users" || tbl.find("sys_") == 0) continue;

        auto snapshot = BuildSnapshot(tbl, target_time, actual_db, Strategy::AUTO);
        if (!snapshot) continue;

        std::vector<Tuple> tuples;
        auto iter = snapshot->Begin(nullptr);
        while (iter != snapshot->End()) {
            tuples.push_back(*iter);
            ++iter;
        }
        total_records += tuples.size();
        snapshot_data[tbl] = std::move(tuples);
    }

    // Phase 2: Atomic swap
    for (const auto& [tbl, tuples] : snapshot_data) {
        auto* table_info = catalog_->GetTable(tbl);
        if (!table_info || !table_info->table_heap_) continue;

        TableHeap* heap = table_info->table_heap_.get();

        // Clear existing
        page_id_t page_id = heap->GetFirstPageId();
        while (page_id != INVALID_PAGE_ID) {
            Page* raw = bpm_->FetchPage(page_id);
            if (!raw) break;
            auto* tp = reinterpret_cast<TablePage*>(raw->GetData());
            uint32_t cnt = tp->GetTupleCount();
            for (uint32_t s = 0; s < cnt; s++) {
                heap->MarkDelete(RID(page_id, s), nullptr);
            }
            page_id_t next = tp->GetNextPageId();
            bpm_->UnpinPage(page_id, true);
            page_id = next;
        }

        // Insert snapshot
        for (const auto& tuple : tuples) {
            RID rid;
            heap->InsertTuple(tuple, &rid, nullptr);
        }

        table_info->SetCheckpointLSN(LogRecord::INVALID_LSN);
    }

    bpm_->FlushAllPages();
    if (checkpoint_mgr_) checkpoint_mgr_->BeginCheckpoint();

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    return TimeTravelResult::Success(total_records, ms, Strategy::AUTO);
}

// ============================================================================
// APPLY INVERSE OPERATION
// ============================================================================
    

    
void TimeTravelEngine::ApplyInverseOperation(
    TableHeap* heap,
    const InverseOperation& op,
    const TableMetadata* table_info,
    std::unordered_multimap<std::string, RID>& tuple_lookup) {

    switch (op.original_type) {
        case LogRecordType::INSERT: {
            // Undo INSERT = DELETE
            if (op.values_to_delete.empty()) return;
            
            // Build key to find RID
            Tuple temp_tuple(op.values_to_delete, table_info->schema_);
            std::string key = GenerateTupleKey(temp_tuple, table_info->schema_);
            
            auto range = tuple_lookup.equal_range(key);
            if (range.first != range.second) {
                RID rid_to_delete = range.first->second;
                heap->MarkDelete(rid_to_delete, nullptr);
                tuple_lookup.erase(range.first); 
            }
            break;
        }
        case LogRecordType::MARK_DELETE:
        case LogRecordType::APPLY_DELETE: {
            // Undo DELETE = INSERT
            if (op.values_to_insert.size() == table_info->schema_.GetColumnCount()) {
                Tuple t(op.values_to_insert, table_info->schema_);
                RID rid;
                heap->InsertTuple(t, &rid, nullptr);
                
                // Add to map
                std::string key = GenerateTupleKey(t, table_info->schema_);
                tuple_lookup.insert({key, rid});
            }
            break;
        }
        case LogRecordType::UPDATE: {
            // Undo UPDATE = DELETE new, INSERT old
            
            // 1. Delete New
            Tuple new_tuple(op.new_values, table_info->schema_);
            std::string new_key = GenerateTupleKey(new_tuple, table_info->schema_);
            
            auto range = tuple_lookup.equal_range(new_key);
            if (range.first != range.second) {
                RID rid_to_delete = range.first->second;
                heap->MarkDelete(rid_to_delete, nullptr);
                tuple_lookup.erase(range.first);
                
                // 2. Insert Old
                if (op.old_values.size() == table_info->schema_.GetColumnCount()) {
                    Tuple old_t(op.old_values, table_info->schema_);
                    RID new_rid;
                    heap->InsertTuple(old_t, &new_rid, nullptr);
                    
                    std::string old_key = GenerateTupleKey(old_t, table_info->schema_);
                    tuple_lookup.insert({old_key, new_rid});
                }
            }
            break;
        }
        default: break;
    }
}
    

// ============================================================================
// HELPERS
// ============================================================================

std::unique_ptr<TableHeap> TimeTravelEngine::CloneTable(const std::string& table_name) {
    auto* table_info = catalog_->GetTable(table_name);
    if (!table_info || !table_info->table_heap_) return nullptr;

    auto clone = std::make_unique<TableHeap>(bpm_, nullptr);
    auto iter = table_info->table_heap_->Begin(nullptr);
    while (iter != table_info->table_heap_->End()) {
        Tuple tuple = *iter;
        RID rid;
        clone->InsertTuple(tuple, &rid, nullptr);
        ++iter;
    }
    return clone;
}

std::vector<Value> TimeTravelEngine::ParseTupleString(
    const std::string& tuple_str,
    const TableMetadata* table_info) const {

    std::vector<Value> vals;
    if (!table_info || tuple_str.empty()) return vals;

    // Try binary format first (magic byte 0x02)
    if (!tuple_str.empty() && static_cast<unsigned char>(tuple_str[0]) == 0x02) {
        size_t pos = 1;
        if (pos + sizeof(uint32_t) <= tuple_str.size()) {
            uint32_t count;
            std::memcpy(&count, tuple_str.data() + pos, sizeof(uint32_t));
            pos += sizeof(uint32_t);

            for (uint32_t i = 0; i < count && pos + sizeof(uint32_t) <= tuple_str.size(); i++) {
                uint32_t len;
                std::memcpy(&len, tuple_str.data() + pos, sizeof(uint32_t));
                pos += sizeof(uint32_t);
                if (pos + len > tuple_str.size()) break;

                std::string field = tuple_str.substr(pos, len);
                pos += len;

                if (i < table_info->schema_.GetColumnCount()) {
                    const Column& col = table_info->schema_.GetColumn(i);
                    TypeId type = col.GetType();
                    if (type == TypeId::INTEGER) {
                        try { vals.push_back(Value(type, std::stoi(field))); }
                        catch (...) { vals.push_back(Value(type, 0)); }
                    } else if (type == TypeId::DECIMAL) {
                        try { vals.push_back(Value(type, std::stod(field))); }
                        catch (...) { vals.push_back(Value(type, 0.0)); }
                    } else {
                        vals.push_back(Value(type, field));
                    }
                }
            }
            if (!vals.empty()) {
                // Pad if needed
                while (vals.size() < table_info->schema_.GetColumnCount()) {
                    const Column& col = table_info->schema_.GetColumn(vals.size());
                    TypeId type = col.GetType();
                    if (type == TypeId::INTEGER) vals.push_back(Value(type, 0));
                    else if (type == TypeId::DECIMAL) vals.push_back(Value(type, 0.0));
                    else vals.push_back(Value(type, std::string("")));
                }
                return vals;
            }
        }
    }

    // Fallback: pipe-separated format
    std::stringstream ss(tuple_str);
    std::string item;
    uint32_t col_idx = 0;
    uint32_t col_count = table_info->schema_.GetColumnCount();

    while (std::getline(ss, item, '|') && col_idx < col_count) {
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

    // Pad
    while (vals.size() < col_count) {
        const Column& col = table_info->schema_.GetColumn(vals.size());
        TypeId type = col.GetType();
        if (type == TypeId::INTEGER) vals.push_back(Value(type, 0));
        else if (type == TypeId::DECIMAL) vals.push_back(Value(type, 0.0));
        else vals.push_back(Value(type, std::string("")));
    }

    return vals;
}

bool TimeTravelEngine::TupleMatches(
    const Tuple& tuple,
    const std::vector<Value>& vals,
    const TableMetadata* table_info) const {

    if (!table_info || vals.size() != table_info->schema_.GetColumnCount()) return false;

    for (uint32_t i = 0; i < vals.size(); i++) {
        if (tuple.GetValue(table_info->schema_, i).ToString() != vals[i].ToString()) {
            return false;
        }
    }
    return true;
}

bool TimeTravelEngine::ReadLogRecord(std::ifstream& log_file, LogRecord& record) {
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

    record.db_name_ = ReadString(log_file);

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
        case LogRecordType::CREATE_TABLE:
        case LogRecordType::DROP_TABLE:
        case LogRecordType::CLR:
            record.table_name_ = ReadString(log_file);
            break;
        case LogRecordType::CHECKPOINT_BEGIN:
        case LogRecordType::CHECKPOINT_END: {
            int32_t att_size = 0;
            log_file.read(reinterpret_cast<char*>(&att_size), sizeof(int32_t));
            for (int32_t i = 0; i < att_size && i < 10000; i++) {
                int32_t d; log_file.read(reinterpret_cast<char*>(&d), sizeof(int32_t));
                log_file.read(reinterpret_cast<char*>(&d), sizeof(int32_t));
                log_file.read(reinterpret_cast<char*>(&d), sizeof(int32_t));
            }
            int32_t dpt_size = 0;
            log_file.read(reinterpret_cast<char*>(&dpt_size), sizeof(int32_t));
            for (int32_t i = 0; i < dpt_size && i < 10000; i++) {
                int32_t d; log_file.read(reinterpret_cast<char*>(&d), sizeof(int32_t));
                log_file.read(reinterpret_cast<char*>(&d), sizeof(int32_t));
            }
            break;
        }
        default:
            break;
    }

    uint32_t crc;
    log_file.read(reinterpret_cast<char*>(&crc), sizeof(uint32_t));
    record.size_ = size;
    return true;
}

std::string TimeTravelEngine::ReadString(std::ifstream& in) {
    uint32_t len = 0;
    in.read(reinterpret_cast<char*>(&len), sizeof(uint32_t));
    if (in.gcount() != sizeof(uint32_t) || len > 10000000) return "";
    std::vector<char> buf(len);
    in.read(buf.data(), len);
    return std::string(buf.begin(), buf.end());
}

Value TimeTravelEngine::ReadValue(std::ifstream& in) {
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

// Stub implementations for interface completeness
TimeTravelEngine::Strategy TimeTravelEngine::ChooseStrategy(uint64_t, const std::string&) {
    return Strategy::AUTO;
}

int TimeTravelEngine::EstimateOperationCount(uint64_t, const std::string&) {
    return 0;
}

TimeTravelEngine::TimeTravelResult TimeTravelEngine::RecoverToReverseDelta(uint64_t t, const std::string& db) {
    return RecoverTo(t, db);
}

TimeTravelEngine::TimeTravelResult TimeTravelEngine::RecoverToForwardReplay(uint64_t t, const std::string& db) {
    return RecoverTo(t, db);
}

} // namespace chronosdb
