#include "ai/temporal/access_tracker.h"

#include <algorithm>
#include <unordered_map>

namespace chronosdb {
namespace ai {

TemporalAccessTracker::TemporalAccessTracker() = default;

void TemporalAccessTracker::RecordAccess(const TemporalAccessEvent& event) {
    std::unique_lock lock(mutex_);
    events_.push_back(event);
    PruneOldEvents();
}

std::vector<TemporalAccessTracker::FrequencyBucket>
TemporalAccessTracker::GetFrequencyHistogram(uint64_t bucket_width_us,
                                              size_t max_buckets) const {
    std::shared_lock lock(mutex_);
    if (events_.empty()) return {};

    // Find min/max queried timestamps
    uint64_t min_ts = events_.front().queried_timestamp_us;
    uint64_t max_ts = min_ts;
    for (const auto& e : events_) {
        min_ts = std::min(min_ts, e.queried_timestamp_us);
        max_ts = std::max(max_ts, e.queried_timestamp_us);
    }

    size_t num_buckets = std::min(
        max_buckets,
        static_cast<size_t>((max_ts - min_ts) / bucket_width_us) + 1);

    std::vector<FrequencyBucket> histogram(num_buckets);
    for (size_t i = 0; i < num_buckets; ++i) {
        histogram[i].start_us = min_ts + i * bucket_width_us;
        histogram[i].end_us = histogram[i].start_us + bucket_width_us;
        histogram[i].access_count = 0;
    }

    for (const auto& e : events_) {
        size_t idx = static_cast<size_t>(
            (e.queried_timestamp_us - min_ts) / bucket_width_us);
        if (idx < num_buckets) {
            histogram[idx].access_count++;
        }
    }

    return histogram;
}

std::vector<TemporalAccessEvent> TemporalAccessTracker::GetEvents(
    uint64_t start_us, uint64_t end_us) const {
    std::shared_lock lock(mutex_);
    std::vector<TemporalAccessEvent> result;
    for (const auto& e : events_) {
        if (e.queried_timestamp_us >= start_us &&
            e.queried_timestamp_us < end_us) {
            result.push_back(e);
        }
    }
    return result;
}

std::vector<TemporalAccessEvent> TemporalAccessTracker::GetAllEvents() const {
    std::shared_lock lock(mutex_);
    return {events_.begin(), events_.end()};
}

std::vector<uint64_t> TemporalAccessTracker::GetHotTimestamps(size_t k) const {
    std::shared_lock lock(mutex_);
    // Count frequency of each queried timestamp (bucketed to nearest second)
    std::unordered_map<uint64_t, uint64_t> freq;
    for (const auto& e : events_) {
        uint64_t bucket = e.queried_timestamp_us / 1000000 * 1000000; // Round to second
        freq[bucket]++;
    }

    // Sort by frequency
    std::vector<std::pair<uint64_t, uint64_t>> sorted(freq.begin(), freq.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    std::vector<uint64_t> result;
    result.reserve(std::min(k, sorted.size()));
    for (size_t i = 0; i < k && i < sorted.size(); ++i) {
        result.push_back(sorted[i].first);
    }
    return result;
}

size_t TemporalAccessTracker::GetTotalAccessCount() const {
    std::shared_lock lock(mutex_);
    return events_.size();
}

void TemporalAccessTracker::PruneOldEvents() {
    while (events_.size() > ACCESS_PATTERN_WINDOW_SIZE) {
        events_.pop_front();
    }
}

} // namespace ai
} // namespace chronosdb
