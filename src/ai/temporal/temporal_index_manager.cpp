#include "ai/temporal/temporal_index_manager.h"
#include "ai/temporal/access_tracker.h"
#include "ai/temporal/hotspot_detector.h"
#include "ai/temporal/snapshot_scheduler.h"
#include "ai/temporal/retention_manager.h"
#include "ai/metrics_store.h"
#include "common/logger.h"

#include <chrono>
#include <sstream>

namespace chronosdb {
namespace ai {

TemporalIndexManager::TemporalIndexManager(
    LogManager* log_manager, Catalog* catalog,
    IBufferManager* bpm, CheckpointManager* checkpoint_mgr)
    : log_manager_(log_manager),
      catalog_(catalog),
      bpm_(bpm),
      checkpoint_mgr_(checkpoint_mgr),
      access_tracker_(std::make_unique<TemporalAccessTracker>()),
      hotspot_detector_(std::make_unique<HotspotDetector>()),
      snapshot_scheduler_(std::make_unique<SmartSnapshotScheduler>(
          checkpoint_mgr, log_manager)),
      retention_manager_(std::make_unique<WALRetentionManager>(log_manager)) {}

TemporalIndexManager::~TemporalIndexManager() {
    Stop();
}

void TemporalIndexManager::OnTimeTravelQuery(const std::string& table_name,
                                               uint64_t target_timestamp,
                                               const std::string& db_name) {
    if (!active_.load()) return;

    auto now = std::chrono::system_clock::now();
    uint64_t now_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count());

    TemporalAccessEvent event;
    event.queried_timestamp_us = target_timestamp;
    event.query_time_us = now_us;
    event.table_name = table_name;
    event.db_name = db_name;
    access_tracker_->RecordAccess(event);

    // Record in shared metrics store
    MetricEvent metric{};
    metric.type = MetricType::TIME_TRAVEL_QUERY;
    metric.timestamp_us = now_us;
    metric.table_name = table_name;
    metric.db_name = db_name;
    metric.target_timestamp = target_timestamp;
    MetricsStore::Instance().Record(metric);
}

void TemporalIndexManager::PeriodicAnalysis() {
    if (!active_.load()) return;

    // Step 1: Get all temporal access events
    auto events = access_tracker_->GetAllEvents();
    if (events.empty()) return;

    // Step 2: Detect hotspots via DBSCAN clustering
    auto hotspots = hotspot_detector_->DetectHotspots(events);

    // Step 3: Detect change points in access frequency
    auto histogram = access_tracker_->GetFrequencyHistogram(
        60000000); // 1-minute buckets
    std::vector<double> rates;
    std::vector<uint64_t> timestamps;
    for (const auto& bucket : histogram) {
        rates.push_back(static_cast<double>(bucket.access_count));
        timestamps.push_back(bucket.start_us);
    }
    auto change_points = hotspot_detector_->DetectChangePoints(rates, timestamps);

    // Step 4: Update cached hotspots
    {
        std::unique_lock lock(results_mutex_);
        current_hotspots_ = hotspots;
    }

    // Step 5: Evaluate snapshot scheduling
    snapshot_scheduler_->Evaluate(hotspots, change_points);

    // Step 6: Update retention policy
    auto policy = retention_manager_->ComputePolicy(*access_tracker_);
    retention_manager_->UpdatePolicy(policy);

    if (!hotspots.empty()) {
        LOG_DEBUG("TemporalIndex",
                  "Analysis: " + std::to_string(hotspots.size()) +
                  " hotspots, " + std::to_string(change_points.size()) +
                  " change points, " + std::to_string(events.size()) +
                  " access events");
    }
}

std::string TemporalIndexManager::GetSummary() const {
    std::shared_lock lock(results_mutex_);
    size_t accesses = access_tracker_->GetTotalAccessCount();
    size_t snapshots = snapshot_scheduler_->GetTotalSnapshotsTriggered();

    std::ostringstream oss;
    oss << current_hotspots_.size() << " hotspots detected, "
        << accesses << " time-travel queries tracked, "
        << snapshots << " smart snapshots triggered";
    return oss.str();
}

std::vector<TemporalHotspot> TemporalIndexManager::GetCurrentHotspots() const {
    std::shared_lock lock(results_mutex_);
    return current_hotspots_;
}

void TemporalIndexManager::Start() {
    active_ = true;
    periodic_task_id_ = AIScheduler::Instance().SchedulePeriodic(
        "TemporalIndex::PeriodicAnalysis",
        TEMPORAL_ANALYSIS_INTERVAL_MS,
        [this]() { PeriodicAnalysis(); });
    LOG_INFO("TemporalIndex", "Temporal Index Manager started "
             "(analysis interval=" +
             std::to_string(TEMPORAL_ANALYSIS_INTERVAL_MS) + "ms)");
}

void TemporalIndexManager::Stop() {
    active_ = false;
    if (periodic_task_id_ != 0) {
        AIScheduler::Instance().Cancel(periodic_task_id_);
        periodic_task_id_ = 0;
    }
}

} // namespace ai
} // namespace chronosdb
