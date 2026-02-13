#include "ai/immune/immune_system.h"
#include "ai/immune/mutation_monitor.h"
#include "ai/immune/user_profiler.h"
#include "ai/immune/anomaly_detector.h"
#include "ai/immune/response_engine.h"
#include "ai/metrics_store.h"
#include "common/logger.h"

#include <chrono>
#include <sstream>

namespace chronosdb {
namespace ai {

// System/internal tables that should not be monitored by the Immune System.
// These are modified during normal startup, auth, and catalog operations.
static bool IsSystemTable(const std::string& table_name) {
    // Internal tables start with "chronos_" (e.g. chronos_users, chronos_databases)
    if (table_name.size() >= 8 && table_name.compare(0, 8, "chronos_") == 0) return true;
    // System catalog tables
    if (table_name == "sys_tables" || table_name == "sys_columns" ||
        table_name == "sys_indexes") return true;
    return false;
}

// Warm-up period: collect baseline data before triggering any responses.
// This prevents false positives from startup mutations.
static constexpr uint64_t WARMUP_PERIOD_US = 30ULL * 1000000; // 30 seconds

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

    // Never block system tables - they are managed by the engine
    if (IsSystemTable(event.table_name)) return true;

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

    // Skip system tables entirely - don't even record their mutations
    if (IsSystemTable(event.table_name)) return;

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

    // Warm-up guard: don't trigger responses until we have enough baseline data.
    // This prevents false positives from startup/restore mutations.
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    if (now_us - start_time_us_ < WARMUP_PERIOD_US) {
        return; // Still collecting baseline, skip analysis
    }

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
    // Record start time for warm-up period
    start_time_us_ = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    active_ = true;
    periodic_task_id_ = AIScheduler::Instance().SchedulePeriodic(
        "ImmuneSystem::PeriodicAnalysis",
        IMMUNE_CHECK_INTERVAL_MS,
        [this]() { PeriodicAnalysis(); });
    LOG_INFO("ImmuneSystem", "Immune System started "
             "(z-score thresholds: LOW=" +
             std::to_string(ZSCORE_LOW_THRESHOLD) + ", MEDIUM=" +
             std::to_string(ZSCORE_MEDIUM_THRESHOLD) + ", HIGH=" +
             std::to_string(ZSCORE_HIGH_THRESHOLD) +
             ", warmup=" + std::to_string(WARMUP_PERIOD_US / 1000000) + "s)");
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
