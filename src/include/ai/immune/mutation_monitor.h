#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ai/ai_config.h"
#include "ai/dml_observer.h"

namespace chronosdb {
namespace ai {

/**
 * MutationMonitor - Tracks per-table mutation rates using a rolling window.
 *
 * Records every INSERT/UPDATE/DELETE event with timestamp and row count.
 * Provides historical rate data for z-score anomaly detection.
 */
class MutationMonitor {
public:
    MutationMonitor();

    // Record a mutation event for a table
    void RecordMutation(const std::string& table_name, DMLOperation op,
                        uint32_t rows_affected, uint64_t timestamp_us);

    // Mutation count for a table in the last window_us microseconds
    uint64_t GetMutationCount(const std::string& table_name,
                               uint64_t window_us) const;

    // Rolling average mutation rate (rows/second) for a table
    double GetMutationRate(const std::string& table_name) const;

    // Historical rates for z-score: vector of rates per interval
    std::vector<double> GetHistoricalRates(const std::string& table_name,
                                            size_t num_intervals,
                                            uint64_t interval_us) const;

    // All tables being monitored
    std::vector<std::string> GetMonitoredTables() const;

    // Clear mutation history for a table (called after recovery)
    void ClearTableHistory(const std::string& table_name);

    // Decay all historical data to adapt to changing workloads
    void Decay(double decay_factor);

private:
    struct MutationEntry {
        uint64_t timestamp_us;
        uint32_t row_count;
    };

    struct TableMutationLog {
        std::deque<MutationEntry> entries;
        mutable std::mutex mutex;
    };

    mutable std::shared_mutex tables_mutex_;
    std::unordered_map<std::string, std::unique_ptr<TableMutationLog>> tables_;

    TableMutationLog& GetOrCreate(const std::string& table_name);
    void PruneOldEntries(TableMutationLog& log, uint64_t cutoff_us) const;
    uint64_t GetCurrentTimeUs() const;
};

} // namespace ai
} // namespace chronosdb
