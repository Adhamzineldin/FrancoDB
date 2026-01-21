#include "common/auth_manager.h"
#include "execution/execution_engine.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "storage/table/schema.h"
#include "common/exception.h"
#include "network/session_context.h"
#include "common/franco_net_config.h"
#include "common/config_manager.h"
#include <sstream>
#include <functional>
#include <iomanip>
#include <iostream>

namespace francodb {

    // Global instance pointer
    AuthManager* g_auth_manager = nullptr;

    // -------------------------------------------------------------------------
    // Helper: Password Hashing
    // -------------------------------------------------------------------------
    std::string AuthManager::HashPassword(const std::string &password) {
        std::hash<std::string> hasher;
        std::string data = password + net::PASSWORD_PEPPER;
        size_t hash = 0;
        // Basic stretching to prevent simple rainbow table attacks
        for (int i = 0; i < 100; i++) {
            hash = hasher(data + std::to_string(hash));
        }
        std::ostringstream oss;
        oss << std::hex << hash;
        return oss.str();
    }
    
    
    
    
    std::unique_ptr<SessionContext> CreateSystemSession() {
        auto session = std::make_unique<SessionContext>();
        session->is_authenticated = true;
        session->current_user = "INTERNAL_SYSTEM";
        session->current_db = "system";
        session->role = UserRole::SUPERADMIN;
        return session;
    }

    // -------------------------------------------------------------------------
    // Constructor / Destructor
    // -------------------------------------------------------------------------
    AuthManager::AuthManager(BufferPoolManager *system_bpm, Catalog *system_catalog, 
                             DatabaseRegistry *db_registry, LogManager *log_manager)
     : system_bpm_(system_bpm), 
       system_catalog_(system_catalog), 
       db_registry_(db_registry),
       log_manager_(log_manager), // [ACID]
       initialized_(false) {
    
        // [FIX] Now passing 5 arguments including log_manager
        system_engine_ = new ExecutionEngine(system_bpm_, system_catalog_, this, db_registry_, log_manager_);
    
        InitializeSystemDatabase();
        LoadUsers();
    }

    AuthManager::~AuthManager() {
        SaveUsers();
        if (system_engine_) delete system_engine_;
    }

    // -------------------------------------------------------------------------
    // System Initialization
    // -------------------------------------------------------------------------
    bool AuthManager::CheckUserExists(const std::string &username) {
        if (users_cache_.count(username)) return true;
        return false; 
    }

    void AuthManager::InitializeSystemDatabase() {
        if (initialized_) return;

        // 1. Create 'franco_users' table if missing
        if (system_catalog_->GetTable("franco_users") == nullptr) {
            std::vector<Column> user_cols;
            user_cols.emplace_back("username", TypeId::VARCHAR, static_cast<uint32_t>(64), true);
            user_cols.emplace_back("password_hash", TypeId::VARCHAR, static_cast<uint32_t>(128), false);
            user_cols.emplace_back("db_name", TypeId::VARCHAR, static_cast<uint32_t>(64), false); // Scope
            user_cols.emplace_back("role", TypeId::VARCHAR, static_cast<uint32_t>(16), false);    // Permission
            Schema user_schema(user_cols);

            if (system_catalog_->CreateTable("franco_users", user_schema) == nullptr) {
                std::cerr << "[AUTH] Failed to create system user table." << std::endl;
                return; 
            }
            system_catalog_->SaveCatalog();
        }

        // 2. Ensure Root/SuperAdmin exists
        auto &config = ConfigManager::GetInstance();
        std::string root_user = config.GetRootUsername();

        if (!CheckUserExists(root_user)) {
            std::string root_pass = config.GetRootPassword();
            std::string admin_hash = HashPassword(root_pass);
            
            // SUPERADMIN role on 'default' implies global superadmin
            std::string insert_sql = "EMLA GOWA franco_users ELKEYAM ('" + root_user + "', '" + admin_hash +
                                     "', 'default', 'SUPERADMIN');";

            Lexer lexer(insert_sql);
            Parser parser(std::move(lexer));
            auto stmt = parser.ParseQuery();
            
            // [FIX] Pass System Session
            auto session = CreateSystemSession();
            if (stmt) system_engine_->Execute(stmt.get(), session.get());

            system_catalog_->SaveCatalog();
            system_bpm_->FlushAllPages();
        }
        initialized_ = true;
    }

    // -------------------------------------------------------------------------
    // Persistence (Load / Save)
    // -------------------------------------------------------------------------
    void AuthManager::LoadUsers() {
        users_cache_.clear();
        try {
            std::string select_sql = "2E5TAR * MEN franco_users;";
            Lexer lexer(select_sql);
            Parser parser(std::move(lexer));
            auto stmt = parser.ParseQuery();
            if (!stmt) return;

            // [FIX] Pass System Session
            auto session = CreateSystemSession();
            ExecutionResult res = system_engine_->Execute(stmt.get(), session.get());
            
            if (!res.success || !res.result_set) return;

            for (const auto &row: res.result_set->rows) {
                if (row.size() < 4) continue;
                std::string u_name = row[0];
                std::string u_hash = row[1];
                std::string u_db = row[2];
                std::string u_role_str = row[3];

                // [FIX] Convert role string to UPPERCASE for case-insensitive comparison
                std::string upper_role = u_role_str;
                std::transform(upper_role.begin(), upper_role.end(), upper_role.begin(), 
                               [](unsigned char c){ return std::toupper(c); });

                UserRole r = UserRole::DENIED; 
    
                if (upper_role == "SUPERADMIN") r = UserRole::SUPERADMIN;
                else if (upper_role == "ADMIN") r = UserRole::ADMIN;
                else if (upper_role == "NORMAL") r = UserRole::NORMAL;
                else if (upper_role == "READONLY") r = UserRole::READONLY;
    
                users_cache_[u_name].username = u_name;
                users_cache_[u_name].password_hash = u_hash;
                users_cache_[u_name].db_roles[u_db] = r;
            }
        } catch (...) {}
    }

    void AuthManager::SaveUsers() {
        auto session = CreateSystemSession();

        // 1. Truncate table
        try {
            std::string clear_sql = "2EMSA7 MEN franco_users;"; 
            Lexer l_clear(clear_sql);
            Parser p_clear(std::move(l_clear));
            auto stmt_clear = p_clear.ParseQuery();
            // [FIX] Pass System Session
            if (stmt_clear) system_engine_->Execute(stmt_clear.get(), session.get());
        } catch (...) {}

        // 2. Re-insert
        for (const auto &[username, user]: users_cache_) {
            for (const auto &[db, role]: user.db_roles) {
                std::string role_str;
                switch (role) {
                    case UserRole::SUPERADMIN: role_str = "SUPERADMIN"; break;
                    case UserRole::ADMIN:      role_str = "ADMIN"; break;
                    case UserRole::NORMAL:     role_str = "NORMAL"; break;
                    case UserRole::READONLY:   role_str = "READONLY"; break;
                    case UserRole::DENIED:     role_str = "DENIED"; break;
                }
                
                std::string insert_sql = "EMLA GOWA franco_users ELKEYAM ('" +
                                         username + "', '" + user.password_hash + "', '" +
                                         db + "', '" + role_str + "');";
                try {
                    Lexer lexer(insert_sql);
                    Parser parser(std::move(lexer));
                    auto stmt = parser.ParseQuery();
                    // [FIX] Pass System Session
                    if (stmt) system_engine_->Execute(stmt.get(), session.get());
                } catch (...) {}
            }
        }
        system_catalog_->SaveCatalog();
        system_bpm_->FlushAllPages();
    }

    // -------------------------------------------------------------------------
    // Core Authentication Logic
    // -------------------------------------------------------------------------
    
    static bool IsRootConfig(const std::string &username) {
        return username == ConfigManager::GetInstance().GetRootUsername();
    }

    bool AuthManager::Authenticate(const std::string &username, const std::string &password, UserRole &out_role) {
        if (IsRootConfig(username)) {
            std::string input_hash = HashPassword(password);
            std::string expected_hash = HashPassword(ConfigManager::GetInstance().GetRootPassword());
            
            if (input_hash == expected_hash || password == ConfigManager::GetInstance().GetRootPassword()) {
                out_role = UserRole::SUPERADMIN;
                return true;
            }
            return false;
        }

        if (users_cache_.empty()) LoadUsers();

        auto it = users_cache_.find(username);
        if (it == users_cache_.end()) return false;
        
        if (HashPassword(password) != it->second.password_hash) return false;
        
        // Default role is READONLY until they select a specific DB
        out_role = UserRole::READONLY;
        return true;
    }

    bool AuthManager::IsSuperAdmin(const std::string &username) {
        if (IsRootConfig(username)) return true;
        
        auto it = users_cache_.find(username);
        if (it == users_cache_.end()) return false;

        for (const auto &[db, role]: it->second.db_roles) {
            if (role == UserRole::SUPERADMIN) return true;
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // Role & Permission Logic
    // -------------------------------------------------------------------------

    UserRole AuthManager::GetUserRole(const std::string &username, const std::string &db_name) {
        if (IsSuperAdmin(username)) return UserRole::SUPERADMIN;

        auto it = users_cache_.find(username);
        if (it == users_cache_.end()) return UserRole::DENIED;
        
        if (it->second.db_roles.count(db_name)) {
            return it->second.db_roles.at(db_name);
        }
        
        return UserRole::DENIED;
    }

    bool AuthManager::HasDatabaseAccess(const std::string &username, const std::string &db_name) {
        if (IsSuperAdmin(username)) return true;
        UserRole role = GetUserRole(username, db_name);
        return role != UserRole::DENIED;
    }

    // In auth_manager.cpp - Update HasPermission if needed
    bool AuthManager::HasPermission(UserRole role, StatementType stmt_type) {
        // 1. DENIED
        if (role == UserRole::DENIED) return false;

        // 2. SUPERADMIN - can do everything
        if (role == UserRole::SUPERADMIN) return true;
    
        // 3. ADMIN - can do everything in their database
        if (role == UserRole::ADMIN) {
            // Admins can create/drop databases and do all DDL/DML
            switch (stmt_type) {
                case StatementType::CREATE_DB:
                case StatementType::DROP_DB:
                case StatementType::USE_DB:
                case StatementType::CREATE_TABLE:
                case StatementType::DROP:
                case StatementType::CREATE_INDEX:
                case StatementType::INSERT:
                case StatementType::SELECT:
                case StatementType::UPDATE_CMD:
                case StatementType::DELETE_CMD:
                case StatementType::BEGIN:
                case StatementType::COMMIT:
                case StatementType::ROLLBACK:
                    return true;
                default:
                    return false; // User management reserved for SUPERADMIN
            }
        }

        // 4. NORMAL USER - Read/Write (DML) but no DDL
        if (role == UserRole::NORMAL) {
            switch (stmt_type) {
                case StatementType::SELECT:
                case StatementType::INSERT:
                case StatementType::UPDATE_CMD: 
                case StatementType::DELETE_CMD:
                case StatementType::BEGIN:
                case StatementType::COMMIT:
                case StatementType::ROLLBACK:
                    return true;
                default: 
                    return false;
            }
        }
    
        // 5. READONLY - only SELECT
        if (role == UserRole::READONLY) {
            return stmt_type == StatementType::SELECT;
        }

        return false;
    }

    // -------------------------------------------------------------------------
    // User Management Wrappers
    // -------------------------------------------------------------------------

    bool AuthManager::SetUserRole(const std::string &username, const std::string &db_name, UserRole role) {
        if (IsRootConfig(username)) return false;
        
        users_cache_[username].username = username; 
        users_cache_[username].db_roles[db_name] = role;
        SaveUsers();
        return true;
    }

    bool AuthManager::CreateUser(const std::string &username, const std::string &password, UserRole role) {
        if (users_cache_.count(username)) return false;
        
        users_cache_[username].username = username;
        users_cache_[username].password_hash = HashPassword(password);
        users_cache_[username].db_roles["default"] = role;
        
        SaveUsers();
        return true;
    }

    bool AuthManager::DeleteUser(const std::string &username) {
        if (IsRootConfig(username)) return false;
        
        if (users_cache_.erase(username)) {
            SaveUsers();
            return true;
        }
        return false;
    }

    std::vector<UserInfo> AuthManager::GetAllUsers() {
        if (users_cache_.empty()) LoadUsers();
        std::vector<UserInfo> result;
        for (const auto &[u, info]: users_cache_) result.push_back(info);
        return result;
    }

    bool AuthManager::SetUserRole(const std::string &username, UserRole new_role) {
        return SetUserRole(username, "francodb", new_role);
    }
    
    UserRole AuthManager::GetUserRole(const std::string &username) {
        return GetUserRole(username, "francodb");
    }

}