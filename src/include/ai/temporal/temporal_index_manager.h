#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "ai/ai_scheduler.h"
#include "ai/temporal/hotspot_detector.h"

namespace chronosdb {

class LogManager;
class Catalog;
class IBufferManager;
class CheckpointManager;

namespace ai {

class TemporalAccessTracker;
class SmartSnapshotScheduler;
class WALRetentionManager;

/**
 * TemporalIndexManager - Intelligent temporal index optimization.
 *
 * Observes time-travel queries, detects temporal hotspots,
 * schedules smart snapshots, and manages WAL retention.
 */
class TemporalIndexManager {
public:
    TemporalIndexManager(LogManager* log_manager, Catalog* catalog,
                          IBufferManager* bpm, CheckpointManager* checkpoint_mgr);
    ~TemporalIndexManager();

    // Called when a time-travel query is executed
    void OnTimeTravelQuery(const std::string& table_name,
                           uint64_t target_timestamp,
                           const std::string& db_name);

    // Periodic analysis (called by AIScheduler)
    void PeriodicAnalysis();

    // Status
    std::string GetSummary() const;
    std::vector<TemporalHotspot> GetCurrentHotspots() const;

    // Lifecycle
    void Start();
    void Stop();

private:
    LogManager* log_manager_;
    Catalog* catalog_;
    IBufferManager* bpm_;
    CheckpointManager* checkpoint_mgr_;

    std::unique_ptr<TemporalAccessTracker> access_tracker_;
    std::unique_ptr<HotspotDetector> hotspot_detector_;
    std::unique_ptr<SmartSnapshotScheduler> snapshot_scheduler_;
    std::unique_ptr<WALRetentionManager> retention_manager_;

    TaskId periodic_task_id_{0};
    std::atomic<bool> active_{false};

    // Cached analysis results
    mutable std::shared_mutex results_mutex_;
    std::vector<TemporalHotspot> current_hotspots_;
};

} // namespace ai
} // namespace chronosdb
