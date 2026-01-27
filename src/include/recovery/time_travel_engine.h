#pragma once

#include "recovery/log_manager.h"
#include "recovery/log_record.h"
#include "recovery/checkpoint_manager.h"
#include "catalog/catalog.h"
#include "catalog/table_metadata.h"
#include "storage/storage_interface.h"
#include "storage/table/table_heap.h"
#include <memory>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <fstream>

namespace chronosdb {

/**
 * Time Travel Engine - Reverse Delta Implementation
 *
 * This class implements the "Reverse Delta" strategy for time travel:
 * - Instead of replaying forward from genesis (O(N))
 * - We start from current state and apply inverse operations backwards (O(K))
 * - Where K = number of operations between target_time and now
 *
 * Mental Model:
 * =============
 *
 * Current State: [A=10, B=20, C=30]
 *
 * Log (chronological):
 *   LSN 1: INSERT A=5          @ T1
 *   LSN 2: UPDATE A=5->10      @ T2
 *   LSN 3: INSERT B=20         @ T3
 *   LSN 4: INSERT C=30         @ T4
 *
 * To get state at T2 (after UPDATE but before B insert):
 *
 * Forward Replay (old approach):
 *   - Start empty, replay LSN 1, 2 = [A=10]
 *   - Scanned all records up to T2
 *
 * Reverse Delta (new approach):
 *   - Start with current: [A=10, B=20, C=30]
 *   - Undo LSN 4: Remove C -> [A=10, B=20]
 *   - Undo LSN 3: Remove B -> [A=10]
 *   - Stop (reached T2)
 *   - Only scanned records AFTER T2
 *
 * Benefits:
 * - Recent queries are O(delta) not O(total_history)
 * - Common case (recent past) is fast
 * - Edge case (distant past) falls back to forward replay
 *
 * SOLID Principles:
 * - Single Responsibility: Only handles time travel logic
 * - Open/Closed: Strategy pattern for different approaches
 * - Dependency Inversion: Depends on IBufferManager interface
 */
class TimeTravelEngine {
public:
    /**
     * Time Travel Strategy
     */
    enum class Strategy {
        REVERSE_DELTA,  // Apply inverse operations from current backwards
        FORWARD_REPLAY, // Replay log from beginning (fallback)
        AUTO            // Automatically choose best strategy
    };

    /**
     * Result of a time travel operation
     */
    struct TimeTravelResult {
        bool success = false;
        std::string error_message;
        int records_processed = 0;
        int64_t elapsed_ms = 0;
        Strategy strategy_used = Strategy::AUTO;

        static TimeTravelResult Success(int records, int64_t ms, Strategy s) {
            return {true, "", records, ms, s};
        }

        static TimeTravelResult Error(const std::string& msg) {
            return {false, msg, 0, 0, Strategy::AUTO};
        }
    };

    /**
     * Inverse operation for undo
     */
    struct InverseOperation {
        LogRecordType original_type;
        LogRecord::lsn_t lsn;
        uint64_t timestamp;
        std::string table_name;
        std::vector<Value> values_to_insert;   // For undo DELETE
        std::vector<Value> values_to_delete;   // For undo INSERT
        std::vector<Value> old_values;         // For undo UPDATE (restore to this)
        std::vector<Value> new_values;         // For undo UPDATE (find this)
    };

    /**
     * Constructor
     */
    TimeTravelEngine(LogManager* log_manager, Catalog* catalog,
                     IBufferManager* bpm, CheckpointManager* checkpoint_mgr = nullptr);

    // ========================================================================
    // SELECT ... AS OF (Read-Only Virtual Snapshot)
    // ========================================================================

    /**
     * Build a read-only snapshot of a table at a specific timestamp.
     * Uses reverse delta strategy - clones current state and applies
     * inverse operations backwards until target time.
     *
     * @param table_name Table to snapshot
     * @param target_time Target timestamp in microseconds
     * @param strategy Which strategy to use (default: AUTO)
     * @return Unique pointer to snapshot heap (caller owns), nullptr on error
     */
    std::unique_ptr<TableHeap> BuildSnapshot(
        const std::string& table_name,
        uint64_t target_time,
        const std::string& db_name = "",
        Strategy strategy = Strategy::AUTO);

    // ========================================================================
    // RECOVER TO (Persistent Rollback)
    // ========================================================================

    /**
     * Permanently revert database to a specific timestamp.
     * Uses reverse delta with atomic commit.
     *
     * @param target_time Target timestamp in microseconds
     * @param db_name Database name
     * @return Result of the operation
     */
    TimeTravelResult RecoverTo(uint64_t target_time, const std::string& db_name = "");

    // ========================================================================
    // CONFIGURATION
    // ========================================================================

    /**
     * Set threshold for strategy selection (in microseconds).
     * If target_time is within this threshold of current time,
     * reverse delta is used. Otherwise, forward replay.
     *
     * Default: 1 hour (3,600,000,000 microseconds)
     */
    void SetReverseDeltaThreshold(uint64_t threshold_us) {
        reverse_delta_threshold_ = threshold_us;
    }

    /**
     * Get current threshold
     */
    uint64_t GetReverseDeltaThreshold() const {
        return reverse_delta_threshold_;
    }

private:
    // ========================================================================
    // STRATEGY SELECTION
    // ========================================================================

    /**
     * Choose the best strategy for a given target time.
     *
     * Logic:
     * - If target is within threshold: REVERSE_DELTA (fast for recent)
     * - If target is before last checkpoint: FORWARD_REPLAY (need full history)
     * - Otherwise: REVERSE_DELTA
     */
    Strategy ChooseStrategy(uint64_t target_time, const std::string& db_name);

    /**
     * Estimate number of operations between target_time and now.
     * Used to decide which strategy is more efficient.
     */
    int EstimateOperationCount(uint64_t target_time, const std::string& db_name);

    // ========================================================================
    // REVERSE DELTA IMPLEMENTATION
    // ========================================================================

    /**
     * Build snapshot using reverse delta.
     *
     * Algorithm:
     * 1. Clone current table state
     * 2. Collect log records after target_time (in memory)
     * 3. Sort by LSN descending (most recent first)
     * 4. Apply inverse of each operation
     * 5. Return resulting snapshot
     */
    std::unique_ptr<TableHeap> BuildSnapshotReverseDelta(
        const std::string& table_name,
        uint64_t target_time,
        const std::string& db_name);

    /**
     * Collect all operations after target_time for a table.
     * Returns them sorted by LSN descending (newest first).
     */
    std::vector<InverseOperation> CollectOperationsAfter(
        const std::string& table_name,
        uint64_t target_time,
        const std::string& db_name);

    /**
     * Apply an inverse operation to a heap.
     *
     * Inverse mapping:
     * - INSERT -> DELETE the inserted row
     * - DELETE -> INSERT the deleted row
     * - UPDATE -> Replace new values with old values
     */
    void ApplyInverseOperation(TableHeap* heap,
                               const InverseOperation& op,
                               const TableMetadata* table_info);

    // ========================================================================
    // FORWARD REPLAY IMPLEMENTATION (Fallback)
    // ========================================================================

    /**
     * Build snapshot using forward replay (legacy approach).
     * Used for distant past queries or when reverse delta is not efficient.
     */
    std::unique_ptr<TableHeap> BuildSnapshotForwardReplay(
        const std::string& table_name,
        uint64_t target_time,
        const std::string& db_name);

    // ========================================================================
    // ATOMIC RECOVERY IMPLEMENTATION
    // ========================================================================

    /**
     * Perform atomic recovery using reverse delta.
     *
     * Algorithm:
     * 1. Build snapshot at target_time (in memory)
     * 2. Begin atomic transaction
     * 3. Clear all tables
     * 4. Copy snapshot data to tables
     * 5. Commit transaction
     *
     * If any step fails, rollback to original state.
     */
    TimeTravelResult RecoverToReverseDelta(uint64_t target_time, const std::string& db_name);

    /**
     * Perform atomic recovery using forward replay.
     */
    TimeTravelResult RecoverToForwardReplay(uint64_t target_time, const std::string& db_name);

    // ========================================================================
    // HELPERS
    // ========================================================================

    /**
     * Clone current table data into a new heap.
     */
    std::unique_ptr<TableHeap> CloneTable(const std::string& table_name);

    /**
     * Parse tuple string into values.
     */
    std::vector<Value> ParseTupleString(const std::string& tuple_str,
                                        const TableMetadata* table_info) const;

    /**
     * Check if tuple matches given values.
     */
    bool TupleMatches(const Tuple& tuple,
                      const std::vector<Value>& vals,
                      const TableMetadata* table_info) const;

    /**
     * Read a single log record from file.
     */
    bool ReadLogRecord(std::ifstream& log_file, LogRecord& record);

    /**
     * Read helpers
     */
    static std::string ReadString(std::ifstream& in);
    static Value ReadValue(std::ifstream& in);

    // ========================================================================
    // DATA MEMBERS
    // ========================================================================

    LogManager* log_manager_;
    Catalog* catalog_;
    IBufferManager* bpm_;
    CheckpointManager* checkpoint_mgr_;

    // Strategy selection threshold (default: 1 hour in microseconds)
    uint64_t reverse_delta_threshold_ = 3600000000ULL;
};

} // namespace chronosdb
