#include "ai/immune/immune_system.h"
#include "ai/immune/mutation_monitor.h"
#include "ai/immune/user_profiler.h"
#include "ai/immune/anomaly_detector.h"
#include "ai/immune/response_engine.h"
#include "ai/metrics_store.h"
#include "common/logger.h"

#include <sstream>

namespace chronosdb {
namespace ai {

ImmuneSystem::ImmuneSystem(LogManager* log_manager, Catalog* catalog,
                            IBufferManager* bpm,
                            CheckpointManager* checkpoint_mgr)
    : mutation_monitor_(std::make_unique<MutationMonitor>()),
      user_profiler_(std::make_unique<UserBehaviorProfiler>()),
      anomaly_detector_(std::make_unique<AnomalyDetector>()),
      response_engine_(std::make_unique<ResponseEngine>(
          log_manager, catalog, bpm, checkpoint_mgr)) {}

ImmuneSystem::~ImmuneSystem() {
    Stop();
}

bool ImmuneSystem::OnBeforeDML(const DMLEvent& event) {
    if (!active_.load()) return true;

    // Only check mutations (INSERT/UPDATE/DELETE), not SELECT
    if (event.operation == DMLOperation::SELECT) return true;

    // Check if table is blocked
    if (response_engine_->IsTableBlocked(event.table_name)) {
        return false; // Block the operation
    }

    // Check if user is blocked
    if (!event.user.empty() && response_engine_->IsUserBlocked(event.user)) {
        return false; // Block the operation
    }

    return true;
}

void ImmuneSystem::OnAfterDML(const DMLEvent& event) {
    if (!active_.load()) return;

    // Record mutation events
    if (event.operation != DMLOperation::SELECT) {
        mutation_monitor_->RecordMutation(event.table_name, event.operation,
                                           event.rows_affected,
                                           event.start_time_us);
    }

    // Record user behavior for all operations
    if (!event.user.empty()) {
        user_profiler_->RecordEvent(event.user, event.operation,
                                     event.table_name, event.start_time_us);
    }

    // Record in shared metrics store
    MetricEvent metric{};
    switch (event.operation) {
        case DMLOperation::INSERT: metric.type = MetricType::DML_INSERT; break;
        case DMLOperation::UPDATE: metric.type = MetricType::DML_UPDATE; break;
        case DMLOperation::DELETE_OP: metric.type = MetricType::DML_DELETE; break;
        case DMLOperation::SELECT: metric.type = MetricType::DML_SELECT; break;
    }
    metric.timestamp_us = event.start_time_us;
    metric.duration_us = event.duration_us;
    metric.session_id = event.session_id;
    metric.user = event.user;
    metric.table_name = event.table_name;
    metric.db_name = event.db_name;
    metric.rows_affected = event.rows_affected;
    MetricsStore::Instance().Record(metric);
}

void ImmuneSystem::PeriodicAnalysis() {
    if (!active_.load()) return;

    auto reports = anomaly_detector_->Analyze(*mutation_monitor_,
                                               *user_profiler_);
    for (const auto& report : reports) {
        anomaly_detector_->RecordAnomaly(report);
        response_engine_->Respond(report);
    }
}

std::string ImmuneSystem::GetSummary() const {
    size_t anomalies = anomaly_detector_->GetTotalAnomalies();
    auto blocked_tables = response_engine_->GetBlockedTables();
    auto blocked_users = response_engine_->GetBlockedUsers();
    auto tables = mutation_monitor_->GetMonitoredTables();

    std::ostringstream oss;
    oss << anomalies << " anomalies detected, "
        << blocked_tables.size() << " tables blocked, "
        << blocked_users.size() << " users blocked, "
        << tables.size() << " tables monitored";
    return oss.str();
}

std::vector<AnomalyReport> ImmuneSystem::GetRecentAnomalies(
    size_t max_count) const {
    return anomaly_detector_->GetRecentAnomalies(max_count);
}

void ImmuneSystem::Start() {
    active_ = true;
    periodic_task_id_ = AIScheduler::Instance().SchedulePeriodic(
        "ImmuneSystem::PeriodicAnalysis",
        IMMUNE_CHECK_INTERVAL_MS,
        [this]() { PeriodicAnalysis(); });
    LOG_INFO("ImmuneSystem", "Immune System started "
             "(z-score thresholds: LOW=" +
             std::to_string(ZSCORE_LOW_THRESHOLD) + ", MEDIUM=" +
             std::to_string(ZSCORE_MEDIUM_THRESHOLD) + ", HIGH=" +
             std::to_string(ZSCORE_HIGH_THRESHOLD) + ")");
}

void ImmuneSystem::Stop() {
    active_ = false;
    if (periodic_task_id_ != 0) {
        AIScheduler::Instance().Cancel(periodic_task_id_);
        periodic_task_id_ = 0;
    }
}

} // namespace ai
} // namespace chronosdb
