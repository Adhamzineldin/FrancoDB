/**
 * time_travel_engine.cpp
 *
 * OPTIMIZED Time Travel Engine with Buffer Pool Integration
 *
 * Key Features:
 * 1. Page-cached log reading (integrates with memory budget)
 * 2. In-memory snapshot building (no buffer pool I/O during construction)
 * 3. Hash-based tuple lookup O(1) instead of O(n)
 * 4. Safety limits to prevent infinite loops
 * 5. Production-grade logging
 */

#include "recovery/time_travel_engine.h"
#include "recovery/log_page_reader.h"
#include "storage/table/in_memory_table_heap.h"
#include "common/config.h"
#include "common/logger.h"
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <unordered_map>

namespace chronosdb {

// Component name for logging
static constexpr const char* LOG_COMPONENT = "TimeTravel";

// Safety limits - these prevent infinite loops from corrupted logs
// MAX_RECORD_SIZE: Single record can't exceed 10MB (protects against corrupted size field)
// PROGRESS_INTERVAL: Log progress for monitoring long operations (every 10k for visibility)
// Note: No limit on total records - we rely on proper EOF detection via LogPageReader
static constexpr size_t MAX_RECORD_SIZE = 10000000;  // 10MB max single record
static constexpr size_t PROGRESS_INTERVAL = 10000;   // Log progress every 10k records (was 100k)

// ============================================================================
// HELPERS
// ============================================================================

static std::string MakeTupleKey(const std::vector<Value>& vals) {
    std::string key;
    key.reserve(256);
    for (size_t i = 0; i < vals.size(); i++) {
        key += vals[i].ToString();
        if (i < vals.size() - 1) key += '\x1F';
    }
    return key;
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
// BUILD SNAPSHOT - Main Entry Point
// ============================================================================

std::unique_ptr<TableHeap> TimeTravelEngine::BuildSnapshot(
    const std::string& table_name,
    uint64_t target_time,
    const std::string& db_name,
    Strategy strategy) {

    auto start = std::chrono::high_resolution_clock::now();
    LOG_DEBUG(LOG_COMPONENT, "BuildSnapshot started for table '%s'", table_name.c_str());

    std::string actual_db = db_name;
    if (actual_db.empty() && log_manager_) {
        actual_db = log_manager_->GetCurrentDatabase();
    }

    auto* table_info = catalog_->GetTable(table_name);
    if (!table_info) {
        LOG_ERROR(LOG_COMPONENT, "Table not found: %s", table_name.c_str());
        return nullptr;
    }

    uint64_t current_time = LogRecord::GetCurrentTimestamp();

    // Fast path: current or future = just clone
    if (target_time >= current_time) {
        LOG_DEBUG(LOG_COMPONENT, "Target time is current/future, cloning live table");
        return CloneTable(table_name);
    }

    // Strategy selection: choose between forward replay and reverse delta
    // Forward replay is better for distant past (fewer ops to replay)
    // Reverse delta is better for recent past (fewer ops to undo)
    Strategy chosen = strategy;
    if (strategy == Strategy::AUTO) {
        chosen = ChooseStrategy(target_time, actual_db);
    }

    std::unique_ptr<TableHeap> result;
    if (chosen == Strategy::FORWARD_REPLAY) {
        LOG_DEBUG(LOG_COMPONENT, "Using FORWARD_REPLAY strategy");
        result = BuildSnapshotForwardReplay(table_name, target_time, actual_db);
    } else {
        LOG_DEBUG(LOG_COMPONENT, "Using REVERSE_DELTA strategy");
        result = BuildSnapshotReverseDelta(table_name, target_time, actual_db);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (ms > 100) {
        LOG_INFO(LOG_COMPONENT, "Snapshot for '%s' built in %lldms",
                 table_name.c_str(), static_cast<long long>(ms));
    }

    return result;
}

// ============================================================================
// IN-MEMORY SNAPSHOT (FAST - BYPASSES BUFFER POOL)
// ============================================================================

std::unique_ptr<InMemoryTableHeap> TimeTravelEngine::BuildSnapshotInMemory(
    const std::string& table_name,
    uint64_t target_time,
    const std::string& db_name) {

    auto start = std::chrono::high_resolution_clock::now();
    LOG_DEBUG(LOG_COMPONENT, "BuildSnapshotInMemory started for table '%s'", table_name.c_str());

    std::string actual_db = db_name;
    if (actual_db.empty() && log_manager_) {
        actual_db = log_manager_->GetCurrentDatabase();
    }

    auto* table_info = catalog_->GetTable(table_name);
    if (!table_info) {
        LOG_ERROR(LOG_COMPONENT, "Table not found: %s", table_name.c_str());
        return nullptr;
    }

    uint64_t current_time = LogRecord::GetCurrentTimestamp();

    // Fast path: current or future = clone live table to memory
    if (target_time >= current_time) {
        LOG_DEBUG(LOG_COMPONENT, "Target time is current/future, cloning live table to memory");
        auto snapshot = std::make_unique<InMemoryTableHeap>();
        if (table_info->table_heap_) {
            auto iter = table_info->table_heap_->Begin(nullptr);
            while (iter != table_info->table_heap_->End()) {
                Tuple tuple = *iter;
                RID rid;
                snapshot->InsertTuple(tuple, &rid, nullptr);
                ++iter;
            }
        }
        return snapshot;
    }

    // Choose strategy based on operation count (not just time)
    Strategy chosen = ChooseStrategy(target_time, actual_db);

    std::unique_ptr<InMemoryTableHeap> result;
    if (chosen == Strategy::FORWARD_REPLAY) {
        LOG_INFO(LOG_COMPONENT, "Using FORWARD_REPLAY strategy (in-memory)");
        result = BuildSnapshotForwardReplayInMemory(table_name, target_time, actual_db);
    } else {
        LOG_INFO(LOG_COMPONENT, "Using REVERSE_DELTA strategy (in-memory)");
        result = BuildSnapshotReverseDeltaInMemory(table_name, target_time, actual_db);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (result) {
        LOG_INFO(LOG_COMPONENT, "In-memory snapshot for '%s' built in %lldms (%zu rows)",
                 table_name.c_str(), static_cast<long long>(ms), result->GetTupleCount());
    }

    return result;
}

// ============================================================================
// FORWARD REPLAY - IN-MEMORY (NO BUFFER POOL)
// ============================================================================

std::unique_ptr<InMemoryTableHeap> TimeTravelEngine::BuildSnapshotForwardReplayInMemory(
    const std::string& table_name,
    uint64_t target_time,
    const std::string& db_name) {

    auto* table_info = catalog_->GetTable(table_name);
    if (!table_info) return nullptr;

    const Schema& schema = table_info->schema_;

    // =========================================================================
    // ORDER-PRESERVING DATA STRUCTURE
    // =========================================================================
    // We use a vector + hash index instead of unordered_multimap to preserve
    // insertion order. This is critical for correct query results (ORDER BY, etc.)
    //
    // ordered_rows: Stores all rows in insertion order (deleted rows marked, not removed)
    // key_to_indices: Fast O(1) lookup by key for DELETE/UPDATE operations
    struct RowEntry {
        std::string key;
        std::vector<Value> values;
        bool deleted = false;
    };
    std::vector<RowEntry> ordered_rows;
    ordered_rows.reserve(10000);

    // Hash map: key -> indices in ordered_rows (for fast lookup)
    std::unordered_multimap<std::string, size_t> key_to_indices;
    key_to_indices.reserve(10000);

    std::string log_path = log_manager_->GetLogFilePath(db_name);
    LOG_INFO(LOG_COMPONENT, "Forward replay: opening log file '%s'", log_path.c_str());

    LogPageReader reader(64);
    if (!reader.Open(log_path)) {
        LOG_ERROR(LOG_COMPONENT, "Forward replay: CANNOT open log file '%s' - time travel requires WAL log!",
                  log_path.c_str());
        return nullptr;
    }

    auto scan_start = std::chrono::high_resolution_clock::now();
    size_t records_processed = 0;
    size_t table_ops = 0;

    LogRecord record(0, 0, LogRecordType::INVALID);
    while (ReadLogRecordFromReader(reader, record)) {
        records_processed++;

        if (records_processed % PROGRESS_INTERVAL == 0) {
            LOG_INFO(LOG_COMPONENT, "Forward replay (in-memory): %zu records processed, %zu table ops",
                     records_processed, table_ops);
        }

        // Stop at target time
        if (record.timestamp_ > target_time) {
            break;
        }

        if (record.table_name_ != table_name) {
            continue;
        }

        table_ops++;

        switch (record.log_record_type_) {
            case LogRecordType::INSERT: {
                auto vals = ParseTupleString(record.new_value_.ToString(), table_info);
                if (!vals.empty()) {
                    std::string key = MakeTupleKey(vals);
                    size_t idx = ordered_rows.size();
                    ordered_rows.push_back({key, std::move(vals), false});
                    key_to_indices.emplace(key, idx);
                }
                break;
            }
            case LogRecordType::MARK_DELETE:
            case LogRecordType::APPLY_DELETE: {
                auto vals = ParseTupleString(record.old_value_.ToString(), table_info);
                if (!vals.empty()) {
                    std::string key = MakeTupleKey(vals);
                    // Find and mark as deleted (don't remove to preserve order)
                    auto range = key_to_indices.equal_range(key);
                    for (auto it = range.first; it != range.second; ++it) {
                        size_t idx = it->second;
                        if (idx < ordered_rows.size() && !ordered_rows[idx].deleted) {
                            ordered_rows[idx].deleted = true;
                            key_to_indices.erase(it);
                            break;  // Delete first matching non-deleted row
                        }
                    }
                }
                break;
            }
            case LogRecordType::UPDATE: {
                auto old_vals = ParseTupleString(record.old_value_.ToString(), table_info);
                auto new_vals = ParseTupleString(record.new_value_.ToString(), table_info);

                // Mark old row as deleted
                if (!old_vals.empty()) {
                    std::string old_key = MakeTupleKey(old_vals);
                    auto range = key_to_indices.equal_range(old_key);
                    for (auto it = range.first; it != range.second; ++it) {
                        size_t idx = it->second;
                        if (idx < ordered_rows.size() && !ordered_rows[idx].deleted) {
                            ordered_rows[idx].deleted = true;
                            key_to_indices.erase(it);
                            break;
                        }
                    }
                }

                // Insert new row (preserves order - UPDATE moves row to "end" logically)
                if (!new_vals.empty()) {
                    std::string new_key = MakeTupleKey(new_vals);
                    size_t idx = ordered_rows.size();
                    ordered_rows.push_back({new_key, std::move(new_vals), false});
                    key_to_indices.emplace(new_key, idx);
                }
                break;
            }
            default:
                break;
        }
    }
    reader.Close();

    // Count active (non-deleted) rows
    size_t active_rows = 0;
    for (const auto& row : ordered_rows) {
        if (!row.deleted) active_rows++;
    }

    auto scan_end = std::chrono::high_resolution_clock::now();
    auto scan_ms = std::chrono::duration_cast<std::chrono::milliseconds>(scan_end - scan_start).count();

    LOG_INFO(LOG_COMPONENT, "Forward replay: scanned %zu records (%zu table ops) in %lldms, result has %zu rows",
             records_processed, table_ops, static_cast<long long>(scan_ms), active_rows);

    // =========================================================================
    // MATERIALIZE: Convert ordered_rows to InMemoryTableHeap (preserves order!)
    // =========================================================================
    auto mat_start = std::chrono::high_resolution_clock::now();
    auto snapshot = std::make_unique<InMemoryTableHeap>();
    snapshot->Reserve(active_rows);

    size_t materialized = 0;
    for (const auto& row : ordered_rows) {
        if (row.deleted) continue;  // Skip deleted rows

        if (row.values.size() == schema.GetColumnCount()) {
            Tuple tuple(row.values, schema);
            RID rid;
            snapshot->InsertTuple(std::move(tuple), &rid, nullptr);
            materialized++;

            if (materialized % 100000 == 0) {
                LOG_INFO(LOG_COMPONENT, "Forward replay: built %zu / %zu tuples",
                         materialized, active_rows);
            }
        }
    }

    auto mat_end = std::chrono::high_resolution_clock::now();
    auto mat_ms = std::chrono::duration_cast<std::chrono::milliseconds>(mat_end - mat_start).count();

    LOG_INFO(LOG_COMPONENT, "Forward replay: built %zu tuples in %lldms (%.0f rows/sec)",
             materialized, static_cast<long long>(mat_ms),
             mat_ms > 0 ? (materialized * 1000.0 / mat_ms) : 0.0);

    return snapshot;
}

// ============================================================================
// REVERSE DELTA - IN-MEMORY (NO BUFFER POOL)
// ============================================================================

std::unique_ptr<InMemoryTableHeap> TimeTravelEngine::BuildSnapshotReverseDeltaInMemory(
    const std::string& table_name,
    uint64_t target_time,
    const std::string& db_name) {

    auto* table_info = catalog_->GetTable(table_name);
    if (!table_info || !table_info->table_heap_) {
        LOG_ERROR(LOG_COMPONENT, "Table or heap not found: %s", table_name.c_str());
        return nullptr;
    }

    const Schema& schema = table_info->schema_;

    // PHASE 1: Load current table into memory
    LOG_DEBUG(LOG_COMPONENT, "Reverse delta (in-memory): loading current table state");

    struct RowEntry {
        std::string key;
        std::vector<Value> values;
        bool deleted = false;
    };
    std::vector<RowEntry> ordered_rows;
    ordered_rows.reserve(10000);

    std::unordered_multimap<std::string, size_t> key_to_indices;
    key_to_indices.reserve(10000);

    size_t row_count = 0;
    auto iter = table_info->table_heap_->Begin(nullptr);
    while (iter != table_info->table_heap_->End()) {
        const Tuple& tuple = *iter;
        std::vector<Value> vals;
        vals.reserve(schema.GetColumnCount());
        for (uint32_t i = 0; i < schema.GetColumnCount(); i++) {
            vals.push_back(tuple.GetValue(schema, i));
        }
        std::string key = MakeTupleKey(vals);

        size_t idx = ordered_rows.size();
        ordered_rows.push_back({key, std::move(vals), false});
        key_to_indices.emplace(key, idx);

        ++iter;
        row_count++;
    }

    LOG_DEBUG(LOG_COMPONENT, "Loaded %zu rows from current table", row_count);

    // PHASE 2: Read log and collect operations to undo
    std::string log_path = log_manager_->GetLogFilePath(db_name);
    LOG_INFO(LOG_COMPONENT, "Reverse delta: opening log file '%s'", log_path.c_str());

    LogPageReader reader(64);
    if (!reader.Open(log_path)) {
        // BUG FIX: Don't silently return current state - log file is required for time travel!
        LOG_ERROR(LOG_COMPONENT, "Reverse delta: CANNOT open log file '%s' - time travel requires WAL log!",
                  log_path.c_str());
        return nullptr;  // Return nullptr to indicate failure, not current state
    }

    {
        // NOTE: We intentionally start from the BEGINNING of the log, not from estimated offset
        // Reason: Seeking to estimated offset lands mid-record, causing "Invalid record size" errors
        // The log must be read sequentially from a known record boundary (position 0)
        // We simply skip records with timestamp <= target_time
        reader.Seek(0);

        std::vector<InverseOperation> ops_to_undo;
        ops_to_undo.reserve(1000);

        size_t records_scanned = 0;
        size_t records_collected = 0;

        LogRecord record(0, 0, LogRecordType::INVALID);
        while (ReadLogRecordFromReader(reader, record)) {
            records_scanned++;

            if (records_scanned % PROGRESS_INTERVAL == 0) {
                LOG_INFO(LOG_COMPONENT, "Reverse delta: scanned %zu records, collected %zu ops",
                         records_scanned, records_collected);
            }

            if (record.timestamp_ <= target_time) {
                continue;
            }

            if (record.table_name_ != table_name) {
                continue;
            }

            if (!record.IsDataModification()) {
                continue;
            }

            InverseOperation op;
            op.original_type = record.log_record_type_;
            op.lsn = record.lsn_;
            op.timestamp = record.timestamp_;

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
            records_collected++;
        }
        reader.Close();

        LOG_DEBUG(LOG_COMPONENT, "Collected %zu ops to undo", ops_to_undo.size());

        // PHASE 3: Apply undos in reverse order
        if (!ops_to_undo.empty()) {
            std::sort(ops_to_undo.begin(), ops_to_undo.end(),
                [](const InverseOperation& a, const InverseOperation& b) {
                    return a.lsn > b.lsn;
                });

            for (const auto& op : ops_to_undo) {
                switch (op.original_type) {
                    case LogRecordType::INSERT: {
                        if (!op.values_to_delete.empty()) {
                            std::string key = MakeTupleKey(op.values_to_delete);
                            auto range = key_to_indices.equal_range(key);
                            for (auto it = range.first; it != range.second; ++it) {
                                size_t idx = it->second;
                                if (!ordered_rows[idx].deleted) {
                                    ordered_rows[idx].deleted = true;
                                    break;
                                }
                            }
                        }
                        break;
                    }
                    case LogRecordType::MARK_DELETE:
                    case LogRecordType::APPLY_DELETE: {
                        if (!op.values_to_insert.empty()) {
                            std::string key = MakeTupleKey(op.values_to_insert);
                            size_t idx = ordered_rows.size();
                            ordered_rows.push_back({key, op.values_to_insert, false});
                            key_to_indices.emplace(key, idx);
                        }
                        break;
                    }
                    case LogRecordType::UPDATE: {
                        if (!op.new_values.empty() && !op.old_values.empty()) {
                            std::string new_key = MakeTupleKey(op.new_values);
                            auto range = key_to_indices.equal_range(new_key);
                            for (auto it = range.first; it != range.second; ++it) {
                                size_t idx = it->second;
                                if (!ordered_rows[idx].deleted) {
                                    ordered_rows[idx].values = op.old_values;
                                    ordered_rows[idx].key = MakeTupleKey(op.old_values);
                                    key_to_indices.erase(it);
                                    key_to_indices.emplace(ordered_rows[idx].key, idx);
                                    break;
                                }
                            }
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    }

    // PHASE 4: Build InMemoryTableHeap directly (O(n), no buffer pool)
    size_t active_rows = 0;
    for (const auto& row : ordered_rows) {
        if (!row.deleted) active_rows++;
    }

    LOG_INFO(LOG_COMPONENT, "Reverse delta: building result with %zu rows", active_rows);

    auto snapshot = std::make_unique<InMemoryTableHeap>();
    snapshot->Reserve(active_rows);

    size_t built = 0;
    for (const auto& row : ordered_rows) {
        if (!row.deleted && row.values.size() == schema.GetColumnCount()) {
            Tuple tuple(row.values, schema);
            RID rid;
            snapshot->InsertTuple(std::move(tuple), &rid, nullptr);
            built++;

            if (built % 100000 == 0) {
                LOG_INFO(LOG_COMPONENT, "Reverse delta: built %zu / %zu tuples", built, active_rows);
            }
        }
    }

    LOG_INFO(LOG_COMPONENT, "Reverse delta: complete (%zu rows)", built);
    return snapshot;
}

// ============================================================================
// REVERSE DELTA - FULLY OPTIMIZED (ORDER-PRESERVING)
// ============================================================================

std::unique_ptr<TableHeap> TimeTravelEngine::BuildSnapshotReverseDelta(
    const std::string& table_name,
    uint64_t target_time,
    const std::string& db_name) {

    auto* table_info = catalog_->GetTable(table_name);
    if (!table_info || !table_info->table_heap_) {
        LOG_ERROR(LOG_COMPONENT, "Table or heap not found: %s", table_name.c_str());
        return nullptr;
    }

    const Schema& schema = table_info->schema_;

    // =========================================================================
    // PHASE 1: Load current table into MEMORY (preserving order)
    // =========================================================================
    LOG_DEBUG(LOG_COMPONENT, "Phase 1: Loading current table state into memory");

    // Use vector for ordered storage + multimap for fast key lookups
    // Each entry: (key, values, deleted_flag)
    struct RowEntry {
        std::string key;
        std::vector<Value> values;
        bool deleted = false;
    };
    std::vector<RowEntry> ordered_rows;
    ordered_rows.reserve(10000);

    // Map from key to indices in ordered_rows (for fast lookup during undo)
    std::unordered_multimap<std::string, size_t> key_to_indices;
    key_to_indices.reserve(10000);

    size_t row_count = 0;
    auto iter = table_info->table_heap_->Begin(nullptr);
    while (iter != table_info->table_heap_->End()) {
        const Tuple& tuple = *iter;
        std::vector<Value> vals;
        vals.reserve(schema.GetColumnCount());
        for (uint32_t i = 0; i < schema.GetColumnCount(); i++) {
            vals.push_back(tuple.GetValue(schema, i));
        }
        std::string key = MakeTupleKey(vals);

        size_t idx = ordered_rows.size();
        ordered_rows.push_back({key, std::move(vals), false});
        key_to_indices.emplace(key, idx);

        ++iter;
        row_count++;
    }

    LOG_DEBUG(LOG_COMPONENT, "Loaded %zu rows from current table", row_count);

    // =========================================================================
    // PHASE 2: Read log and collect operations to undo
    // =========================================================================
    LOG_DEBUG(LOG_COMPONENT, "Phase 2: Scanning log for operations after target time");

    std::string log_path = log_manager_->GetLogFilePath(db_name);

    // Use page-cached reader (integrates with memory budget)
    LogPageReader reader(64); // 64 pages = 256KB cache
    if (!reader.Open(log_path)) {
        // No log file = current state IS the snapshot
        LOG_DEBUG(LOG_COMPONENT, "No log file found, using current state");
        goto materialize;
    }

    {
        // NOTE: We intentionally start from the BEGINNING of the log
        // Reason: Seeking to estimated offset lands mid-record, causing "Invalid record size" errors
        // The log must be read sequentially from a known record boundary (position 0)
        // We simply skip records with timestamp <= target_time during the scan
        reader.Seek(0);

        std::vector<InverseOperation> ops_to_undo;
        ops_to_undo.reserve(1000);

        size_t records_scanned = 0;
        size_t records_collected = 0;

        LogRecord record(0, 0, LogRecordType::INVALID);
        while (ReadLogRecordFromReader(reader, record)) {
            records_scanned++;

            // Progress logging (INFO level so users can see it's working)
            if (records_scanned % PROGRESS_INTERVAL == 0) {
                LOG_INFO(LOG_COMPONENT, "Reverse delta: scanned %zu records, collected %zu ops to undo",
                         records_scanned, records_collected);
            }

            // Skip records at or before target time
            // (binary search gets us close, but we may need to skip a few more)
            if (record.timestamp_ <= target_time) {
                continue;
            }

            // Only this table
            if (record.table_name_ != table_name) {
                continue;
            }

            // Only data modifications
            if (!record.IsDataModification()) {
                continue;
            }

            InverseOperation op;
            op.original_type = record.log_record_type_;
            op.lsn = record.lsn_;
            op.timestamp = record.timestamp_;

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
            records_collected++;
        }

        reader.Close();

        LOG_DEBUG(LOG_COMPONENT, "Scanned %zu records, collected %zu ops to undo (cache hit rate: %.1f%%)",
                 records_scanned, records_collected, reader.GetHitRate());

        // =====================================================================
        // PHASE 3: Apply undos in REVERSE order (newest first) - IN MEMORY
        // =====================================================================
        if (!ops_to_undo.empty()) {
            LOG_DEBUG(LOG_COMPONENT, "Phase 3: Applying %zu inverse operations", ops_to_undo.size());

            // Sort by LSN descending
            std::sort(ops_to_undo.begin(), ops_to_undo.end(),
                [](const InverseOperation& a, const InverseOperation& b) {
                    return a.lsn > b.lsn;
                });

            for (const auto& op : ops_to_undo) {
                switch (op.original_type) {
                    case LogRecordType::INSERT: {
                        // Undo INSERT = mark as deleted
                        if (!op.values_to_delete.empty()) {
                            std::string key = MakeTupleKey(op.values_to_delete);
                            auto range = key_to_indices.equal_range(key);
                            for (auto it = range.first; it != range.second; ++it) {
                                size_t idx = it->second;
                                if (!ordered_rows[idx].deleted) {
                                    ordered_rows[idx].deleted = true;
                                    break; // Only delete first matching non-deleted row
                                }
                            }
                        }
                        break;
                    }
                    case LogRecordType::MARK_DELETE:
                    case LogRecordType::APPLY_DELETE: {
                        // Undo DELETE = re-insert at end (maintains relative order)
                        if (!op.values_to_insert.empty()) {
                            std::string key = MakeTupleKey(op.values_to_insert);
                            size_t idx = ordered_rows.size();
                            ordered_rows.push_back({key, op.values_to_insert, false});
                            key_to_indices.emplace(key, idx);
                        }
                        break;
                    }
                    case LogRecordType::UPDATE: {
                        // Undo UPDATE = replace new values with old values
                        if (!op.new_values.empty() && !op.old_values.empty()) {
                            std::string new_key = MakeTupleKey(op.new_values);
                            auto range = key_to_indices.equal_range(new_key);
                            for (auto it = range.first; it != range.second; ++it) {
                                size_t idx = it->second;
                                if (!ordered_rows[idx].deleted) {
                                    // Update the values in place
                                    ordered_rows[idx].values = op.old_values;
                                    ordered_rows[idx].key = MakeTupleKey(op.old_values);
                                    // Update the index
                                    key_to_indices.erase(it);
                                    key_to_indices.emplace(ordered_rows[idx].key, idx);
                                    break;
                                }
                            }
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    }

materialize:
    // =========================================================================
    // PHASE 4: Materialize in-memory table to TableHeap (in original order)
    // =========================================================================
    size_t active_rows = 0;
    for (const auto& row : ordered_rows) {
        if (!row.deleted) active_rows++;
    }

    // For very large tables, warn about materialization time
    if (active_rows > 100000) {
        LOG_INFO(LOG_COMPONENT, "Reverse delta: large snapshot (%zu rows) - materialization may be slow",
                 active_rows);
    }

    auto mat_start = std::chrono::high_resolution_clock::now();
    LOG_INFO(LOG_COMPONENT, "Reverse delta: materializing %zu rows to TableHeap...", active_rows);

    auto snapshot = std::make_unique<TableHeap>(bpm_, nullptr);
    size_t materialized = 0;
    auto last_log = mat_start;

    for (const auto& row : ordered_rows) {
        if (!row.deleted && row.values.size() == schema.GetColumnCount()) {
            Tuple tuple(row.values, schema);
            RID rid;
            snapshot->InsertTuple(tuple, &rid, nullptr);
            materialized++;

            // Progress logging every 10k rows with timing
            if (materialized % PROGRESS_INTERVAL == 0) {
                auto now = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - mat_start).count();
                auto batch_time = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_log).count();
                double rate = (materialized * 1000.0) / (elapsed > 0 ? elapsed : 1);

                LOG_INFO(LOG_COMPONENT, "Reverse delta: materialized %zu / %zu rows (%.0f rows/sec, batch: %lldms)",
                         materialized, active_rows, rate, static_cast<long long>(batch_time));
                last_log = now;
            }
        }
    }

    auto mat_end = std::chrono::high_resolution_clock::now();
    auto mat_ms = std::chrono::duration_cast<std::chrono::milliseconds>(mat_end - mat_start).count();

    LOG_INFO(LOG_COMPONENT, "Reverse delta: materialization complete (%zu rows in %lldms, %.0f rows/sec)",
             materialized, static_cast<long long>(mat_ms),
             mat_ms > 0 ? (materialized * 1000.0 / mat_ms) : 0.0);

    return snapshot;
}

// ============================================================================
// LOG RECORD READING - With Page Cache
// ============================================================================

bool TimeTravelEngine::ReadLogRecordFromReader(LogPageReader& reader, LogRecord& record) {
    // Fixed header: size(4) + lsn(4) + prev_lsn(4) + undo_next(4) + txn_id(4) + timestamp(8) + type(4) = 32 bytes
    int32_t size = 0;
    if (reader.ReadSequential(reinterpret_cast<char*>(&size), sizeof(int32_t)) != sizeof(int32_t)) {
        return false;
    }

    if (size <= 0 || static_cast<size_t>(size) > MAX_RECORD_SIZE) {
        LOG_WARN(LOG_COMPONENT, "Invalid record size: %d", size);
        return false;
    }

    if (reader.ReadSequential(reinterpret_cast<char*>(&record.lsn_), sizeof(LogRecord::lsn_t)) != sizeof(LogRecord::lsn_t)) return false;
    if (reader.ReadSequential(reinterpret_cast<char*>(&record.prev_lsn_), sizeof(LogRecord::lsn_t)) != sizeof(LogRecord::lsn_t)) return false;
    if (reader.ReadSequential(reinterpret_cast<char*>(&record.undo_next_lsn_), sizeof(LogRecord::lsn_t)) != sizeof(LogRecord::lsn_t)) return false;
    if (reader.ReadSequential(reinterpret_cast<char*>(&record.txn_id_), sizeof(LogRecord::txn_id_t)) != sizeof(LogRecord::txn_id_t)) return false;
    if (reader.ReadSequential(reinterpret_cast<char*>(&record.timestamp_), sizeof(LogRecord::timestamp_t)) != sizeof(LogRecord::timestamp_t)) return false;

    int log_type_int;
    if (reader.ReadSequential(reinterpret_cast<char*>(&log_type_int), sizeof(int)) != sizeof(int)) return false;
    record.log_record_type_ = static_cast<LogRecordType>(log_type_int);

    record.db_name_ = ReadStringFromReader(reader);

    switch (record.log_record_type_) {
        case LogRecordType::INSERT:
            record.table_name_ = ReadStringFromReader(reader);
            record.new_value_ = ReadValueFromReader(reader);
            break;
        case LogRecordType::UPDATE:
            record.table_name_ = ReadStringFromReader(reader);
            record.old_value_ = ReadValueFromReader(reader);
            record.new_value_ = ReadValueFromReader(reader);
            break;
        case LogRecordType::APPLY_DELETE:
        case LogRecordType::MARK_DELETE:
        case LogRecordType::ROLLBACK_DELETE:
            record.table_name_ = ReadStringFromReader(reader);
            record.old_value_ = ReadValueFromReader(reader);
            break;
        case LogRecordType::CREATE_TABLE:
        case LogRecordType::DROP_TABLE:
        case LogRecordType::CLR:
            record.table_name_ = ReadStringFromReader(reader);
            break;
        case LogRecordType::CHECKPOINT_BEGIN:
        case LogRecordType::CHECKPOINT_END: {
            int32_t att_size = 0;
            reader.ReadSequential(reinterpret_cast<char*>(&att_size), sizeof(int32_t));
            for (int32_t i = 0; i < att_size && i < 10000; i++) {
                int32_t d;
                reader.ReadSequential(reinterpret_cast<char*>(&d), sizeof(int32_t));
                reader.ReadSequential(reinterpret_cast<char*>(&d), sizeof(int32_t));
                reader.ReadSequential(reinterpret_cast<char*>(&d), sizeof(int32_t));
            }
            int32_t dpt_size = 0;
            reader.ReadSequential(reinterpret_cast<char*>(&dpt_size), sizeof(int32_t));
            for (int32_t i = 0; i < dpt_size && i < 10000; i++) {
                int32_t d;
                reader.ReadSequential(reinterpret_cast<char*>(&d), sizeof(int32_t));
                reader.ReadSequential(reinterpret_cast<char*>(&d), sizeof(int32_t));
            }
            break;
        }
        default:
            break;
    }

    uint32_t crc;
    reader.ReadSequential(reinterpret_cast<char*>(&crc), sizeof(uint32_t));
    record.size_ = size;
    return true;
}

std::string TimeTravelEngine::ReadStringFromReader(LogPageReader& reader) {
    uint32_t len = 0;
    if (reader.ReadSequential(reinterpret_cast<char*>(&len), sizeof(uint32_t)) != sizeof(uint32_t)) {
        return "";
    }
    if (len > MAX_RECORD_SIZE) return "";
    std::string result(len, '\0');
    reader.ReadSequential(&result[0], len);
    return result;
}

Value TimeTravelEngine::ReadValueFromReader(LogPageReader& reader) {
    int type_id = 0;
    reader.ReadSequential(reinterpret_cast<char*>(&type_id), sizeof(int));
    std::string s_val = ReadStringFromReader(reader);
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

// ============================================================================
// FORWARD REPLAY - For distant past queries
// ============================================================================

std::unique_ptr<TableHeap> TimeTravelEngine::BuildSnapshotForwardReplay(
    const std::string& table_name,
    uint64_t target_time,
    const std::string& db_name) {

    auto* table_info = catalog_->GetTable(table_name);
    if (!table_info) return nullptr;

    const Schema& schema = table_info->schema_;

    std::unordered_multimap<std::string, std::vector<Value>> in_memory_table;
    in_memory_table.reserve(10000);

    std::string log_path = log_manager_->GetLogFilePath(db_name);
    LogPageReader reader(64);
    if (!reader.Open(log_path)) {
        return std::make_unique<TableHeap>(bpm_, nullptr);
    }

    size_t records_processed = 0;
    LogRecord record(0, 0, LogRecordType::INVALID);
    while (ReadLogRecordFromReader(reader, record)) {
        records_processed++;

        // Progress logging for long operations (INFO level for visibility)
        if (records_processed % PROGRESS_INTERVAL == 0) {
            LOG_INFO(LOG_COMPONENT, "Forward replay: %zu records processed", records_processed);
        }

        // Stop at target time
        if (record.timestamp_ > target_time) {
            break;
        }

        if (record.table_name_ != table_name) {
            continue;
        }

        switch (record.log_record_type_) {
            case LogRecordType::INSERT: {
                auto vals = ParseTupleString(record.new_value_.ToString(), table_info);
                if (!vals.empty()) {
                    std::string key = MakeTupleKey(vals);
                    in_memory_table.emplace(key, std::move(vals));
                }
                break;
            }
            case LogRecordType::MARK_DELETE:
            case LogRecordType::APPLY_DELETE: {
                auto vals = ParseTupleString(record.old_value_.ToString(), table_info);
                if (!vals.empty()) {
                    std::string key = MakeTupleKey(vals);
                    auto range = in_memory_table.equal_range(key);
                    if (range.first != range.second) {
                        in_memory_table.erase(range.first);
                    }
                }
                break;
            }
            case LogRecordType::UPDATE: {
                auto old_vals = ParseTupleString(record.old_value_.ToString(), table_info);
                auto new_vals = ParseTupleString(record.new_value_.ToString(), table_info);
                if (!old_vals.empty()) {
                    std::string old_key = MakeTupleKey(old_vals);
                    auto range = in_memory_table.equal_range(old_key);
                    if (range.first != range.second) {
                        in_memory_table.erase(range.first);
                    }
                }
                if (!new_vals.empty()) {
                    std::string new_key = MakeTupleKey(new_vals);
                    in_memory_table.emplace(new_key, std::move(new_vals));
                }
                break;
            }
            default:
                break;
        }
    }
    reader.Close();

    LOG_INFO(LOG_COMPONENT, "Forward replay: finished scanning %zu records, in-memory table has %zu rows",
             records_processed, in_memory_table.size());

    // For very large tables, warn about materialization time
    if (in_memory_table.size() > 100000) {
        LOG_INFO(LOG_COMPONENT, "Forward replay: large snapshot (%zu rows) - materialization may be slow",
                 in_memory_table.size());
    }

    // Materialize - this can be slow for large tables due to buffer pool I/O
    auto mat_start = std::chrono::high_resolution_clock::now();
    LOG_INFO(LOG_COMPONENT, "Forward replay: materializing %zu rows to TableHeap...", in_memory_table.size());

    auto snapshot = std::make_unique<TableHeap>(bpm_, nullptr);
    size_t materialized = 0;
    auto last_log = mat_start;

    for (const auto& [key, vals] : in_memory_table) {
        if (vals.size() == schema.GetColumnCount()) {
            Tuple tuple(vals, schema);
            RID rid;
            snapshot->InsertTuple(tuple, &rid, nullptr);
            materialized++;

            // Progress logging every 10k rows with timing
            if (materialized % PROGRESS_INTERVAL == 0) {
                auto now = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - mat_start).count();
                auto batch_time = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_log).count();
                double rate = (materialized * 1000.0) / (elapsed > 0 ? elapsed : 1);

                LOG_INFO(LOG_COMPONENT, "Forward replay: materialized %zu / %zu rows (%.0f rows/sec, batch: %lldms)",
                         materialized, in_memory_table.size(), rate, static_cast<long long>(batch_time));
                last_log = now;
            }
        }
    }

    auto mat_end = std::chrono::high_resolution_clock::now();
    auto mat_ms = std::chrono::duration_cast<std::chrono::milliseconds>(mat_end - mat_start).count();

    LOG_INFO(LOG_COMPONENT, "Forward replay: materialization complete (%zu rows in %lldms, %.0f rows/sec)",
             materialized, static_cast<long long>(mat_ms),
             mat_ms > 0 ? (materialized * 1000.0 / mat_ms) : 0.0);

    return snapshot;
}

// ============================================================================
// RECOVER TO
// ============================================================================

TimeTravelEngine::TimeTravelResult TimeTravelEngine::RecoverTo(
    uint64_t target_time,
    const std::string& db_name) {

    auto start = std::chrono::high_resolution_clock::now();
    LOG_INFO(LOG_COMPONENT, "Starting RECOVER TO operation");

    std::string actual_db = db_name;
    if (actual_db.empty() && log_manager_) {
        actual_db = log_manager_->GetCurrentDatabase();
    }

    uint64_t current_time = LogRecord::GetCurrentTimestamp();
    if (target_time >= current_time) {
        LOG_INFO(LOG_COMPONENT, "Target time is current/future, no recovery needed");
        return TimeTravelResult::Success(0, 0, Strategy::AUTO);
    }

    auto all_tables = catalog_->GetAllTables();
    if (all_tables.empty()) {
        LOG_WARN(LOG_COMPONENT, "No tables to recover");
        return TimeTravelResult::Error("No tables to recover");
    }

    int total_records = 0;

    // Build snapshots for all tables
    std::map<std::string, std::vector<Tuple>> snapshot_data;

    for (auto* tbl_info : all_tables) {
        if (!tbl_info || !tbl_info->table_heap_) continue;

        std::string tbl = tbl_info->name_;
        if (tbl == "chronos_users" || tbl.find("sys_") == 0) continue;

        LOG_DEBUG(LOG_COMPONENT, "Building snapshot for table: %s", tbl.c_str());

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

        LOG_DEBUG(LOG_COMPONENT, "Snapshot for '%s': %zu rows", tbl.c_str(), snapshot_data[tbl].size());
    }

    // Atomic swap
    LOG_INFO(LOG_COMPONENT, "Performing atomic swap for %zu tables", snapshot_data.size());

    for (const auto& [tbl, tuples] : snapshot_data) {
        auto* tbl_info = catalog_->GetTable(tbl);
        if (!tbl_info || !tbl_info->table_heap_) continue;

        TableHeap* heap = tbl_info->table_heap_.get();

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

        tbl_info->SetCheckpointLSN(LogRecord::INVALID_LSN);
    }

    bpm_->FlushAllPages();
    if (checkpoint_mgr_) checkpoint_mgr_->BeginCheckpoint();

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    LOG_INFO(LOG_COMPONENT, "RECOVER TO completed: %d records in %lldms",
             total_records, static_cast<long long>(ms));

    return TimeTravelResult::Success(total_records, ms, Strategy::AUTO);
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

    uint32_t col_count = table_info->schema_.GetColumnCount();
    vals.reserve(col_count);

    // Try binary format (magic 0x02)
    if (static_cast<unsigned char>(tuple_str[0]) == 0x02) {
        size_t pos = 1;
        if (pos + sizeof(uint32_t) <= tuple_str.size()) {
            uint32_t count;
            std::memcpy(&count, tuple_str.data() + pos, sizeof(uint32_t));
            pos += sizeof(uint32_t);

            for (uint32_t i = 0; i < count && i < col_count && pos + sizeof(uint32_t) <= tuple_str.size(); i++) {
                uint32_t len;
                std::memcpy(&len, tuple_str.data() + pos, sizeof(uint32_t));
                pos += sizeof(uint32_t);
                if (pos + len > tuple_str.size()) break;

                std::string field(tuple_str.data() + pos, len);
                pos += len;

                const Column& col = table_info->schema_.GetColumn(i);
                TypeId type = col.GetType();
                if (type == TypeId::INTEGER) {
                    try { vals.emplace_back(type, std::stoi(field)); }
                    catch (...) { vals.emplace_back(type, 0); }
                } else if (type == TypeId::DECIMAL) {
                    try { vals.emplace_back(type, std::stod(field)); }
                    catch (...) { vals.emplace_back(type, 0.0); }
                } else {
                    vals.emplace_back(type, field);
                }
            }

            if (!vals.empty()) {
                while (vals.size() < col_count) {
                    const Column& col = table_info->schema_.GetColumn(vals.size());
                    TypeId type = col.GetType();
                    if (type == TypeId::INTEGER) vals.emplace_back(type, 0);
                    else if (type == TypeId::DECIMAL) vals.emplace_back(type, 0.0);
                    else vals.emplace_back(type, std::string(""));
                }
                return vals;
            }
        }
    }

    // Fallback: pipe-separated
    std::stringstream ss(tuple_str);
    std::string item;
    uint32_t col_idx = 0;

    while (std::getline(ss, item, '|') && col_idx < col_count) {
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

    while (vals.size() < col_count) {
        const Column& col = table_info->schema_.GetColumn(vals.size());
        TypeId type = col.GetType();
        if (type == TypeId::INTEGER) vals.emplace_back(type, 0);
        else if (type == TypeId::DECIMAL) vals.emplace_back(type, 0.0);
        else vals.emplace_back(type, std::string(""));
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

// Legacy methods for compatibility - now use LogPageReader internally
bool TimeTravelEngine::ReadLogRecord(std::ifstream& log_file, LogRecord& record) {
    int32_t size = 0;
    log_file.read(reinterpret_cast<char*>(&size), sizeof(int32_t));
    if (log_file.gcount() != sizeof(int32_t) || size <= 0 || static_cast<size_t>(size) > MAX_RECORD_SIZE) return false;

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
                int32_t d;
                log_file.read(reinterpret_cast<char*>(&d), sizeof(int32_t));
                log_file.read(reinterpret_cast<char*>(&d), sizeof(int32_t));
                log_file.read(reinterpret_cast<char*>(&d), sizeof(int32_t));
            }
            int32_t dpt_size = 0;
            log_file.read(reinterpret_cast<char*>(&dpt_size), sizeof(int32_t));
            for (int32_t i = 0; i < dpt_size && i < 10000; i++) {
                int32_t d;
                log_file.read(reinterpret_cast<char*>(&d), sizeof(int32_t));
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
    if (in.gcount() != sizeof(uint32_t) || len > MAX_RECORD_SIZE) return "";
    std::string result(len, '\0');
    in.read(&result[0], len);
    return result;
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

// ============================================================================
// FAST OFFSET ESTIMATION BY TIMESTAMP (O(1) - NO SCANNING)
// ============================================================================

/**
 * Estimate log file offset for a target timestamp using time interpolation.
 *
 * FAST ALGORITHM (O(1)):
 * 1. Read first timestamp from log (position 0)
 * 2. Read last timestamp from log (seek near end)
 * 3. Calculate proportional position: (target - first) / (last - first)
 * 4. Multiply by file size to get estimated offset
 *
 * This avoids expensive byte-by-byte scanning or binary search with linear probes.
 * The offset is an approximation - actual scanning will skip records before target_time.
 */
size_t TimeTravelEngine::FindStartOffsetForTimestamp(LogPageReader& reader, uint64_t target_time) {
    size_t file_size = reader.GetFileSize();
    if (file_size < 36) return 0; // File too small

    // Header layout: size(4) + lsn(4) + prev_lsn(4) + undo_next(4) + txn_id(4) + timestamp(8)
    constexpr size_t TIMESTAMP_OFFSET = 20;

    // Step 1: Read FIRST timestamp (O(1) - always at position 0)
    uint64_t first_timestamp = 0;
    if (reader.Read(TIMESTAMP_OFFSET, reinterpret_cast<char*>(&first_timestamp), sizeof(uint64_t)) != sizeof(uint64_t)) {
        LOG_WARN(LOG_COMPONENT, "Failed to read first timestamp");
        return 0;
    }

    // Sanity check first timestamp
    if (first_timestamp < 946684800000000ULL || first_timestamp > 4102444800000000ULL) {
        LOG_WARN(LOG_COMPONENT, "Invalid first timestamp: %llu", static_cast<unsigned long long>(first_timestamp));
        return 0;
    }

    // Step 2: Get LAST timestamp
    // PROBLEM: Seeking to arbitrary position lands mid-record, causing "Invalid record size"
    // SOLUTION: Use current system time as upper bound (safe approximation)
    // This works because: if target < current_time, we're in the past, interpolation works
    uint64_t last_timestamp = LogRecord::GetCurrentTimestamp();

    // Sanity check: last should be >= first
    if (last_timestamp < first_timestamp) {
        last_timestamp = first_timestamp + 1;  // Avoid division by zero
    }

    LOG_DEBUG(LOG_COMPONENT, "Using current time as last_timestamp upper bound: %llu",
              static_cast<unsigned long long>(last_timestamp));

    LOG_DEBUG(LOG_COMPONENT, "Time range: first=%llu, last=%llu, target=%llu",
              static_cast<unsigned long long>(first_timestamp),
              static_cast<unsigned long long>(last_timestamp),
              static_cast<unsigned long long>(target_time));

    // Step 3: Calculate proportional position based on time
    if (target_time <= first_timestamp) {
        return 0; // Target before log start
    }

    if (target_time >= last_timestamp) {
        return file_size; // Target after log end
    }

    // Time-based interpolation
    double time_range = static_cast<double>(last_timestamp - first_timestamp);
    if (time_range <= 0) {
        return 0; // All at same timestamp
    }

    double time_position = static_cast<double>(target_time - first_timestamp) / time_range;
    size_t estimated_offset = static_cast<size_t>(time_position * static_cast<double>(file_size));

    LOG_DEBUG(LOG_COMPONENT, "Time interpolation: position=%.2f, estimated_offset=%zu",
              time_position, estimated_offset);

    return estimated_offset;
}

// Legacy stub (for ifstream interface - deprecated)
std::streampos TimeTravelEngine::FindClosestLogOffset(std::ifstream&, uint64_t) { return 0; }

/**
 * Choose the best strategy based on ACTUAL log position for target timestamp.
 *
 * STRATEGY SELECTION LOGIC:
 * - FORWARD_REPLAY: Start from empty, replay all operations up to target
 * - REVERSE_DELTA:  Start from current state, undo operations after target
 *
 * ALGORITHM:
 * 1. Scan log from beginning to find ACTUAL byte offset where timestamp > target
 * 2. Compare bytes_before_target vs bytes_after_target
 * 3. Choose strategy with fewer bytes (= fewer operations)
 *
 * This is more accurate than time-based interpolation because it measures
 * actual operation distribution, not assumed uniform time distribution.
 */
TimeTravelEngine::Strategy TimeTravelEngine::ChooseStrategy(uint64_t target_time, const std::string& db_name) {
    if (!log_manager_) {
        LOG_WARN(LOG_COMPONENT, "ChooseStrategy: no log_manager, defaulting to REVERSE_DELTA");
        return Strategy::REVERSE_DELTA;
    }

    std::string log_path = log_manager_->GetLogFilePath(db_name);
    LOG_INFO(LOG_COMPONENT, "ChooseStrategy: checking log file '%s' for db '%s'",
             log_path.c_str(), db_name.c_str());

    LogPageReader reader(32); // Moderate cache for scanning
    if (!reader.Open(log_path)) {
        LOG_ERROR(LOG_COMPONENT, "ChooseStrategy: CANNOT open log file '%s' - time travel will fail!",
                  log_path.c_str());
        return Strategy::FORWARD_REPLAY; // Return forward replay so the actual build will fail clearly
    }

    size_t file_size = reader.GetFileSize();
    if (file_size < 100) {
        reader.Close();
        return Strategy::REVERSE_DELTA; // Tiny log - either strategy is fine
    }

    LOG_INFO(LOG_COMPONENT, "Strategy selection: scanning log file (%zu bytes) to find target position", file_size);

    // =========================================================================
    // SCAN LOG TO FIND ACTUAL BYTE OFFSET FOR TARGET TIMESTAMP
    // =========================================================================
    // This gives us the TRUE position where operations exceed target_time,
    // which accurately reflects operation count on each side.
    reader.Seek(0);
    size_t target_offset = 0;
    size_t records_scanned = 0;
    size_t position_before_record = 0;

    LogRecord record(0, 0, LogRecordType::INVALID);
    while (true) {
        // Save position BEFORE reading the record
        position_before_record = reader.Tell();

        if (!ReadLogRecordFromReader(reader, record)) {
            // Reached end of file - all records are <= target_time
            target_offset = file_size;
            break;
        }

        records_scanned++;

        // Found first record AFTER target time - this is the split point
        if (record.timestamp_ > target_time) {
            target_offset = position_before_record;  // Position where this record STARTS
            break;
        }

        // Progress logging for large logs (every 100K records)
        if (records_scanned % 100000 == 0) {
            LOG_DEBUG(LOG_COMPONENT, "Strategy scan: %zu records scanned...", records_scanned);
        }
    }

    reader.Close();

    LOG_INFO(LOG_COMPONENT, "Strategy selection: scanned %zu records, target_offset=%zu / %zu bytes",
             records_scanned, target_offset, file_size);

    // =========================================================================
    // COMPARE OPERATION COUNTS ON EACH SIDE
    // =========================================================================
    // bytes_before = operations FORWARD_REPLAY must apply
    // bytes_after  = operations REVERSE_DELTA must undo
    size_t bytes_before = target_offset;
    size_t bytes_after = (file_size > target_offset) ? (file_size - target_offset) : 0;

    LOG_INFO(LOG_COMPONENT, "Strategy comparison: FORWARD needs %zu bytes, REVERSE needs %zu bytes",
             bytes_before, bytes_after);

    // Handle edge cases
    if (bytes_before == 0) {
        LOG_INFO(LOG_COMPONENT, "Choosing FORWARD_REPLAY (target at/before log start - nothing to replay)");
        return Strategy::FORWARD_REPLAY;
    }

    if (bytes_after == 0) {
        LOG_INFO(LOG_COMPONENT, "Choosing REVERSE_DELTA (target at/after log end - nothing to undo)");
        return Strategy::REVERSE_DELTA;
    }

    // Choose strategy with FEWER operations (fewer bytes = fewer records)
    if (bytes_before < bytes_after) {
        LOG_INFO(LOG_COMPONENT, "Choosing FORWARD_REPLAY (%zu bytes < %zu bytes = fewer operations)",
                 bytes_before, bytes_after);
        return Strategy::FORWARD_REPLAY;
    } else {
        LOG_INFO(LOG_COMPONENT, "Choosing REVERSE_DELTA (%zu bytes <= %zu bytes = fewer operations)",
                 bytes_after, bytes_before);
        return Strategy::REVERSE_DELTA;
    }
}

int TimeTravelEngine::EstimateOperationCount(uint64_t, const std::string&) { return 0; }
TimeTravelEngine::TimeTravelResult TimeTravelEngine::RecoverToReverseDelta(uint64_t t, const std::string& db) { return RecoverTo(t, db); }
TimeTravelEngine::TimeTravelResult TimeTravelEngine::RecoverToForwardReplay(uint64_t t, const std::string& db) { return RecoverTo(t, db); }
void TimeTravelEngine::ApplyInverseOperation(TableHeap*, const InverseOperation&, const TableMetadata*, std::unordered_multimap<std::string, RID>&) {}

} // namespace chronosdb
