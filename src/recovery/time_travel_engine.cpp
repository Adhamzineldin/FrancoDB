/**
 * time_travel_engine.cpp
 *
 * Implementation of the Reverse Delta Time Travel Engine.
 *
 * Key Concepts:
 * =============
 *
 * 1. REVERSE DELTA: Instead of replaying from the beginning (O(N)),
 *    we start from the current state and undo operations backwards (O(K)).
 *
 * 2. EFFICIENCY: For "recent past" queries (the common case), this is
 *    dramatically faster because K << N.
 *
 * 3. INVERSE OPERATIONS:
 *    - INSERT(values) undone by -> DELETE(values)
 *    - DELETE(values) undone by -> INSERT(values)
 *    - UPDATE(old, new) undone by -> UPDATE(new, old)
 *
 * 4. ATOMICITY: RECOVER TO uses a two-phase approach:
 *    Phase 1: Build snapshot in memory (can fail safely)
 *    Phase 2: Atomic swap of table contents (all-or-nothing)
 */

#include "recovery/time_travel_engine.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>

namespace chronosdb {

// ============================================================================
// CONSTRUCTOR
// ============================================================================

TimeTravelEngine::TimeTravelEngine(LogManager* log_manager, Catalog* catalog,
                                   IBufferManager* bpm, CheckpointManager* checkpoint_mgr)
    : log_manager_(log_manager),
      catalog_(catalog),
      bpm_(bpm),
      checkpoint_mgr_(checkpoint_mgr) {
    std::cout << "[TimeTravelEngine] Initialized with reverse delta support" << std::endl;
}

// ============================================================================
// SELECT AS OF - Build Read-Only Snapshot
// ============================================================================

std::unique_ptr<TableHeap> TimeTravelEngine::BuildSnapshot(
    const std::string& table_name,
    uint64_t target_time,
    const std::string& db_name,
    Strategy strategy) {

    auto start_time = std::chrono::high_resolution_clock::now();

    // Resolve database name
    std::string actual_db = db_name;
    if (actual_db.empty() && log_manager_) {
        actual_db = log_manager_->GetCurrentDatabase();
    }

    // Validate table exists
    auto* table_info = catalog_->GetTable(table_name);
    if (!table_info) {
        std::cerr << "[TimeTravelEngine] Table not found: " << table_name << std::endl;
        return nullptr;
    }

    // Get current time for comparison
    uint64_t current_time = LogRecord::GetCurrentTimestamp();

    // Special case: target time is current or future - just clone live table
    if (target_time >= current_time) {
        std::cout << "[TimeTravelEngine] Target is current/future - using live table" << std::endl;
        return CloneTable(table_name);
    }

    // Choose strategy
    Strategy chosen = strategy;
    if (strategy == Strategy::AUTO) {
        chosen = ChooseStrategy(target_time, actual_db);
    }

    std::unique_ptr<TableHeap> result;

    if (chosen == Strategy::REVERSE_DELTA) {
        std::cout << "[TimeTravelEngine] Using REVERSE DELTA strategy" << std::endl;
        result = BuildSnapshotReverseDelta(table_name, target_time, actual_db);
    } else {
        std::cout << "[TimeTravelEngine] Using FORWARD REPLAY strategy" << std::endl;
        result = BuildSnapshotForwardReplay(table_name, target_time, actual_db);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "[TimeTravelEngine] Snapshot built in " << duration.count() << "ms" << std::endl;

    return result;
}

// ============================================================================
// RECOVER TO - Persistent Rollback
// ============================================================================

TimeTravelEngine::TimeTravelResult TimeTravelEngine::RecoverTo(
    uint64_t target_time,
    const std::string& db_name) {

    auto start_time = std::chrono::high_resolution_clock::now();

    std::string actual_db = db_name;
    if (actual_db.empty() && log_manager_) {
        actual_db = log_manager_->GetCurrentDatabase();
    }

    std::cout << "[TimeTravelEngine] RECOVER TO timestamp: " << target_time << std::endl;
    std::cout << "[TimeTravelEngine] Database: " << actual_db << std::endl;

    // Special case: recover to latest
    uint64_t current_time = LogRecord::GetCurrentTimestamp();
    if (target_time == UINT64_MAX || target_time >= current_time) {
        std::cout << "[TimeTravelEngine] Target is current - no recovery needed" << std::endl;
        return TimeTravelResult::Success(0, 0, Strategy::AUTO);
    }

    // Choose strategy
    Strategy chosen = ChooseStrategy(target_time, actual_db);
    TimeTravelResult result;

    if (chosen == Strategy::REVERSE_DELTA) {
        result = RecoverToReverseDelta(target_time, actual_db);
    } else {
        result = RecoverToForwardReplay(target_time, actual_db);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();

    return result;
}

// ============================================================================
// STRATEGY SELECTION
// ============================================================================

TimeTravelEngine::Strategy TimeTravelEngine::ChooseStrategy(
    uint64_t target_time,
    const std::string& db_name) {

    uint64_t current_time = LogRecord::GetCurrentTimestamp();

    // If target is within threshold (default 1 hour), use reverse delta
    if (current_time - target_time <= reverse_delta_threshold_) {
        std::cout << "[TimeTravelEngine] Target within threshold - choosing REVERSE_DELTA" << std::endl;
        return Strategy::REVERSE_DELTA;
    }

    // Check if we have a checkpoint that helps
    if (checkpoint_mgr_) {
        uint64_t checkpoint_time = checkpoint_mgr_->GetLastCheckpointTimestamp();

        // If target is after last checkpoint, forward replay is efficient
        if (target_time >= checkpoint_time && checkpoint_time > 0) {
            std::cout << "[TimeTravelEngine] Target after checkpoint - choosing FORWARD_REPLAY" << std::endl;
            return Strategy::FORWARD_REPLAY;
        }
    }

    // Estimate operation count to decide
    int ops = EstimateOperationCount(target_time, db_name);

    // Heuristic: if fewer than 1000 operations to undo, use reverse delta
    // Otherwise, forward replay might be more efficient
    if (ops < 1000) {
        std::cout << "[TimeTravelEngine] Few operations (~" << ops << ") - choosing REVERSE_DELTA" << std::endl;
        return Strategy::REVERSE_DELTA;
    }

    std::cout << "[TimeTravelEngine] Many operations (~" << ops << ") - choosing FORWARD_REPLAY" << std::endl;
    return Strategy::FORWARD_REPLAY;
}

int TimeTravelEngine::EstimateOperationCount(uint64_t target_time, const std::string& db_name) {
    if (!log_manager_) return INT_MAX;

    std::string log_path = log_manager_->GetLogFilePath(db_name);
    std::ifstream log_file(log_path, std::ios::binary | std::ios::in);
    if (!log_file.is_open()) return INT_MAX;

    // Quick scan to estimate operations after target_time
    int count = 0;
    LogRecord record(0, 0, LogRecordType::INVALID);

    while (ReadLogRecord(log_file, record)) {
        if (record.timestamp_ > target_time && record.IsDataModification()) {
            count++;
        }
    }

    log_file.close();
    return count;
}

// ============================================================================
// REVERSE DELTA IMPLEMENTATION
// ============================================================================

std::unique_ptr<TableHeap> TimeTravelEngine::BuildSnapshotReverseDelta(
    const std::string& table_name,
    uint64_t target_time,
    const std::string& db_name) {

    std::cout << "[ReverseDelta] Building snapshot for '" << table_name << "'" << std::endl;
    std::cout << "[ReverseDelta] Target time: " << target_time << std::endl;

    // Step 1: Clone current table state
    auto snapshot = CloneTable(table_name);
    if (!snapshot) {
        std::cerr << "[ReverseDelta] Failed to clone table" << std::endl;
        return nullptr;
    }

    // Count tuples in clone
    int initial_count = 0;
    {
        auto iter = snapshot->Begin(nullptr);
        while (iter != snapshot->End()) {
            initial_count++;
            ++iter;
        }
    }
    std::cout << "[ReverseDelta] Cloned " << initial_count << " tuples from current state" << std::endl;

    // Step 2: Collect operations after target_time
    auto operations = CollectOperationsAfter(table_name, target_time, db_name);

    if (operations.empty()) {
        std::cout << "[ReverseDelta] No operations to undo - snapshot is current state" << std::endl;
        return snapshot;
    }

    std::cout << "[ReverseDelta] Found " << operations.size() << " operations to undo" << std::endl;

    // Step 3: Apply inverse operations in reverse order (newest first)
    auto* table_info = catalog_->GetTable(table_name);
    int undo_count = 0;

    for (const auto& op : operations) {
        ApplyInverseOperation(snapshot.get(), op, table_info);
        undo_count++;

        // Progress logging for large undo sets
        if (undo_count % 100 == 0) {
            std::cout << "[ReverseDelta] Undone " << undo_count << "/" << operations.size() << " operations" << std::endl;
        }
    }

    // Count final tuples
    int final_count = 0;
    {
        auto iter = snapshot->Begin(nullptr);
        while (iter != snapshot->End()) {
            final_count++;
            ++iter;
        }
    }

    std::cout << "[ReverseDelta] Complete. Undone " << undo_count << " operations. "
              << "Tuples: " << initial_count << " -> " << final_count << std::endl;

    return snapshot;
}

std::vector<TimeTravelEngine::InverseOperation> TimeTravelEngine::CollectOperationsAfter(
    const std::string& table_name,
    uint64_t target_time,
    const std::string& db_name) {

    std::vector<InverseOperation> operations;

    if (!log_manager_) return operations;

    std::string log_path = log_manager_->GetLogFilePath(db_name);
    std::ifstream log_file(log_path, std::ios::binary | std::ios::in);
    if (!log_file.is_open()) {
        std::cerr << "[ReverseDelta] Cannot open log file: " << log_path << std::endl;
        return operations;
    }

    auto* table_info = catalog_->GetTable(table_name);
    if (!table_info) {
        std::cerr << "[ReverseDelta] Table not in catalog: " << table_name << std::endl;
        return operations;
    }

    LogRecord record(0, 0, LogRecordType::INVALID);

    while (ReadLogRecord(log_file, record)) {
        // Only collect operations AFTER target_time
        if (record.timestamp_ <= target_time) {
            continue;
        }

        // Only collect operations for this table
        if (record.table_name_ != table_name) {
            continue;
        }

        // Only collect data modification operations
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
                // To undo INSERT, we DELETE the inserted values
                op.values_to_delete = ParseTupleString(record.new_value_.ToString(), table_info);
                break;

            case LogRecordType::MARK_DELETE:
            case LogRecordType::APPLY_DELETE:
                // To undo DELETE, we INSERT the deleted values
                op.values_to_insert = ParseTupleString(record.old_value_.ToString(), table_info);
                break;

            case LogRecordType::UPDATE:
                // To undo UPDATE, we replace new values with old values
                op.old_values = ParseTupleString(record.old_value_.ToString(), table_info);
                op.new_values = ParseTupleString(record.new_value_.ToString(), table_info);
                break;

            default:
                continue;
        }

        operations.push_back(op);
    }

    log_file.close();

    // Sort by LSN descending (newest first) - this is critical for correct undo order
    std::sort(operations.begin(), operations.end(),
        [](const InverseOperation& a, const InverseOperation& b) {
            return a.lsn > b.lsn;
        });

    return operations;
}

void TimeTravelEngine::ApplyInverseOperation(
    TableHeap* heap,
    const InverseOperation& op,
    const TableMetadata* table_info) {

    if (!heap || !table_info) return;

    switch (op.original_type) {
        case LogRecordType::INSERT: {
            // UNDO INSERT = DELETE the inserted row
            if (op.values_to_delete.empty()) return;

            auto iter = heap->Begin(nullptr);
            while (iter != heap->End()) {
                if (TupleMatches(*iter, op.values_to_delete, table_info)) {
                    heap->MarkDelete(iter.GetRID(), nullptr);
                    break;
                }
                ++iter;
            }
            break;
        }

        case LogRecordType::MARK_DELETE:
        case LogRecordType::APPLY_DELETE: {
            // UNDO DELETE = Re-INSERT the deleted row
            if (op.values_to_insert.empty()) return;

            if (op.values_to_insert.size() == table_info->schema_.GetColumnCount()) {
                Tuple tuple(op.values_to_insert, table_info->schema_);
                RID rid;
                heap->InsertTuple(tuple, &rid, nullptr);
            }
            break;
        }

        case LogRecordType::UPDATE: {
            // UNDO UPDATE = Find row with new values, replace with old values
            if (op.new_values.empty() || op.old_values.empty()) return;

            auto iter = heap->Begin(nullptr);
            while (iter != heap->End()) {
                if (TupleMatches(*iter, op.new_values, table_info)) {
                    // Delete current (new) values
                    heap->MarkDelete(iter.GetRID(), nullptr);

                    // Insert old values
                    if (op.old_values.size() == table_info->schema_.GetColumnCount()) {
                        Tuple old_tuple(op.old_values, table_info->schema_);
                        RID rid;
                        heap->InsertTuple(old_tuple, &rid, nullptr);
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

// ============================================================================
// FORWARD REPLAY IMPLEMENTATION (Fallback)
// ============================================================================

std::unique_ptr<TableHeap> TimeTravelEngine::BuildSnapshotForwardReplay(
    const std::string& table_name,
    uint64_t target_time,
    const std::string& db_name) {

    std::cout << "[ForwardReplay] Building snapshot for '" << table_name << "'" << std::endl;

    auto* table_info = catalog_->GetTable(table_name);
    if (!table_info) {
        std::cerr << "[ForwardReplay] Table not found: " << table_name << std::endl;
        return nullptr;
    }

    auto snapshot = std::make_unique<TableHeap>(bpm_, nullptr);

    if (!log_manager_) {
        std::cerr << "[ForwardReplay] No log manager" << std::endl;
        return snapshot;
    }

    std::string log_path = log_manager_->GetLogFilePath(db_name);
    std::ifstream log_file(log_path, std::ios::binary | std::ios::in);
    if (!log_file.is_open()) {
        std::cerr << "[ForwardReplay] Cannot open log: " << log_path << std::endl;
        return snapshot;
    }

    int record_count = 0;
    LogRecord record(0, 0, LogRecordType::INVALID);

    while (ReadLogRecord(log_file, record)) {
        // Stop at target time
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
                    record_count++;
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
                            Tuple new_tuple(new_vals, table_info->schema_);
                            RID rid;
                            snapshot->InsertTuple(new_tuple, &rid, nullptr);
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
    std::cout << "[ForwardReplay] Replayed " << record_count << " records" << std::endl;

    return snapshot;
}

// ============================================================================
// ATOMIC RECOVERY IMPLEMENTATION
// ============================================================================

TimeTravelEngine::TimeTravelResult TimeTravelEngine::RecoverToReverseDelta(
    uint64_t target_time,
    const std::string& db_name) {

    std::cout << "[RecoverTo] Starting ATOMIC recovery using REVERSE DELTA" << std::endl;

    auto all_tables = catalog_->GetAllTables();
    if (all_tables.empty()) {
        return TimeTravelResult::Error("No tables to recover");
    }

    int total_records = 0;

    // Phase 1: Build all snapshots in memory
    // This phase can fail safely - no data is modified yet
    std::map<std::string, std::unique_ptr<TableHeap>> snapshots;
    std::map<std::string, std::vector<Tuple>> snapshot_data;

    for (auto* table_info : all_tables) {
        if (!table_info || !table_info->table_heap_) continue;

        std::string table_name = table_info->name_;

        // Skip system tables
        if (table_name == "chronos_users" || table_name.find("sys_") == 0) {
            continue;
        }

        std::cout << "[RecoverTo] Building snapshot for table: " << table_name << std::endl;

        auto snapshot = BuildSnapshotReverseDelta(table_name, target_time, db_name);
        if (!snapshot) {
            return TimeTravelResult::Error("Failed to build snapshot for table: " + table_name);
        }

        // Extract tuple data into memory (for atomic swap)
        std::vector<Tuple> tuples;
        auto iter = snapshot->Begin(nullptr);
        while (iter != snapshot->End()) {
            tuples.push_back(*iter);
            ++iter;
        }

        snapshot_data[table_name] = std::move(tuples);
        total_records += snapshot_data[table_name].size();

        std::cout << "[RecoverTo] Snapshot for '" << table_name << "' has "
                  << snapshot_data[table_name].size() << " tuples" << std::endl;
    }

    // Phase 2: Atomic swap
    // Clear all tables and insert snapshot data
    // This is the "point of no return"

    std::cout << "[RecoverTo] Phase 2: Atomic swap begins" << std::endl;

    try {
        for (const auto& [table_name, tuples] : snapshot_data) {
            auto* table_info = catalog_->GetTable(table_name);
            if (!table_info || !table_info->table_heap_) continue;

            TableHeap* heap = table_info->table_heap_.get();

            // Clear existing data
            int deleted = 0;
            page_id_t page_id = heap->GetFirstPageId();
            while (page_id != INVALID_PAGE_ID) {
                Page* raw_page = bpm_->FetchPage(page_id);
                if (!raw_page) break;

                auto* table_page = reinterpret_cast<TablePage*>(raw_page->GetData());
                uint32_t tuple_count = table_page->GetTupleCount();

                for (uint32_t slot = 0; slot < tuple_count; slot++) {
                    RID rid(page_id, slot);
                    if (heap->MarkDelete(rid, nullptr)) {
                        deleted++;
                    }
                }

                page_id_t next = table_page->GetNextPageId();
                bpm_->UnpinPage(page_id, true);
                page_id = next;
            }

            std::cout << "[RecoverTo] Cleared " << deleted << " tuples from '" << table_name << "'" << std::endl;

            // Insert snapshot data
            int inserted = 0;
            for (const auto& tuple : tuples) {
                RID rid;
                if (heap->InsertTuple(tuple, &rid, nullptr)) {
                    inserted++;
                }
            }

            std::cout << "[RecoverTo] Inserted " << inserted << " tuples into '" << table_name << "'" << std::endl;

            // Reset checkpoint LSN since table state has changed
            table_info->SetCheckpointLSN(LogRecord::INVALID_LSN);
        }

        // Flush to ensure persistence
        bpm_->FlushAllPages();

        // Take checkpoint to establish new baseline
        if (checkpoint_mgr_) {
            checkpoint_mgr_->BeginCheckpoint();
        }

    } catch (const std::exception& e) {
        return TimeTravelResult::Error(std::string("Recovery failed during swap: ") + e.what());
    }

    std::cout << "[RecoverTo] ATOMIC recovery complete" << std::endl;

    return TimeTravelResult::Success(total_records, 0, Strategy::REVERSE_DELTA);
}

TimeTravelEngine::TimeTravelResult TimeTravelEngine::RecoverToForwardReplay(
    uint64_t target_time,
    const std::string& db_name) {

    std::cout << "[RecoverTo] Starting ATOMIC recovery using FORWARD REPLAY" << std::endl;

    // Same logic as reverse delta, but using forward replay for snapshots
    auto all_tables = catalog_->GetAllTables();
    if (all_tables.empty()) {
        return TimeTravelResult::Error("No tables to recover");
    }

    int total_records = 0;
    std::map<std::string, std::vector<Tuple>> snapshot_data;

    // Phase 1: Build snapshots
    for (auto* table_info : all_tables) {
        if (!table_info || !table_info->table_heap_) continue;

        std::string table_name = table_info->name_;
        if (table_name == "chronos_users" || table_name.find("sys_") == 0) {
            continue;
        }

        auto snapshot = BuildSnapshotForwardReplay(table_name, target_time, db_name);
        if (!snapshot) {
            return TimeTravelResult::Error("Failed to build snapshot for: " + table_name);
        }

        std::vector<Tuple> tuples;
        auto iter = snapshot->Begin(nullptr);
        while (iter != snapshot->End()) {
            tuples.push_back(*iter);
            ++iter;
        }

        snapshot_data[table_name] = std::move(tuples);
        total_records += snapshot_data[table_name].size();
    }

    // Phase 2: Atomic swap (same as reverse delta)
    try {
        for (const auto& [table_name, tuples] : snapshot_data) {
            auto* table_info = catalog_->GetTable(table_name);
            if (!table_info || !table_info->table_heap_) continue;

            TableHeap* heap = table_info->table_heap_.get();

            // Clear existing data
            page_id_t page_id = heap->GetFirstPageId();
            while (page_id != INVALID_PAGE_ID) {
                Page* raw_page = bpm_->FetchPage(page_id);
                if (!raw_page) break;

                auto* table_page = reinterpret_cast<TablePage*>(raw_page->GetData());
                uint32_t tuple_count = table_page->GetTupleCount();

                for (uint32_t slot = 0; slot < tuple_count; slot++) {
                    RID rid(page_id, slot);
                    heap->MarkDelete(rid, nullptr);
                }

                page_id_t next = table_page->GetNextPageId();
                bpm_->UnpinPage(page_id, true);
                page_id = next;
            }

            // Insert snapshot data
            for (const auto& tuple : tuples) {
                RID rid;
                heap->InsertTuple(tuple, &rid, nullptr);
            }

            table_info->SetCheckpointLSN(LogRecord::INVALID_LSN);
        }

        bpm_->FlushAllPages();

        if (checkpoint_mgr_) {
            checkpoint_mgr_->BeginCheckpoint();
        }

    } catch (const std::exception& e) {
        return TimeTravelResult::Error(std::string("Recovery failed: ") + e.what());
    }

    return TimeTravelResult::Success(total_records, 0, Strategy::FORWARD_REPLAY);
}

// ============================================================================
// HELPER METHODS
// ============================================================================

std::unique_ptr<TableHeap> TimeTravelEngine::CloneTable(const std::string& table_name) {
    auto* table_info = catalog_->GetTable(table_name);
    if (!table_info || !table_info->table_heap_) {
        return nullptr;
    }

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

    // Pad with defaults if needed
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

bool TimeTravelEngine::TupleMatches(
    const Tuple& tuple,
    const std::vector<Value>& vals,
    const TableMetadata* table_info) const {

    if (!table_info || vals.size() != table_info->schema_.GetColumnCount()) {
        return false;
    }

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
                int32_t dummy;
                log_file.read(reinterpret_cast<char*>(&dummy), sizeof(int32_t));
                log_file.read(reinterpret_cast<char*>(&dummy), sizeof(int32_t));
                log_file.read(reinterpret_cast<char*>(&dummy), sizeof(int32_t));
            }
            int32_t dpt_size = 0;
            log_file.read(reinterpret_cast<char*>(&dpt_size), sizeof(int32_t));
            for (int32_t i = 0; i < dpt_size && i < 10000; i++) {
                int32_t dummy;
                log_file.read(reinterpret_cast<char*>(&dummy), sizeof(int32_t));
                log_file.read(reinterpret_cast<char*>(&dummy), sizeof(int32_t));
            }
            break;
        }
        default:
            break;
    }

    // Read CRC
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
        try {
            return Value(type, std::stoi(s_val));
        } catch (...) {
            return Value(type, 0);
        }
    }
    if (type == TypeId::DECIMAL) {
        try {
            return Value(type, std::stod(s_val));
        } catch (...) {
            return Value(type, 0.0);
        }
    }
    return Value(type, s_val);
}

} // namespace chronosdb
