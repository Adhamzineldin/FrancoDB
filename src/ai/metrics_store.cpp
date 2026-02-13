#include "ai/metrics_store.h"

#include <algorithm>
#include <chrono>

namespace chronosdb {
namespace ai {

MetricsStore& MetricsStore::Instance() {
    static MetricsStore instance;
    return instance;
}

void MetricsStore::Record(const MetricEvent& event) {
    size_t idx = write_index_.fetch_add(1, std::memory_order_relaxed)
                 % METRICS_RING_BUFFER_CAPACITY;

    // Write to buffer slot — single-writer per slot due to unique index
    {
        std::unique_lock lock(read_mutex_);
        buffer_[idx] = event;
    }

    // Track total count (saturates at capacity for iteration bounds)
    size_t current = count_.load(std::memory_order_relaxed);
    if (current < METRICS_RING_BUFFER_CAPACITY) {
        count_.fetch_add(1, std::memory_order_relaxed);
    }
}

std::vector<MetricEvent> MetricsStore::Query(uint64_t start_time_us,
                                              uint64_t end_time_us,
                                              MetricType type_filter) const {
    std::vector<MetricEvent> result;
    ForEachEvent([&](const MetricEvent& e) {
        if (e.type == type_filter &&
            e.timestamp_us >= start_time_us &&
            e.timestamp_us < end_time_us) {
            result.push_back(e);
        }
    });
    return result;
}

uint64_t MetricsStore::CountEvents(MetricType type, uint64_t window_us) const {
    uint64_t cutoff = GetCurrentTimeUs() - window_us;
    uint64_t count = 0;
    ForEachEvent([&](const MetricEvent& e) {
        if (e.type == type && e.timestamp_us >= cutoff) {
            ++count;
        }
    });
    return count;
}

double MetricsStore::AverageDuration(MetricType type, const std::string& table,
                                      uint64_t window_us) const {
    uint64_t cutoff = GetCurrentTimeUs() - window_us;
    uint64_t total_duration = 0;
    uint64_t count = 0;
    ForEachEvent([&](const MetricEvent& e) {
        if (e.type == type && e.timestamp_us >= cutoff &&
            (table.empty() || e.table_name == table)) {
            total_duration += e.duration_us;
            ++count;
        }
    });
    return count > 0 ? static_cast<double>(total_duration) / count : 0.0;
}

uint64_t MetricsStore::GetMutationCount(const std::string& table,
                                         uint64_t window_us) const {
    uint64_t cutoff = GetCurrentTimeUs() - window_us;
    uint64_t count = 0;
    ForEachEvent([&](const MetricEvent& e) {
        if (e.timestamp_us >= cutoff && e.table_name == table &&
            (e.type == MetricType::DML_INSERT ||
             e.type == MetricType::DML_UPDATE ||
             e.type == MetricType::DML_DELETE)) {
            count += e.rows_affected;
        }
    });
    return count;
}

uint64_t MetricsStore::GetUserEventCount(const std::string& user,
                                          MetricType type,
                                          uint64_t window_us) const {
    uint64_t cutoff = GetCurrentTimeUs() - window_us;
    uint64_t count = 0;
    ForEachEvent([&](const MetricEvent& e) {
        if (e.type == type && e.user == user && e.timestamp_us >= cutoff) {
            ++count;
        }
    });
    return count;
}

size_t MetricsStore::GetTotalRecorded() const {
    return std::min(count_.load(std::memory_order_relaxed),
                    METRICS_RING_BUFFER_CAPACITY);
}

void MetricsStore::Reset() {
    std::unique_lock lock(read_mutex_);
    write_index_.store(0, std::memory_order_relaxed);
    count_.store(0, std::memory_order_relaxed);
}

uint64_t MetricsStore::GetCurrentTimeUs() const {
    auto now = std::chrono::system_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch())
            .count());
}

template <typename Fn>
void MetricsStore::ForEachEvent(Fn&& fn) const {
    std::shared_lock lock(read_mutex_);
    size_t total = std::min(count_.load(std::memory_order_relaxed),
                            METRICS_RING_BUFFER_CAPACITY);
    size_t write_pos = write_index_.load(std::memory_order_relaxed);

    for (size_t i = 0; i < total; ++i) {
        size_t idx;
        if (total < METRICS_RING_BUFFER_CAPACITY) {
            idx = i;
        } else {
            // Ring buffer wrapped: start from oldest entry
            idx = (write_pos + i) % METRICS_RING_BUFFER_CAPACITY;
        }
        fn(buffer_[idx]);
    }
}

} // namespace ai
} // namespace chronosdb
