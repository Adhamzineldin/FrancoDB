#include "ai/temporal/snapshot_scheduler.h"
#include "ai/metrics_store.h"
#include "common/logger.h"
#include "recovery/checkpoint_manager.h"

#include <chrono>

namespace chronosdb {
namespace ai {

SmartSnapshotScheduler::SmartSnapshotScheduler(
    CheckpointManager* checkpoint_mgr, LogManager* log_manager)
    : checkpoint_mgr_(checkpoint_mgr), log_manager_(log_manager) {}

void SmartSnapshotScheduler::Evaluate(
    const std::vector<TemporalHotspot>& hotspots,
    const std::vector<uint64_t>& change_points) {
    std::lock_guard lock(mutex_);

    auto now = std::chrono::system_clock::now();
    uint64_t now_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count());

    if (!ShouldSnapshot(now_us)) return;

    // Decision: trigger a checkpoint if either:
    // 1. There are hotspots but no recent snapshot near them
    // 2. A change point was detected recently

    bool should_trigger = false;

    // Check for recent change points (within the last analysis interval)
    for (uint64_t cp : change_points) {
        uint64_t cp_age = (now_us > cp) ? (now_us - cp) : 0;
        // If change point is within last 5 minutes
        if (cp_age < 5ULL * 60 * 1000000) {
            should_trigger = true;
            break;
        }
    }

    // Check hotspot density: if high density, ensure snapshot coverage
    for (const auto& hs : hotspots) {
        if (hs.density > 1.0 && hs.access_count >= 10) {
            should_trigger = true;
            break;
        }
    }

    if (should_trigger && checkpoint_mgr_) {
        LOG_INFO("TemporalIndex",
                 "Smart snapshot triggered (hotspots=" +
                 std::to_string(hotspots.size()) +
                 ", change_points=" + std::to_string(change_points.size()) + ")");

        checkpoint_mgr_->BeginCheckpoint();
        last_snapshot_time_us_ = now_us;
        total_snapshots_++;

        // Record in metrics
        MetricEvent metric{};
        metric.type = MetricType::SNAPSHOT_TRIGGERED;
        metric.timestamp_us = now_us;
        MetricsStore::Instance().Record(metric);
    }

    // Update scheduled snapshot list
    scheduled_snapshots_.clear();
    for (const auto& hs : hotspots) {
        scheduled_snapshots_.push_back(hs.center_timestamp_us);
    }
}

std::vector<uint64_t> SmartSnapshotScheduler::GetScheduledSnapshots() const {
    std::lock_guard lock(mutex_);
    return scheduled_snapshots_;
}

uint64_t SmartSnapshotScheduler::GetLastSnapshotTime() const {
    std::lock_guard lock(mutex_);
    return last_snapshot_time_us_;
}

size_t SmartSnapshotScheduler::GetTotalSnapshotsTriggered() const {
    std::lock_guard lock(mutex_);
    return total_snapshots_;
}

bool SmartSnapshotScheduler::ShouldSnapshot(uint64_t now_us) const {
    // Don't snapshot too frequently: minimum 30 seconds between snapshots
    if (last_snapshot_time_us_ > 0) {
        uint64_t elapsed = now_us - last_snapshot_time_us_;
        if (elapsed < 30ULL * 1000000) return false;
    }
    return true;
}

} // namespace ai
} // namespace chronosdb
