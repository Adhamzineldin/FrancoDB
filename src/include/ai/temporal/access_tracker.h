#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "ai/ai_config.h"

namespace chronosdb {
namespace ai {

struct TemporalAccessEvent {
    uint64_t queried_timestamp_us; // The timestamp the user time-traveled to
    uint64_t query_time_us;        // When the query was executed
    std::string table_name;
    std::string db_name;
};

/**
 * TemporalAccessTracker - Records which timestamps are queried in time-travel.
 *
 * Builds a frequency distribution of queried temporal points.
 * Used by HotspotDetector to identify popular time ranges.
 */
class TemporalAccessTracker {
public:
    TemporalAccessTracker();

    void RecordAccess(const TemporalAccessEvent& event);

    // Frequency histogram bucketed by time intervals
    struct FrequencyBucket {
        uint64_t start_us;
        uint64_t end_us;
        uint64_t access_count;
    };
    std::vector<FrequencyBucket> GetFrequencyHistogram(
        uint64_t bucket_width_us, size_t max_buckets = 1000) const;

    // Raw events in a time range
    std::vector<TemporalAccessEvent> GetEvents(uint64_t start_us,
                                                uint64_t end_us) const;

    // All recorded events (for hotspot detection)
    std::vector<TemporalAccessEvent> GetAllEvents() const;

    // Top-K most frequently queried timestamps
    std::vector<uint64_t> GetHotTimestamps(size_t k) const;

    size_t GetTotalAccessCount() const;

private:
    mutable std::shared_mutex mutex_;
    std::deque<TemporalAccessEvent> events_;

    void PruneOldEvents();
};

} // namespace ai
} // namespace chronosdb
