#include "ai/immune/response_engine.h"
#include "ai/ai_config.h"
#include "common/logger.h"
#include "recovery/time_travel_engine.h"
#include "recovery/checkpoint_manager.h"

namespace chronosdb {
namespace ai {

ResponseEngine::ResponseEngine(LogManager* log_manager, Catalog* catalog,
                                IBufferManager* bpm,
                                CheckpointManager* checkpoint_mgr)
    : log_manager_(log_manager),
      catalog_(catalog),
      bpm_(bpm),
      checkpoint_mgr_(checkpoint_mgr) {}

void ResponseEngine::Respond(const AnomalyReport& report) {
    switch (report.severity) {
        case AnomalySeverity::LOW:
            RespondLow(report);
            break;
        case AnomalySeverity::MEDIUM:
            RespondMedium(report);
            break;
        case AnomalySeverity::HIGH:
            RespondHigh(report);
            break;
        default:
            break;
    }
}

bool ResponseEngine::IsTableBlocked(const std::string& table_name) const {
    std::shared_lock lock(blocked_mutex_);
    return blocked_tables_.count(table_name) > 0;
}

bool ResponseEngine::IsUserBlocked(const std::string& user) const {
    std::shared_lock lock(blocked_mutex_);
    return blocked_users_.count(user) > 0;
}

void ResponseEngine::UnblockTable(const std::string& table_name) {
    std::unique_lock lock(blocked_mutex_);
    blocked_tables_.erase(table_name);
    LOG_INFO("ImmuneSystem", "Table '" + table_name + "' unblocked by admin");
}

void ResponseEngine::UnblockUser(const std::string& user) {
    std::unique_lock lock(blocked_mutex_);
    blocked_users_.erase(user);
    LOG_INFO("ImmuneSystem", "User '" + user + "' unblocked by admin");
}

std::vector<std::string> ResponseEngine::GetBlockedTables() const {
    std::shared_lock lock(blocked_mutex_);
    return {blocked_tables_.begin(), blocked_tables_.end()};
}

std::vector<std::string> ResponseEngine::GetBlockedUsers() const {
    std::shared_lock lock(blocked_mutex_);
    return {blocked_users_.begin(), blocked_users_.end()};
}

void ResponseEngine::RespondLow(const AnomalyReport& report) {
    LOG_WARN("ImmuneSystem",
             "[ANOMALY LOW] " + report.description);
}

void ResponseEngine::RespondMedium(const AnomalyReport& report) {
    LOG_WARN("ImmuneSystem",
             "[ANOMALY MEDIUM] Blocking mutations on table '" +
             report.table_name + "' - " + report.description);

    std::unique_lock lock(blocked_mutex_);
    blocked_tables_.insert(report.table_name);
    if (!report.user.empty()) {
        blocked_users_.insert(report.user);
    }
}

void ResponseEngine::RespondHigh(const AnomalyReport& report) {
    LOG_ERROR("ImmuneSystem",
              "[ANOMALY HIGH] Auto-recovering table '" +
              report.table_name + "' - " + report.description);

    // Block the table first to prevent further damage
    {
        std::unique_lock lock(blocked_mutex_);
        blocked_tables_.insert(report.table_name);
        if (!report.user.empty()) {
            blocked_users_.insert(report.user);
        }
    }

    // Auto-recover: roll back to RECOVERY_LOOKBACK_US before the anomaly
    if (!log_manager_ || !catalog_ || !bpm_) {
        LOG_ERROR("ImmuneSystem",
                  "Cannot auto-recover: missing dependencies");
        return;
    }

    uint64_t target_time = report.timestamp_us - RECOVERY_LOOKBACK_US;

    try {
        TimeTravelEngine time_travel(log_manager_, catalog_, bpm_,
                                      checkpoint_mgr_);
        std::string db_name = log_manager_->GetCurrentDatabase();
        auto result = time_travel.RecoverTo(target_time, db_name);

        if (result.success) {
            LOG_INFO("ImmuneSystem",
                     "[AUTO-RECOVERY] Successfully recovered to " +
                     std::to_string(RECOVERY_LOOKBACK_US / 1000000) +
                     "s before anomaly. Records processed: " +
                     std::to_string(result.records_processed) +
                     ", elapsed: " + std::to_string(result.elapsed_ms) + "ms");

            // Unblock table after successful recovery
            std::unique_lock lock(blocked_mutex_);
            blocked_tables_.erase(report.table_name);
        } else {
            LOG_ERROR("ImmuneSystem",
                      "[AUTO-RECOVERY FAILED] " + result.error_message +
                      ". Table remains blocked.");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("ImmuneSystem",
                  std::string("[AUTO-RECOVERY EXCEPTION] ") + e.what());
    }
}

} // namespace ai
} // namespace chronosdb
