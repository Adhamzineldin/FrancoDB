#include "common/auth_manager.h"
#include "execution/execution_engine.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "storage/table/schema.h"
#include "common/exception.h"
#include "common/franco_net_config.h"
#include "common/config_manager.h"
#include <sstream>
#include <functional>
#include <iomanip>

namespace francodb {

    // DEFINITION OF GLOBAL VARIABLE
    AuthManager* g_auth_manager = nullptr;

    std::string AuthManager::HashPassword(const std::string &password) {
        std::hash<std::string> hasher;
        std::string data = password + net::PASSWORD_PEPPER;
        size_t hash = 0;
        const int kCost = 10000;
        for (int i = 0; i < kCost; i++) {
            hash = hasher(data + std::to_string(hash));
        }
        std::ostringstream oss;
        oss << std::hex << hash;
        return oss.str();
    }

    AuthManager::AuthManager(BufferPoolManager *system_bpm, Catalog *system_catalog)
        : system_bpm_(system_bpm), system_catalog_(system_catalog), initialized_(false) {
        // Create a private execution engine just for system queries
        system_engine_ = new ExecutionEngine(system_bpm_, system_catalog_);
        InitializeSystemDatabase();
        LoadUsers();
    }

    AuthManager::~AuthManager() {
        SaveUsers();
        delete system_engine_;
    }

    bool AuthManager::CheckUserExists(const std::string &username) {
        std::string select_sql = "2E5TAR * MEN franco_users WHERE username = '" + username + "';";
        Lexer lexer(select_sql);
        Parser parser(std::move(lexer));
        try {
            auto stmt = parser.ParseQuery();
            if (!stmt) return false;
            ExecutionResult res = system_engine_->Execute(stmt.get());
            return (res.success && res.result_set && !res.result_set->rows.empty());
        } catch (...) {
            return false;
        }
    }

    void AuthManager::InitializeSystemDatabase() {
        if (initialized_) return;

        if (system_catalog_->GetTable("franco_users") == nullptr) {
            std::vector<Column> user_cols;
            user_cols.emplace_back("username", TypeId::VARCHAR, 64, true);
            user_cols.emplace_back("password_hash", TypeId::VARCHAR, 128, false);
            user_cols.emplace_back("db_name", TypeId::VARCHAR, 64, false);
            user_cols.emplace_back("role", TypeId::VARCHAR, 16, false);
            Schema user_schema(user_cols);

            if (system_catalog_->CreateTable("franco_users", user_schema) == nullptr) {
                return; // Failed
            }
            system_catalog_->SaveCatalog();
        }

        auto &config = ConfigManager::GetInstance();
        std::string root_user = config.GetRootUsername();

        if (!CheckUserExists(root_user)) {
            std::string root_pass = config.GetRootPassword();
            std::string admin_hash = HashPassword(root_pass);
            std::string insert_sql = "EMLA GOWA franco_users ELKEYAM ('" + root_user + "', '" + admin_hash +
                                     "', 'default', 'SUPERADMIN');";

            Lexer lexer(insert_sql);
            Parser parser(std::move(lexer));
            auto stmt = parser.ParseQuery();
            if (stmt) system_engine_->Execute(stmt.get());

            system_catalog_->SaveCatalog();
            system_bpm_->FlushAllPages();
        }
        initialized_ = true;
    }

    void AuthManager::LoadUsers() {
        users_cache_.clear();
        try {
            std::string select_sql = "2E5TAR * MEN franco_users;";
            Lexer lexer(select_sql);
            Parser parser(std::move(lexer));
            auto stmt = parser.ParseQuery();
            if (!stmt) return;

            ExecutionResult res = system_engine_->Execute(stmt.get());
            if (!res.success || !res.result_set) return;

            for (const auto &row: res.result_set->rows) {
                if (row.size() < 4) continue;
                // Assuming row[0]=username, row[1]=hash, row[2]=db, row[3]=role
                std::string u_name = row[0];
                std::string u_hash = row[1];
                std::string u_db = row[2];
                std::string u_role_str = row[3];

                UserRole r = UserRole::USER;
                if (u_role_str == "SUPERADMIN") r = UserRole::SUPERADMIN;
                else if (u_role_str == "ADMIN") r = UserRole::ADMIN;
                else if (u_role_str == "READONLY") r = UserRole::READONLY;
                else if (u_role_str == "DENIED") r = UserRole::DENIED;

                users_cache_[u_name].username = u_name;
                users_cache_[u_name].password_hash = u_hash;
                users_cache_[u_name].db_roles[u_db] = r;
            }
        } catch (...) {}
    }

    void AuthManager::SaveUsers() {
        // Simple UPSERT logic simulation
        for (const auto &[username, user]: users_cache_) {
            for (const auto &[db, role]: user.db_roles) {
                std::string role_str;
                switch (role) {
                    case UserRole::SUPERADMIN: role_str = "SUPERADMIN"; break;
                    case UserRole::ADMIN: role_str = "ADMIN"; break;
                    case UserRole::USER: role_str = "USER"; break;
                    case UserRole::READONLY: role_str = "READONLY"; break;
                    case UserRole::DENIED: role_str = "DENIED"; break;
                }
                std::string insert_sql = "EMLA GOWA franco_users ELKEYAM ('" +
                                         username + "', '" + user.password_hash + "', '" +
                                         db + "', '" + role_str + "');";
                try {
                    Lexer lexer(insert_sql);
                    Parser parser(std::move(lexer));
                    auto stmt = parser.ParseQuery();
                    if (stmt) system_engine_->Execute(stmt.get());
                } catch (...) {}
            }
        }
        system_catalog_->SaveCatalog();
        system_bpm_->FlushAllPages();
    }

    static bool IsRoot(const std::string &username) {
        return username == ConfigManager::GetInstance().GetRootUsername();
    }

    bool AuthManager::Authenticate(const std::string &username, const std::string &password, UserRole &out_role) {
        if (IsRoot(username)) {
            std::string input_hash = HashPassword(password);
            std::string expected_hash = HashPassword(ConfigManager::GetInstance().GetRootPassword());
            if (input_hash == expected_hash) {
                out_role = UserRole::SUPERADMIN;
                return true;
            }
            return false;
        }
        LoadUsers();
        auto it = users_cache_.find(username);
        if (it == users_cache_.end()) return false;
        
        if (HashPassword(password) != it->second.password_hash) return false;
        
        out_role = UserRole::READONLY; // Default placeholder
        return true;
    }

    bool AuthManager::IsSuperAdmin(const std::string &username) {
        if (IsRoot(username)) return true;
        LoadUsers();
        auto it = users_cache_.find(username);
        if (it == users_cache_.end()) return false;
        for (const auto &[db, role]: it->second.db_roles) {
            if (role == UserRole::SUPERADMIN) return true;
        }
        return false;
    }

    UserRole AuthManager::GetUserRole(const std::string &username, const std::string &db_name) {
        if (IsSuperAdmin(username)) return UserRole::SUPERADMIN;
        LoadUsers();
        auto it = users_cache_.find(username);
        if (it == users_cache_.end()) return UserRole::DENIED;
        
        if (it->second.db_roles.count(db_name)) return it->second.db_roles.at(db_name);
        return UserRole::DENIED;
    }

    bool AuthManager::SetUserRole(const std::string &username, const std::string &db_name, UserRole role) {
        if (IsRoot(username)) return false;
        LoadUsers();
        users_cache_[username].username = username; // Ensure exists
        users_cache_[username].db_roles[db_name] = role;
        SaveUsers();
        return true;
    }

    bool AuthManager::CreateUser(const std::string &username, const std::string &password, UserRole role) {
        LoadUsers();
        if (users_cache_.count(username)) return false;
        users_cache_[username].username = username;
        users_cache_[username].password_hash = HashPassword(password);
        users_cache_[username].db_roles["default"] = role;
        SaveUsers();
        return true;
    }

    std::vector<UserInfo> AuthManager::GetAllUsers() {
        LoadUsers();
        std::vector<UserInfo> result;
        for (const auto &[u, info]: users_cache_) result.push_back(info);
        return result;
    }

    bool AuthManager::DeleteUser(const std::string &username) {
        if (IsRoot(username)) return false;
        LoadUsers();
        if (users_cache_.erase(username)) {
            // In a real system, you'd run a DELETE SQL here too
            SaveUsers();
            return true;
        }
        return false;
    }

    bool AuthManager::SetUserRole(const std::string &username, UserRole new_role) {
        return SetUserRole(username, "default", new_role);
    }

    UserRole AuthManager::GetUserRole(const std::string &username) {
        return GetUserRole(username, "default");
    }

    bool AuthManager::HasDatabaseAccess(const std::string &username, const std::string &db_name) {
        if (IsSuperAdmin(username)) return true;
        return GetUserRole(username, db_name) != UserRole::DENIED;
    }

    bool AuthManager::HasPermission(UserRole role, StatementType stmt_type) {
        if (role == UserRole::DENIED) return false;
        if (role == UserRole::SUPERADMIN || role == UserRole::ADMIN) return true;
        
        if (role == UserRole::USER) {
            // USER can DO data manipulation, but NOT schema changes
            switch (stmt_type) {
                case StatementType::SELECT:
                case StatementType::INSERT:
                case StatementType::UPDATE_CMD:
                case StatementType::DELETE_CMD: // Usually users can delete data
                case StatementType::BEGIN:
                case StatementType::COMMIT:
                case StatementType::ROLLBACK:
                    return true;
                default: return false;
            }
        }
        if (role == UserRole::READONLY) {
            return stmt_type == StatementType::SELECT;
        }
        return false;
    }
}