#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include "catalog/catalog.h"
#include "buffer/buffer_pool_manager.h"
#include "execution/execution_engine.h"

namespace francodb {

    // RBAC Roles
    enum class UserRole {
        SUPERADMIN, // Full access to everything (like root) - can manage users, databases, all operations
        ADMIN,      // Full access: CREATE, INSERT, SELECT, UPDATE, DELETE, DROP, CREATE_DB
        USER,       // Limited: SELECT, INSERT, UPDATE (no DROP, CREATE_DB)
        READONLY,   // SELECT only
        DENIED      // No access to the database
    };

    struct UserInfo {
        std::string username;
        std::string password_hash;  // Simple hash for now (in production use bcrypt/argon2)
        // Per-database roles: db_name -> role
        std::unordered_map<std::string, UserRole> db_roles;
    };

    /**
     * AuthManager handles user authentication and permission checking.
     * Uses a system database (system.francodb) to store users.
     */
    class AuthManager {
    public:
        AuthManager(BufferPoolManager* system_bpm, Catalog* system_catalog);
        ~AuthManager();

        // Initialize system database with default admin user
        void InitializeSystemDatabase();

        // Authentication
        bool Authenticate(const std::string& username, const std::string& password, UserRole& out_role);
        
        // User Management
        bool CreateUser(const std::string& username, const std::string& password, UserRole role);
        bool DeleteUser(const std::string& username);
        bool SetUserRole(const std::string& username, UserRole new_role);
        UserRole GetUserRole(const std::string& username);
        // Per-database role management
        UserRole GetUserRole(const std::string& username, const std::string& db_name);
        bool SetUserRole(const std::string& username, const std::string& db_name, UserRole role);
        std::vector<UserInfo> GetAllUsers();

        // Permission Checking (RBAC)
        bool HasPermission(UserRole role, StatementType stmt_type);
        
        // Check if user has access to a database (SUPERADMIN always has access)
        bool HasDatabaseAccess(const std::string& username, const std::string& db_name);
        
        // Check if user is SUPERADMIN
        bool IsSuperAdmin(const std::string& username);
        
        // Save users to system database (public for crash handling)
        void SaveUsers();

    private:
        // Simple hash function (for demo - use proper crypto in production)
        std::string HashPassword(const std::string& password);
        
        // Load users from system database into memory cache
        void LoadUsers();

        BufferPoolManager* system_bpm_;
        Catalog* system_catalog_;
        ExecutionEngine* system_engine_;
        
        // In-memory cache: username -> UserInfo
        std::unordered_map<std::string, UserInfo> users_cache_;
        bool initialized_;
    };

} // namespace francodb
