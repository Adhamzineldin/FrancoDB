#pragma once
#include <chrono>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ai/immune/anomaly_detector.h"

namespace chronosdb {

class LogManager;
class Catalog;
class IBufferManager;
class CheckpointManager;

namespace ai {

/**
 * ResponseEngine - Executes appropriate responses to detected anomalies.
 *
 * Severity responses:
 *   LOW    -> Log warning via Logger
 *   MEDIUM -> Block table/user mutations (reversible)
 *   HIGH   -> Auto-recover via TimeTravelEngine to pre-anomaly state
 */
class ResponseEngine {
public:
    ResponseEngine(LogManager* log_manager, Catalog* catalog,
                   IBufferManager* bpm, CheckpointManager* checkpoint_mgr);

    // Execute response for an anomaly
    void Respond(const AnomalyReport& report);

    // Check if a table/user is currently blocked
    bool IsTableBlocked(const std::string& table_name) const;
    bool IsUserBlocked(const std::string& user) const;

    // Check if a table is in cooldown (recently recovered)
    bool IsInCooldown(const std::string& table_name) const;

    // Admin: unblock
    void UnblockTable(const std::string& table_name);
    void UnblockUser(const std::string& user);

    // Status
    std::vector<std::string> GetBlockedTables() const;
    std::vector<std::string> GetBlockedUsers() const;

private:
    void RespondLow(const AnomalyReport& report);
    void RespondMedium(const AnomalyReport& report);
    void RespondHigh(const AnomalyReport& report);

    LogManager* log_manager_;
    Catalog* catalog_;
    IBufferManager* bpm_;
    CheckpointManager* checkpoint_mgr_;

    mutable std::shared_mutex blocked_mutex_;
    std::unordered_set<std::string> blocked_tables_;
    std::unordered_set<std::string> blocked_users_;

    // Cooldown: tables that were recently recovered (prevent re-triggering)
    // Maps table_name -> cooldown_end_time
    mutable std::shared_mutex cooldown_mutex_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> recovery_cooldown_;

    // Cooldown duration: 60 seconds after recovery before re-analyzing
    static constexpr auto RECOVERY_COOLDOWN = std::chrono::seconds(60);
};

} // namespace ai
} // namespace chronosdb
