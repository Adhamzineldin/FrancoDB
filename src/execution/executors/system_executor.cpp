/**
 * system_executor.cpp
 * 
 * Implementation of System Introspection Operations
 * Extracted from ExecutionEngine to satisfy Single Responsibility Principle.
 * 
 * @author ChronosDB Team
 */

#include "execution/system_executor.h"
#include "parser/statement.h"
#include "network/session_context.h"
#include "network/database_registry.h"
#include "common/chronos_net_config.h"
#include "ai/ai_manager.h"
#include "ai/learning/learning_engine.h"
#include "ai/learning/bandit.h"
#include "ai/immune/immune_system.h"
#include "ai/immune/anomaly_detector.h"
#include <filesystem>
#include <algorithm>
#include <set>
#include <sstream>
#include <iomanip>

namespace chronosdb {

// ============================================================================
// SHOW DATABASES
// ============================================================================
ExecutionResult SystemExecutor::ShowDatabases(ShowDatabasesStatement* stmt, SessionContext* session) {
    auto rs = std::make_shared<ResultSet>();
    rs->column_names.push_back("Database");

    // Use a set to avoid duplicates
    std::set<std::string> db_names;

    // Always show chronosdb if user has access
    if (auth_manager_->HasDatabaseAccess(session->current_user, "chronosdb")) {
        db_names.insert("chronosdb");
    }

    // Scan filesystem directory for persisted databases
    namespace fs = std::filesystem;
    auto& config = ConfigManager::GetInstance();
    std::string data_dir = config.GetDataDirectory();

    if (fs::exists(data_dir)) {
        for (const auto& entry : fs::directory_iterator(data_dir)) {
            std::string db_name;
            if (entry.is_directory()) {
                db_name = entry.path().filename().string();

                // Verify it's a valid database directory (contains .chronosdb file)
                fs::path db_file = entry.path() / (db_name + ".chronosdb");
                if (!fs::exists(db_file)) {
                    continue;
                }
            }

            if (!db_name.empty() && db_name != "system") {
                if (auth_manager_->HasDatabaseAccess(session->current_user, db_name)) {
                    db_names.insert(db_name);
                }
            }
        }
    }

    for (const auto& db_name : db_names) {
        rs->AddRow({db_name});
    }

    return ExecutionResult::Data(rs);
}

// ============================================================================
// SHOW TABLES
// ============================================================================
ExecutionResult SystemExecutor::ShowTables(ShowTablesStatement* stmt, SessionContext* session) {
    auto rs = std::make_shared<ResultSet>();
    rs->column_names = {"Tables_in_" + session->current_db};

    // Resolve catalog for the currently selected database
    Catalog* cat = nullptr;
    if (db_registry_) {
        if (auto entry = db_registry_->Get(session->current_db)) {
            cat = entry->catalog.get();
        }
        if (!cat) {
            cat = db_registry_->ExternalCatalog(session->current_db);
        }
    }
    if (!cat) {
        // Fallback to injected catalog (e.g., single-DB mode)
        cat = catalog_;
    }
    if (!cat) {
        return ExecutionResult::Error("No catalog available for current database");
    }

    try {
        std::vector<std::string> table_names = cat->GetAllTableNames();
        std::sort(table_names.begin(), table_names.end());

        for (const auto& name : table_names) {
            rs->AddRow({name});
        }

        return ExecutionResult::Data(rs);
    } catch (const std::exception& e) {
        return ExecutionResult::Error("Failed to retrieve tables: " + std::string(e.what()));
    }
}

// ============================================================================
// SHOW STATUS
// ============================================================================
ExecutionResult SystemExecutor::ShowStatus(ShowStatusStatement* stmt, SessionContext* session) {
    auto rs = std::make_shared<ResultSet>();
    rs->column_names = {"Variable", "Value"};

    // Current User
    rs->AddRow({"Current User", session->current_user.empty() ? "Guest" : session->current_user});

    // Current Database
    rs->AddRow({"Current Database", session->current_db});

    // Current Role
    std::string role_str;
    switch (session->role) {
        case UserRole::SUPERADMIN: role_str = "SUPERADMIN (Full Access)"; break;
        case UserRole::ADMIN: role_str = "ADMIN (Read/Write)"; break;
        case UserRole::NORMAL: role_str = "NORMAL (Read/Write)"; break;
        case UserRole::READONLY: role_str = "READONLY (Select Only)"; break;
        case UserRole::DENIED: role_str = "DENIED (No Access)"; break;
    }
    rs->AddRow({"Current Role", role_str});

    // Auth Status
    rs->AddRow({"Authenticated", session->is_authenticated ? "Yes" : "No"});

    return ExecutionResult::Data(rs);
}

// ============================================================================
// SHOW USERS
// ============================================================================
ExecutionResult SystemExecutor::ShowUsers(ShowUsersStatement* stmt) {
    std::vector<UserInfo> users = auth_manager_->GetAllUsers();
    auto rs = std::make_shared<ResultSet>();
    rs->column_names = {"Username", "Roles"};

    for (const auto& user : users) {
        std::string roles_desc;
        for (const auto& [db, role] : user.db_roles) {
            if (!roles_desc.empty()) roles_desc += ", ";
            roles_desc += db + ":";
            switch (role) {
                case UserRole::SUPERADMIN: roles_desc += "SUPER"; break;
                case UserRole::ADMIN: roles_desc += "ADMIN"; break;
                case UserRole::NORMAL: roles_desc += "NORMAL"; break;
                case UserRole::READONLY: roles_desc += "READONLY"; break;
                case UserRole::DENIED: roles_desc += "DENIED"; break;
                default: roles_desc += "UNKNOWN"; break;
            }
        }
        rs->AddRow({user.username, roles_desc});
    }
    return ExecutionResult::Data(rs);
}

// ============================================================================
// WHOAMI
// ============================================================================
ExecutionResult SystemExecutor::WhoAmI(WhoAmIStatement* stmt, SessionContext* session) {
    auto rs = std::make_shared<ResultSet>();
    rs->column_names = {"Current User", "Current DB", "Role"};

    std::string role_str = "USER";
    if (session->role == UserRole::SUPERADMIN) role_str = "SUPERADMIN";
    else if (session->role == UserRole::ADMIN) role_str = "ADMIN";
    else if (session->role == UserRole::READONLY) role_str = "READONLY";

    rs->AddRow({session->current_user, session->current_db, role_str});
    return ExecutionResult::Data(rs);
}

// ============================================================================
// SHOW AI STATUS
// ============================================================================
ExecutionResult SystemExecutor::ShowAIStatus(ShowAIStatusStatement* stmt) {
    auto rs = std::make_shared<ResultSet>();
    rs->column_names = {"Component", "Status", "Details"};

    auto& ai_mgr = ai::AIManager::Instance();
    if (!ai_mgr.IsInitialized()) {
        rs->AddRow({"AI Layer", "INACTIVE", "Not initialized"});
        return ExecutionResult::Data(rs);
    }

    auto status = ai_mgr.GetStatus();

    rs->AddRow({"Learning Engine",
                status.learning_engine_active ? "ACTIVE" : "INACTIVE",
                status.learning_summary});

    rs->AddRow({"Immune System",
                status.immune_system_active ? "ACTIVE" : "INACTIVE",
                status.immune_summary});

    rs->AddRow({"Temporal Index",
                status.temporal_index_active ? "ACTIVE" : "INACTIVE",
                status.temporal_summary});

    rs->AddRow({"Metrics Recorded", "INFO",
                std::to_string(status.metrics_recorded) + " events"});

    rs->AddRow({"Scheduled Tasks", "INFO",
                std::to_string(status.scheduled_tasks) + " tasks"});

    return ExecutionResult::Data(rs);
}

// ============================================================================
// SHOW ANOMALIES
// ============================================================================
ExecutionResult SystemExecutor::ShowAnomalies(ShowAnomaliesStatement* stmt) {
    auto rs = std::make_shared<ResultSet>();
    rs->column_names = {"Table", "Severity", "Z-Score", "Current Rate", "Mean Rate", "Detected At"};

    auto& ai_mgr = ai::AIManager::Instance();
    if (!ai_mgr.IsInitialized() || !ai_mgr.GetImmuneSystem()) {
        rs->AddRow({"N/A", "N/A", "N/A", "N/A", "N/A", "Immune System not active"});
        return ExecutionResult::Data(rs);
    }

    auto anomalies = ai_mgr.GetImmuneSystem()->GetRecentAnomalies(50);

    if (anomalies.empty()) {
        rs->AddRow({"(none)", "NORMAL", "0.00", "0.00", "0.00", "No anomalies detected"});
        return ExecutionResult::Data(rs);
    }

    for (const auto& a : anomalies) {
        std::string severity_str;
        switch (a.severity) {
            case ai::AnomalySeverity::LOW:    severity_str = "LOW";    break;
            case ai::AnomalySeverity::MEDIUM: severity_str = "MEDIUM"; break;
            case ai::AnomalySeverity::HIGH:   severity_str = "HIGH";   break;
            default:                          severity_str = "NONE";   break;
        }

        std::ostringstream z_oss, cr_oss, mr_oss;
        z_oss  << std::fixed << std::setprecision(2) << a.z_score;
        cr_oss << std::fixed << std::setprecision(2) << a.current_rate;
        mr_oss << std::fixed << std::setprecision(2) << a.mean_rate;

        rs->AddRow({a.table_name, severity_str, z_oss.str(),
                    cr_oss.str(), mr_oss.str(), std::to_string(a.timestamp_us)});
    }

    return ExecutionResult::Data(rs);
}

// ============================================================================
// SHOW EXECUTION STATS
// ============================================================================
ExecutionResult SystemExecutor::ShowExecutionStats(ShowExecutionStatsStatement* stmt) {
    auto rs = std::make_shared<ResultSet>();
    rs->column_names = {"Strategy", "Pulls", "Avg Reward", "UCB Score"};

    auto& ai_mgr = ai::AIManager::Instance();
    if (!ai_mgr.IsInitialized() || !ai_mgr.GetLearningEngine()) {
        rs->AddRow({"N/A", "0", "0.00", "Learning Engine not active"});
        return ExecutionResult::Data(rs);
    }

    auto arm_stats = ai_mgr.GetLearningEngine()->GetArmStats();

    for (const auto& arm : arm_stats) {
        std::string name = (arm.strategy == ai::ScanStrategy::SEQUENTIAL_SCAN)
                               ? "Sequential Scan"
                               : "Index Scan";

        std::ostringstream avg_oss, ucb_oss;
        avg_oss << std::fixed << std::setprecision(4) << arm.average_reward;
        ucb_oss << std::fixed << std::setprecision(4) << arm.ucb_score;

        rs->AddRow({name, std::to_string(arm.total_pulls),
                    avg_oss.str(), ucb_oss.str()});
    }

    uint64_t total = ai_mgr.GetLearningEngine()->GetTotalQueriesObserved();
    rs->AddRow({"Total Queries Observed", std::to_string(total), "", ""});
    rs->AddRow({"Summary", ai_mgr.GetLearningEngine()->GetSummary(), "", ""});

    return ExecutionResult::Data(rs);
}

} // namespace chronosdb

