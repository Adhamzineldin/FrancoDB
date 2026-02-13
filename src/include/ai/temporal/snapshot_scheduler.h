#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "ai/temporal/hotspot_detector.h"

namespace chronosdb {

class CheckpointManager;
class LogManager;

namespace ai {

/**
 * SmartSnapshotScheduler - Decides when to trigger snapshots.
 *
 * Based on learned hotspots (frequently queried time regions)
 * and change points (moments of significant data change).
 */
class SmartSnapshotScheduler {
public:
    SmartSnapshotScheduler(CheckpointManager* checkpoint_mgr,
                            LogManager* log_manager);

    // Evaluate whether new snapshots should be taken
    void Evaluate(const std::vector<TemporalHotspot>& hotspots,
                  const std::vector<uint64_t>& change_points);

    // Scheduled snapshot timestamps
    std::vector<uint64_t> GetScheduledSnapshots() const;

    uint64_t GetLastSnapshotTime() const;
    size_t GetTotalSnapshotsTriggered() const;

private:
    CheckpointManager* checkpoint_mgr_;
    LogManager* log_manager_;

    mutable std::mutex mutex_;
    std::vector<uint64_t> scheduled_snapshots_;
    uint64_t last_snapshot_time_us_{0};
    size_t total_snapshots_{0};

    bool ShouldSnapshot(uint64_t now_us) const;
};

} // namespace ai
} // namespace chronosdb
