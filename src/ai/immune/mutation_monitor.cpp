#include "ai/immune/mutation_monitor.h"

#include <algorithm>
#include <chrono>
#include <numeric>

namespace chronosdb {
namespace ai {

MutationMonitor::MutationMonitor() = default;

void MutationMonitor::RecordMutation(const std::string& table_name,
                                      DMLOperation op, uint32_t rows_affected,
                                      uint64_t timestamp_us) {
    (void)op; // All mutations treated equally for rate tracking
    auto& log = GetOrCreate(table_name);
    std::lock_guard lock(log.mutex);
    log.entries.push_back({timestamp_us, rows_affected});

    // Prune entries older than the rolling window
    PruneOldEntries(log, timestamp_us - MUTATION_ROLLING_WINDOW_US);
}

uint64_t MutationMonitor::GetMutationCount(const std::string& table_name,
                                             uint64_t window_us) const {
    std::shared_lock tables_lock(tables_mutex_);
    auto it = tables_.find(table_name);
    if (it == tables_.end()) return 0;

    uint64_t cutoff = GetCurrentTimeUs() - window_us;
    std::lock_guard lock(it->second->mutex);

    uint64_t count = 0;
    for (const auto& entry : it->second->entries) {
        if (entry.timestamp_us >= cutoff) {
            count += entry.row_count;
        }
    }
    return count;
}

double MutationMonitor::GetMutationRate(const std::string& table_name) const {
    // Rate = total mutations in last RATE_INTERVAL_US / interval_seconds
    uint64_t count = GetMutationCount(table_name, RATE_INTERVAL_US);
    double interval_seconds = static_cast<double>(RATE_INTERVAL_US) / 1000000.0;
    return interval_seconds > 0 ? static_cast<double>(count) / interval_seconds : 0.0;
}

std::vector<double> MutationMonitor::GetHistoricalRates(
    const std::string& table_name, size_t num_intervals,
    uint64_t interval_us) const {
    std::shared_lock tables_lock(tables_mutex_);
    auto it = tables_.find(table_name);
    if (it == tables_.end()) {
        return std::vector<double>(num_intervals, 0.0);
    }

    uint64_t now = GetCurrentTimeUs();
    double interval_sec = static_cast<double>(interval_us) / 1000000.0;
    std::vector<double> rates(num_intervals, 0.0);

    std::lock_guard lock(it->second->mutex);
    for (const auto& entry : it->second->entries) {
        // Determine which interval this entry belongs to
        if (entry.timestamp_us >= now) continue;
        uint64_t age_us = now - entry.timestamp_us;
        size_t interval_idx = static_cast<size_t>(age_us / interval_us);
        if (interval_idx < num_intervals) {
            // Index 0 = most recent interval
            rates[interval_idx] +=
                static_cast<double>(entry.row_count) / interval_sec;
        }
    }

    return rates;
}

std::vector<std::string> MutationMonitor::GetMonitoredTables() const {
    std::shared_lock lock(tables_mutex_);
    std::vector<std::string> result;
    result.reserve(tables_.size());
    for (const auto& [name, _] : tables_) {
        result.push_back(name);
    }
    return result;
}

MutationMonitor::TableMutationLog& MutationMonitor::GetOrCreate(
    const std::string& table_name) {
    // Fast path: read lock
    {
        std::shared_lock lock(tables_mutex_);
        auto it = tables_.find(table_name);
        if (it != tables_.end()) return *it->second;
    }
    // Slow path: write lock
    std::unique_lock lock(tables_mutex_);
    auto [it, inserted] = tables_.try_emplace(
        table_name, std::make_unique<TableMutationLog>());
    return *it->second;
}

void MutationMonitor::PruneOldEntries(TableMutationLog& log,
                                        uint64_t cutoff_us) const {
    while (!log.entries.empty() && log.entries.front().timestamp_us < cutoff_us) {
        log.entries.pop_front();
    }
}

uint64_t MutationMonitor::GetCurrentTimeUs() const {
    auto now = std::chrono::system_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count());
}

} // namespace ai
} // namespace chronosdb
