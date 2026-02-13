#pragma once

#include "execution/execution_result.h"
#include "common/auth_manager.h"
#include "catalog/catalog.h"
#include <memory>

namespace chronosdb {

// Forward declarations
class ShowDatabasesStatement;
class ShowTablesStatement;
class ShowStatusStatement;
class ShowUsersStatement;
class WhoAmIStatement;
class ShowAIStatusStatement;
class ShowAnomaliesStatement;
class ShowExecutionStatsStatement;
class SessionContext;
class DatabaseRegistry;

/**
 * SystemExecutor - System metadata and status operations
 * 
 * SOLID PRINCIPLE: Single Responsibility
 * This class handles all system introspection operations:
 * - SHOW DATABASES
 * - SHOW TABLES
 * - SHOW STATUS
 * - SHOW USERS
 * - WHOAMI
 * 
 * @author ChronosDB Team
 */
class SystemExecutor {
public:
    SystemExecutor(Catalog* catalog, AuthManager* auth_manager, DatabaseRegistry* db_registry)
        : catalog_(catalog), auth_manager_(auth_manager), db_registry_(db_registry) {}
    
    // ========================================================================
    // SYSTEM INTROSPECTION
    // ========================================================================
    
    /**
     * Show all databases the user has access to.
     */
    ExecutionResult ShowDatabases(ShowDatabasesStatement* stmt, SessionContext* session);
    
    /**
     * Show all tables in the current database.
     */
    ExecutionResult ShowTables(ShowTablesStatement* stmt, SessionContext* session);
    
    /**
     * Show system status information.
     */
    ExecutionResult ShowStatus(ShowStatusStatement* stmt, SessionContext* session);
    
    /**
     * Show all users (admin only).
     */
    ExecutionResult ShowUsers(ShowUsersStatement* stmt);
    
    /**
     * Show current user information.
     */
    ExecutionResult WhoAmI(WhoAmIStatement* stmt, SessionContext* session);

    // ========================================================================
    // AI LAYER INTROSPECTION
    // ========================================================================

    ExecutionResult ShowAIStatus(ShowAIStatusStatement* stmt);
    ExecutionResult ShowAnomalies(ShowAnomaliesStatement* stmt);
    ExecutionResult ShowExecutionStats(ShowExecutionStatsStatement* stmt);

private:
    Catalog* catalog_;
    AuthManager* auth_manager_;
    DatabaseRegistry* db_registry_;
};

} // namespace chronosdb

