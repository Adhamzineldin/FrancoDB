#include "common/auth_manager.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "storage/table/schema.h"
#include "common/exception.h"
#include "common/franco_net_config.h"
#include <sstream>
#include <functional>
#include <iomanip>
#include <algorithm> // For std::transform

namespace francodb {

    // Bcrypt-style password hashing (iterated with secret pepper from config).
    // NOTE: This is not a full bcrypt implementation, but it mimics the idea:
    //  - combine password + secret pepper
    //  - run through many hash iterations to slow down brute-force attacks
    std::string AuthManager::HashPassword(const std::string& password) {
        std::hash<std::string> hasher;

        // Combine password with secret pepper from config
        std::string data = password + net::PASSWORD_PEPPER;
        size_t hash = 0;

        // Cost factor (number of iterations) – can be tuned
        const int kCost = 10000;
        for (int i = 0; i < kCost; i++) {
            // Mix in previous hash value to make each round depend on the last
            hash = hasher(data + std::to_string(hash));
        }

        std::ostringstream oss;
        oss << std::hex << hash;
        return oss.str();
    }

    AuthManager::AuthManager(BufferPoolManager* system_bpm, Catalog* system_catalog)
        : system_bpm_(system_bpm), system_catalog_(system_catalog), initialized_(false) {
        system_engine_ = new ExecutionEngine(system_bpm_, system_catalog_);
        InitializeSystemDatabase();
        LoadUsers();
    }

    AuthManager::~AuthManager() {
        SaveUsers();
        delete system_engine_;
    }

    void AuthManager::InitializeSystemDatabase() {
        if (initialized_) return;

        // Check if franco_users table exists
        if (system_catalog_->GetTable("franco_users") != nullptr) {
            initialized_ = true;
            return;
        }

        // Create franco_users table (this will create system database files if they don't exist)
        std::vector<Column> user_cols;
        user_cols.emplace_back("username", TypeId::VARCHAR, static_cast<uint32_t>(64), true);  // Primary key
        user_cols.emplace_back("password_hash", TypeId::VARCHAR, static_cast<uint32_t>(128), false);
        user_cols.emplace_back("db_name", TypeId::VARCHAR, static_cast<uint32_t>(64), false);
        user_cols.emplace_back("role", TypeId::VARCHAR, static_cast<uint32_t>(16), false);
        Schema user_schema(user_cols);

        if (system_catalog_->CreateTable("franco_users", user_schema) == nullptr) {
            throw Exception(ExceptionType::EXECUTION, "Failed to create franco_users table");
        }

        // Insert default admin user (from config) as SUPERADMIN
        // This ensures default user exists even if config file exists but system files don't
        auto& config = ConfigManager::GetInstance();
        std::string root_user = config.GetRootUsername();
        std::string root_pass = config.GetRootPassword();
        std::string admin_hash = HashPassword(root_pass);
        std::string insert_sql = "EMLA GOWA franco_users ELKEYAM ('" + root_user + "', '" + admin_hash + "', 'default', 'SUPERADMIN');";
        
        Lexer lexer(insert_sql);
        Parser parser(std::move(lexer));
        auto stmt = parser.ParseQuery();
        if (stmt) {
            system_engine_->Execute(stmt.get());
        }

        // CRITICAL: Save catalog to ensure system files are written to disk
        // This is important when config exists but system files don't
        system_catalog_->SaveCatalog();
        system_bpm_->FlushAllPages();

        initialized_ = true;
    }

    void AuthManager::LoadUsers() {
        users_cache_.clear();

        // Query all users from franco_users table
        std::string select_sql = "2E5TAR * MEN franco_users;";
        Lexer lexer(select_sql);
        Parser parser(std::move(lexer));
        auto stmt = parser.ParseQuery();
        
        if (!stmt) return;

        ExecutionResult res = system_engine_->Execute(stmt.get());
        if (!res.success || !res.result_set) return;

        // Parse result set into UserInfo objects
        for (const auto& row : res.result_set->rows) {
            if (row.size() < 4) continue;

            std::string username = row[0];
            std::string password_hash = row[1];
            std::string db = row[2];
            std::string role_str = row[3];

            UserRole role;
            if (role_str == "SUPERADMIN") role = UserRole::SUPERADMIN;
            else if (role_str == "ADMIN") role = UserRole::ADMIN;
            else if (role_str == "USER") role = UserRole::USER;
            else if (role_str == "READONLY") role = UserRole::READONLY;
            else if (role_str == "DENIED") role = UserRole::DENIED;
            else role = UserRole::DENIED;

            if (!users_cache_.count(username)) {
                UserInfo info;
                info.username = username;
                info.password_hash = password_hash;
                users_cache_[username] = info;
            }
            users_cache_[username].db_roles[db] = role;
        }
    }

    void AuthManager::SaveUsers() {
        // Clear existing users (simple approach - in production use UPDATE)
        std::string delete_sql = "2EMSA7 MEN franco_users;";
        Lexer lexer_del(delete_sql);
        Parser parser_del(std::move(lexer_del));
        auto del_stmt = parser_del.ParseQuery();
        if (del_stmt) {
            system_engine_->Execute(del_stmt.get());
        }

        // Insert all cached users
        for (const auto& [username, user] : users_cache_) {
            for (const auto& [db, role] : user.db_roles) {
                std::string role_str;
                switch (role) {
                    case UserRole::SUPERADMIN: role_str = "SUPERADMIN"; break;
                    case UserRole::ADMIN: role_str = "ADMIN"; break;
                    case UserRole::USER: role_str = "USER"; break;
                    case UserRole::READONLY: role_str = "READONLY"; break;
                    case UserRole::DENIED: role_str = "DENIED"; break;
                }

                std::string insert_sql = "EMLA GOWA franco_users ELKEYAM ('" + 
                                    username + "', '" + 
                                    user.password_hash + "', '" + 
                                    db + "', '" + 
                                    role_str + "');";
                
                Lexer lexer(insert_sql);
                Parser parser(std::move(lexer));
                auto stmt = parser.ParseQuery();
                if (stmt) {
                    system_engine_->Execute(stmt.get());
                }
            }
        }
    }

    // Helper: check if user is root/superadmin (from config)
    static bool IsRoot(const std::string& username) {
        auto& config = ConfigManager::GetInstance();
        return username == config.GetRootUsername();
    }

    // Updated Authenticate: only checks password
    bool AuthManager::Authenticate(const std::string& username, const std::string& password, UserRole& out_role) {
        // Check if root user first (before loading users)
        if (IsRoot(username)) {
            auto& config = ConfigManager::GetInstance();
            std::string input_hash = HashPassword(password);
            std::string expected_hash = HashPassword(config.GetRootPassword());
            if (input_hash == expected_hash) {
                out_role = UserRole::SUPERADMIN; // Root is always SUPERADMIN
                return true;
            }
            return false;
        }
        LoadUsers();
        auto it = users_cache_.find(username);
        if (it == users_cache_.end()) return false;
        std::string input_hash = HashPassword(password);
        if (input_hash != it->second.password_hash) return false;
        out_role = UserRole::READONLY; // role is per-db, so default here
        return true;
    }

    // Check if user is SUPERADMIN
    bool AuthManager::IsSuperAdmin(const std::string& username) {
        if (IsRoot(username)) return true; // maayn is always SUPERADMIN
        LoadUsers();
        auto it = users_cache_.find(username);
        if (it == users_cache_.end()) return false;
        // Check if user has SUPERADMIN role in any database
        for (const auto& [db, role] : it->second.db_roles) {
            if (role == UserRole::SUPERADMIN) return true;
        }
        return false;
    }

    // Per-db role getter
    UserRole AuthManager::GetUserRole(const std::string& username, const std::string& db_name) {
        if (IsSuperAdmin(username)) return UserRole::SUPERADMIN; // SUPERADMIN has SUPERADMIN role in all databases
        LoadUsers();
        auto it = users_cache_.find(username);
        if (it == users_cache_.end()) return UserRole::DENIED;
        auto role_it = it->second.db_roles.find(db_name);
        if (role_it == it->second.db_roles.end()) return UserRole::DENIED;
        return role_it->second;
    }

    // Per-db role setter
    bool AuthManager::SetUserRole(const std::string& username, const std::string& db_name, UserRole role) {
        if (IsRoot(username)) return false; // maayn (superadmin) cannot be changed
        LoadUsers();
        auto it = users_cache_.find(username);
        if (it == users_cache_.end()) {
            // User doesn't exist in cache, create entry
            UserInfo new_user;
            new_user.username = username;
            new_user.password_hash = ""; // Will be set when user is created
            users_cache_[username] = new_user;
            it = users_cache_.find(username);
        }
        it->second.db_roles[db_name] = role;
        SaveUsers();
        return true;
    }

    // CreateUser: set default role for 'default' db
    bool AuthManager::CreateUser(const std::string& username, const std::string& password, UserRole role) {
        LoadUsers();
        if (users_cache_.find(username) != users_cache_.end()) return false;
        UserInfo new_user;
        new_user.username = username;
        new_user.password_hash = HashPassword(password);
        new_user.db_roles["default"] = role;
        users_cache_[username] = new_user;
        SaveUsers();
        return true;
    }

    // GetAllUsers: return all users from cache
    std::vector<UserInfo> AuthManager::GetAllUsers() {
        LoadUsers();
        std::vector<UserInfo> result;
        for (const auto& [username, user] : users_cache_) {
            result.push_back(user);
        }
        return result;
    }

    // DeleteUser: remove user from system
    bool AuthManager::DeleteUser(const std::string& username) {
        if (IsRoot(username)) return false; // Cannot delete root/superadmin
        LoadUsers();
        auto it = users_cache_.find(username);
        if (it == users_cache_.end()) return false;
        users_cache_.erase(it);
        SaveUsers();
        return true;
    }

    // SetUserRole: set role for current/default database (single parameter version)
    bool AuthManager::SetUserRole(const std::string& username, UserRole new_role) {
        if (IsRoot(username)) return false; // Cannot change root
        LoadUsers();
        auto it = users_cache_.find(username);
        if (it == users_cache_.end()) return false;
        // Set role for "default" database
        it->second.db_roles["default"] = new_role;
        SaveUsers();
        return true;
    }

    // GetUserRole: get role for default database (single parameter version)
    UserRole AuthManager::GetUserRole(const std::string& username) {
        return GetUserRole(username, "default");
    }

    // Check if user has access to a database (SUPERADMIN always has access)
    bool AuthManager::HasDatabaseAccess(const std::string& username, const std::string& db_name) {
        if (IsSuperAdmin(username)) return true; // SUPERADMIN has access to all databases
        UserRole role = GetUserRole(username, db_name);
        return role != UserRole::DENIED;
    }

    // HasPermission: deny all for DENIED, always allow for SUPERADMIN and ADMIN
    bool AuthManager::HasPermission(UserRole role, StatementType stmt_type) {
        if (role == UserRole::DENIED) return false;
        if (role == UserRole::SUPERADMIN || role == UserRole::ADMIN) return true;
        switch (role) {
            case UserRole::USER:
                switch (stmt_type) {
                    case StatementType::SELECT:
                    case StatementType::INSERT:
                    case StatementType::UPDATE_CMD:
                    case StatementType::CREATE_INDEX:
                    case StatementType::BEGIN:
                    case StatementType::COMMIT:
                    case StatementType::ROLLBACK:
                        return true;
                    case StatementType::DROP:
                    case StatementType::DELETE_CMD:
                    case StatementType::CREATE:
                    case StatementType::CREATE_DB:
                        return false;
                    default:
                        return false;
                }
            case UserRole::READONLY:
                return (stmt_type == StatementType::SELECT);
            default:
                return false;
        }
    }

} // namespace francodb
