#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ai/ai_scheduler.h"
#include "ai/dml_observer.h"
#include "ai/immune/anomaly_detector.h"

namespace chronosdb {

class LogManager;
class Catalog;
class IBufferManager;
class CheckpointManager;

namespace ai {

class MutationMonitor;
class UserBehaviorProfiler;
class ResponseEngine;

/**
 * ImmuneSystem - Autonomous anomaly detection and self-healing.
 *
 * Implements IDMLObserver:
 *   OnBeforeDML() -> Checks blocked tables/users, returns false to block
 *   OnAfterDML()  -> Records mutations for baseline building
 *
 * Runs periodic analysis via AIScheduler to detect anomalies
 * and trigger appropriate responses (LOG / BLOCK / AUTO-RECOVER).
 */
class ImmuneSystem : public IDMLObserver {
public:
    ImmuneSystem(LogManager* log_manager, Catalog* catalog,
                 IBufferManager* bpm, CheckpointManager* checkpoint_mgr);
    ~ImmuneSystem() override;

    // IDMLObserver
    bool OnBeforeDML(const DMLEvent& event) override;
    void OnAfterDML(const DMLEvent& event) override;

    // Periodic analysis (called by AIScheduler)
    void PeriodicAnalysis();

    // Status
    std::string GetSummary() const;
    std::vector<AnomalyReport> GetRecentAnomalies(size_t max_count = 50) const;
    std::vector<std::string> GetBlockedTables() const;
    std::vector<std::string> GetBlockedUsers() const;
    std::vector<std::string> GetMonitoredTables() const;
    size_t GetTotalAnomalies() const;

    // Lifecycle
    void Start();
    void Stop();

private:
    std::unique_ptr<MutationMonitor> mutation_monitor_;
    std::unique_ptr<UserBehaviorProfiler> user_profiler_;
    std::unique_ptr<AnomalyDetector> anomaly_detector_;
    std::unique_ptr<ResponseEngine> response_engine_;

    TaskId periodic_task_id_{0};
    std::atomic<bool> active_{false};
    uint64_t start_time_us_{0}; // For warm-up period tracking
};

} // namespace ai
} // namespace chronosdb
