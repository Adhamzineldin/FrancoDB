#include "ai/immune/immune_system.h"
#include "ai/immune/mutation_monitor.h"
#include "ai/immune/user_profiler.h"
#include "ai/immune/anomaly_detector.h"
#include "ai/immune/response_engine.h"
#include "ai/immune/threat_detector.h"
#include "ai/dml_observer.h"
#include "ai/ai_config.h"
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
          log_manager, catalog, bpm, checkpoint_mgr)),
      threat_detector_(std::make_unique<ThreatDetector>()) {}

ImmuneSystem::~ImmuneSystem() {
    Stop();
}

bool ImmuneSystem::OnBeforeDML(const DMLEvent& event) {
    if (!active_.load()) return true;

    // Never block system tables - they are managed by the engine
    if (IsSystemTable(event.table_name)) return true;

    // ================================================================
    // THREAT DETECTION: Analyze query content for SQL injection & XSS
    // Runs on ALL operations (including SELECT) before any other checks
    // ================================================================
    if (!event.query_text.empty()) {
        auto threat = threat_detector_->Analyze(event.query_text);
        if (threat.type != ThreatType::NONE) {
            auto report = ThreatDetector::ToAnomalyReport(
                threat, event.table_name, event.user);
            anomaly_detector_->RecordAnomaly(report);

            if (threat.severity >= AnomalySeverity::MEDIUM) {
                // Build descriptive block reason with attack type and severity
                std::string severity_label = (threat.severity == AnomalySeverity::HIGH)
                    ? "CRITICAL" : "WARNING";
                std::string attack_type = ThreatDetector::ThreatTypeToString(threat.type);
                std::string reason = "[IMMUNE:" + attack_type + ":" + severity_label + "] "
                    + threat.description;
                DMLObserverRegistry::SetBlockReason(reason);

                LOG_WARN("ImmuneSystem",
                         report.description + " [BLOCKED]");
                response_engine_->Respond(report);
                return false;
            }

            // LOW severity: suspicious, log warning but allow
            LOG_WARN("ImmuneSystem",
                     report.description + " [SUSPICIOUS]");
        }
    }

    // SELECT operations don't need mutation-rate checks
    if (event.operation == DMLOperation::SELECT) return true;

    // Check if table is blocked
    if (response_engine_->IsTableBlocked(event.table_name)) {
        DMLObserverRegistry::SetBlockReason(
            "[IMMUNE:TABLE_BLOCKED] Table '" + event.table_name +
            "' is currently blocked due to previous anomaly detection");
        return false;
    }

    // Check if user is blocked
    if (!event.user.empty() && response_engine_->IsUserBlocked(event.user)) {
        DMLObserverRegistry::SetBlockReason(
            "[IMMUNE:USER_BLOCKED] User '" + event.user +
            "' is currently blocked due to suspicious activity");
        return false;
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

    // Immediate detection: mass operations bypass warmup and periodic analysis.
    // A single DML affecting many rows is suspicious regardless of history.
    if (event.operation != DMLOperation::SELECT &&
        event.rows_affected >= MASS_OPERATION_ROW_THRESHOLD) {

        // Check if this table is in cooldown (recently handled)
        if (response_engine_->IsInCooldown(event.table_name)) {
            return; // Skip - already handled recently
        }

        AnomalySeverity severity;
        if (event.rows_affected >= MASS_OPERATION_ROW_THRESHOLD * 10) {
            severity = AnomalySeverity::HIGH;   // 500+ rows = auto-recover
        } else if (event.rows_affected >= MASS_OPERATION_ROW_THRESHOLD * 4) {
            severity = AnomalySeverity::MEDIUM;  // 200+ rows = block table
        } else {
            severity = AnomalySeverity::LOW;     // 50+ rows = log warning
        }

        std::string op_name;
        switch (event.operation) {
            case DMLOperation::INSERT:   op_name = "INSERT"; break;
            case DMLOperation::UPDATE:   op_name = "UPDATE"; break;
            case DMLOperation::DELETE_OP: op_name = "DELETE"; break;
            default: op_name = "DML"; break;
        }

        AnomalyReport report;
        report.table_name = event.table_name;
        report.user = event.user;
        report.severity = severity;
        report.z_score = static_cast<double>(event.rows_affected); // Use rows as "score"
        report.current_rate = static_cast<double>(event.rows_affected);
        report.mean_rate = 0.0;
        report.std_dev = 0.0;
        report.timestamp_us = event.start_time_us;
        report.description = "Mass " + op_name + " on '" + event.table_name +
            "': " + std::to_string(event.rows_affected) + " rows affected in single operation";

        anomaly_detector_->RecordAnomaly(report);
        response_engine_->Respond(report);

        LOG_WARN("ImmuneSystem", report.description +
                 " [severity=" + AnomalyDetector::SeverityToString(severity) + "]");
    }
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
        // Skip tables that are in cooldown (recently recovered)
        if (response_engine_->IsInCooldown(report.table_name)) {
            continue;
        }
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

std::vector<std::string> ImmuneSystem::GetBlockedTables() const {
    return response_engine_->GetBlockedTables();
}

std::vector<std::string> ImmuneSystem::GetBlockedUsers() const {
    return response_engine_->GetBlockedUsers();
}

std::vector<std::string> ImmuneSystem::GetMonitoredTables() const {
    return mutation_monitor_->GetMonitoredTables();
}

size_t ImmuneSystem::GetTotalAnomalies() const {
    return anomaly_detector_->GetTotalAnomalies();
}

uint64_t ImmuneSystem::GetTotalThreats() const {
    return threat_detector_->GetTotalThreatsDetected();
}

uint64_t ImmuneSystem::GetSQLInjectionCount() const {
    return threat_detector_->GetSQLInjectionCount();
}

uint64_t ImmuneSystem::GetXSSCount() const {
    return threat_detector_->GetXSSCount();
}

void ImmuneSystem::Decay(double decay_factor) {
    if (!active_.load()) return;

    LOG_INFO("ImmuneSystem", "Applying decay factor " +
             std::to_string(decay_factor) + " to adapt to workload changes");

    // Decay mutation monitor history
    mutation_monitor_->Decay(decay_factor);

    // User profiler would also be decayed here if it tracked historical data
}

void ImmuneSystem::PeriodicMaintenance() {
    if (!active_.load()) return;

    // Note: Decay is now called by AIManager with a dynamic activity-based factor.
    // This method is kept for any future non-decay maintenance tasks.
    LOG_INFO("ImmuneSystem", "Periodic maintenance complete. " + GetSummary());
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
