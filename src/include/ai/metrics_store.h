#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "ai/ai_config.h"

namespace chronosdb {
namespace ai {

enum class MetricType : uint8_t {
    DML_INSERT = 0,
    DML_UPDATE,
    DML_DELETE,
    DML_SELECT,
    SCAN_SEQ,
    SCAN_INDEX,
    TIME_TRAVEL_QUERY,
    ANOMALY_DETECTED,
    SNAPSHOT_TRIGGERED
};

struct MetricEvent {
    MetricType type;
    uint64_t timestamp_us;
    uint64_t duration_us;
    uint32_t session_id;
    std::string user;
    std::string table_name;
    std::string db_name;
    uint32_t rows_affected;
    uint8_t scan_strategy;    // 0 = seq, 1 = index
    uint64_t target_timestamp; // for time travel queries
};

/**
 * MetricsStore - Thread-safe ring buffer for AI operation metrics.
 *
 * Lock-free writes via atomic index. Reads take a shared_lock for
 * consistent snapshots. All three AI systems read from this single store.
 */
class MetricsStore {
public:
    static MetricsStore& Instance();

    // Record a metric event (lock-free, O(1))
    void Record(const MetricEvent& event);

    // Query events in [start_time_us, end_time_us) matching type_filter
    std::vector<MetricEvent> Query(uint64_t start_time_us, uint64_t end_time_us,
                                   MetricType type_filter) const;

    // Count events of a type in the last window_us microseconds
    uint64_t CountEvents(MetricType type, uint64_t window_us) const;

    // Average execution duration for a type+table in the last window_us
    double AverageDuration(MetricType type, const std::string& table,
                           uint64_t window_us) const;

    // Table-specific mutation count in last window_us (INSERT+UPDATE+DELETE)
    uint64_t GetMutationCount(const std::string& table, uint64_t window_us) const;

    // User-specific event count for a type in last window_us
    uint64_t GetUserEventCount(const std::string& user, MetricType type,
                               uint64_t window_us) const;

    // Total events ever recorded
    size_t GetTotalRecorded() const;

    // Reset all metrics
    void Reset();

private:
    MetricsStore() = default;
    ~MetricsStore() = default;
    MetricsStore(const MetricsStore&) = delete;
    MetricsStore& operator=(const MetricsStore&) = delete;

    std::array<MetricEvent, METRICS_RING_BUFFER_CAPACITY> buffer_;
    std::atomic<size_t> write_index_{0};
    std::atomic<size_t> count_{0};
    mutable std::shared_mutex read_mutex_;

    uint64_t GetCurrentTimeUs() const;

    // Iterate over valid buffer entries, calling fn for each
    template <typename Fn>
    void ForEachEvent(Fn&& fn) const;
};

} // namespace ai
} // namespace chronosdb
