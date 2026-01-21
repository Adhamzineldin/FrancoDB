#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include "catalog/catalog.h"
#include "buffer/buffer_pool_manager.h"
#include "network/database_registry.h"
#include "parser/statement.h" // Needed for StatementType
#include "recovery/log_manager.h" 

namespace francodb {
    class AuthManager; // Forward declaration
    class ExecutionEngine; // Forward declaration

    // GLOBAL POINTER DECLARATION (Extern)
    extern AuthManager *g_auth_manager;

    // RBAC Roles
    enum class UserRole {
        SUPERADMIN,
        ADMIN,
        NORMAL,
        READONLY,
        DENIED, 
    };

    struct UserInfo {
        std::string username;
        std::string password_hash;
        std::unordered_map<std::string, UserRole> db_roles;
    };

    class AuthManager {
    public:
        AuthManager(BufferPoolManager *system_bpm, Catalog *system_catalog, 
                    DatabaseRegistry *db_registry, LogManager *log_manager);

        ~AuthManager();

        bool CheckUserExists(const std::string &username);

        void InitializeSystemDatabase();

        // Authentication
        bool Authenticate(const std::string &username, const std::string &password, UserRole &out_role);

        // User Management
        bool CreateUser(const std::string &username, const std::string &password, UserRole role);

        bool DeleteUser(const std::string &username);

        bool SetUserRole(const std::string &username, UserRole new_role);

        UserRole GetUserRole(const std::string &username);

        UserRole GetUserRole(const std::string &username, const std::string &db_name);

        bool SetUserRole(const std::string &username, const std::string &db_name, UserRole role);

        std::vector<UserInfo> GetAllUsers();

        // Permission Checking
        bool HasPermission(UserRole role, StatementType stmt_type);

        bool HasDatabaseAccess(const std::string &username, const std::string &db_name);

        bool IsSuperAdmin(const std::string &username);

        void SaveUsers();

    private:
        std::string HashPassword(const std::string &password);

        void LoadUsers();

        BufferPoolManager *system_bpm_;
        Catalog *system_catalog_;
        ExecutionEngine *system_engine_; // Owns a private engine for system queries
        DatabaseRegistry *db_registry_;
        LogManager *log_manager_; // [ACID]

        std::unordered_map<std::string, UserInfo> users_cache_;
        bool initialized_;
    };
} // namespace francodb
