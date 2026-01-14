#include "common/auth_manager.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "storage/table/schema.h"
#include "common/exception.h"
#include "common/franco_net_config.h"
#include <sstream>
#include <functional>
#include <iomanip>

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

        // Create franco_users table
        std::vector<Column> user_cols;
        user_cols.emplace_back("username", TypeId::VARCHAR, static_cast<uint32_t>(64), true);  // Primary key
        user_cols.emplace_back("password_hash", TypeId::VARCHAR, static_cast<uint32_t>(128), false);
        user_cols.emplace_back("role", TypeId::VARCHAR, static_cast<uint32_t>(16), false);
        Schema user_schema(user_cols);

        if (system_catalog_->CreateTable("franco_users", user_schema) == nullptr) {
            throw Exception(ExceptionType::EXECUTION, "Failed to create franco_users table");
        }

        // Insert default admin user (from config)
        std::string admin_hash = HashPassword(net::DEFAULT_ADMIN_PASSWORD);
        std::string insert_sql = "EMLA GOWA franco_users ELKEYAM ('" + net::DEFAULT_ADMIN_USERNAME + "', '" + admin_hash + "', 'ADMIN');";
        
        Lexer lexer(insert_sql);
        Parser parser(std::move(lexer));
        auto stmt = parser.ParseQuery();
        if (stmt) {
            system_engine_->Execute(stmt.get());
        }

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
            if (row.size() < 3) continue;

            UserInfo user;
            user.username = row[0];
            user.password_hash = row[1];
            
            std::string role_str = row[2];
            if (role_str == "ADMIN") user.role = UserRole::ADMIN;
            else if (role_str == "USER") user.role = UserRole::USER;
            else if (role_str == "READONLY") user.role = UserRole::READONLY;
            else user.role = UserRole::READONLY; // Default

            users_cache_[user.username] = user;
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
            std::string role_str;
            switch (user.role) {
                case UserRole::ADMIN: role_str = "ADMIN"; break;
                case UserRole::USER: role_str = "USER"; break;
                case UserRole::READONLY: role_str = "READONLY"; break;
            }

            std::string insert_sql = "EMLA GOWA franco_users ELKEYAM ('" + 
                                    user.username + "', '" + 
                                    user.password_hash + "', '" + 
                                    role_str + "');";
            
            Lexer lexer(insert_sql);
            Parser parser(std::move(lexer));
            auto stmt = parser.ParseQuery();
            if (stmt) {
                system_engine_->Execute(stmt.get());
            }
        }
    }

    bool AuthManager::Authenticate(const std::string& username, const std::string& password, UserRole& out_role) {
        LoadUsers(); // Refresh cache
        
        auto it = users_cache_.find(username);
        if (it == users_cache_.end()) {
            return false;
        }

        std::string input_hash = HashPassword(password);
        if (input_hash != it->second.password_hash) {
            return false;
        }

        out_role = it->second.role;
        return true;
    }

    bool AuthManager::CreateUser(const std::string& username, const std::string& password, UserRole role) {
        LoadUsers();
        
        if (users_cache_.find(username) != users_cache_.end()) {
            return false; // User already exists
        }

        UserInfo new_user;
        new_user.username = username;
        new_user.password_hash = HashPassword(password);
        new_user.role = role;

        users_cache_[username] = new_user;
        SaveUsers();
        return true;
    }

    bool AuthManager::DeleteUser(const std::string& username) {
        LoadUsers();
        
        if (users_cache_.find(username) == users_cache_.end()) {
            return false;
        }

        users_cache_.erase(username);
        SaveUsers();
        return true;
    }

    UserRole AuthManager::GetUserRole(const std::string& username) {
        LoadUsers();
        auto it = users_cache_.find(username);
        if (it == users_cache_.end()) {
            return UserRole::READONLY; // Default to readonly if not found
        }
        return it->second.role;
    }

    std::vector<UserInfo> AuthManager::GetAllUsers() {
        LoadUsers();
        std::vector<UserInfo> users;
        users.reserve(users_cache_.size());
        for (const auto &pair : users_cache_) {
            users.push_back(pair.second);
        }
        return users;
    }

    bool AuthManager::SetUserRole(const std::string& username, UserRole new_role) {
        LoadUsers();
        auto it = users_cache_.find(username);
        if (it == users_cache_.end()) {
            return false; // User not found
        }
        it->second.role = new_role;
        SaveUsers();
        return true;
    }

    bool AuthManager::HasPermission(UserRole role, StatementType stmt_type) {
        switch (role) {
            case UserRole::ADMIN:
                // Admin can do everything
                return true;

            case UserRole::USER:
                // User can SELECT, INSERT, UPDATE, but not DROP, CREATE_DB, CREATE
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
                // Readonly can only SELECT
                return (stmt_type == StatementType::SELECT);

            default:
                return false;
        }
    }

} // namespace francodb
