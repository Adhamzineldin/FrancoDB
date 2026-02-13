#include "ai/temporal/retention_manager.h"

#include <chrono>

namespace chronosdb {
namespace ai {

WALRetentionManager::WALRetentionManager(LogManager* log_manager)
    : log_manager_(log_manager) {}

WALRetentionManager::RetentionPolicy WALRetentionManager::ComputePolicy(
    const TemporalAccessTracker& tracker) const {
    RetentionPolicy policy;

    auto now = std::chrono::system_clock::now();
    uint64_t now_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count());

    // Default: keep last 24 hours at full fidelity
    policy.hot_retention_us = 24ULL * 60 * 60 * 1000000;

    // Cold cutoff: 7 days by default
    policy.cold_cutoff_us = 7ULL * 24 * 60 * 60 * 1000000;

    // Adaptive: if there are recent accesses to old data, extend retention
    auto hot_timestamps = tracker.GetHotTimestamps(10);
    for (uint64_t ts : hot_timestamps) {
        if (ts < now_us) {
            uint64_t age = now_us - ts;
            // If users are querying data older than hot retention,
            // extend cold cutoff to cover it
            if (age > policy.hot_retention_us && age < policy.cold_cutoff_us * 2) {
                policy.cold_cutoff_us = std::max(policy.cold_cutoff_us, age + policy.hot_retention_us);
            }
        }
    }

    return policy;
}

WALRetentionManager::RetentionStats WALRetentionManager::GetStats() const {
    std::lock_guard lock(mutex_);
    return current_stats_;
}

void WALRetentionManager::UpdatePolicy(const RetentionPolicy& policy) {
    std::lock_guard lock(mutex_);

    auto now = std::chrono::system_clock::now();
    uint64_t now_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count());

    current_stats_.hot_zone_start_us = now_us - policy.hot_retention_us;
    current_stats_.cold_cutoff_us = now_us - policy.cold_cutoff_us;
    current_stats_.policy_updates++;

    // Note: actual WAL pruning is deferred — the LogManager doesn't currently
    // support log truncation. This policy is tracked for future implementation
    // and for SHOW AI STATUS reporting.
}

} // namespace ai
} // namespace chronosdb
