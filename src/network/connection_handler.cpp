#include "network/connection_handler.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "common/result_formatter.h"
#include "storage/disk/disk_manager.h"

#include <sstream>
#include <algorithm>
#include <filesystem>

namespace francodb {

    // Base ConnectionHandler
    ConnectionHandler::ConnectionHandler(ProtocolType protocol_type, ExecutionEngine *engine, AuthManager *auth_manager)
        : protocol_(std::unique_ptr<ProtocolSerializer>(CreateProtocol(protocol_type))),
          engine_(engine),
          session_(std::make_shared<SessionContext>()),
          auth_manager_(auth_manager) {
    }

    // ClientConnectionHandler (TEXT protocol)
    ClientConnectionHandler::ClientConnectionHandler(ExecutionEngine *engine, AuthManager *auth_manager)
        : ConnectionHandler(ProtocolType::TEXT, engine, auth_manager) {
    }

    std::string ClientConnectionHandler::ProcessRequest(const std::string &request) {
        std::string sql = request;
        // Clean up input
        sql.erase(std::remove(sql.begin(), sql.end(), '\n'), sql.end());
        sql.erase(std::remove(sql.begin(), sql.end(), '\r'), sql.end());
        
        if (sql == "exit" || sql == "quit") {
            return "Goodbye!\n";
        }

        try {
            Lexer lexer(sql);
            Parser parser(std::move(lexer));
            auto stmt = parser.ParseQuery();

            if (!stmt) {
                return "ERROR: Failed to parse query\n";
            }

            // Handle LOGIN
            if (stmt->GetType() == StatementType::LOGIN) {
                auto *login = dynamic_cast<LoginStatement *>(stmt.get());
                if (!login) return "ERROR: Invalid LOGIN\n";
                UserRole login_role;
                if (auth_manager_->Authenticate(login->username_, login->password_, login_role)) {
                    session_->is_authenticated = true;
                    session_->current_user = login->username_;
                    session_->current_db = "default";
                    // Use role from Authenticate, but verify with GetUserRole for per-db roles
                    session_->role = auth_manager_->GetUserRole(session_->current_user, session_->current_db);
                    // If Authenticate returned SUPERADMIN (for root), ensure it's preserved
                    if (login_role == UserRole::SUPERADMIN) {
                        session_->role = UserRole::SUPERADMIN;
                    }
                    return "LOGIN OK (Role: " + std::string(session_->role == UserRole::SUPERADMIN ? "SUPERADMIN" : 
                                                              session_->role == UserRole::ADMIN ? "ADMIN" : 
                                                              session_->role == UserRole::USER ? "USER" : 
                                                              session_->role == UserRole::READONLY ? "READONLY" : "DENIED") + ")\n";
                }
                return "ERROR: Authentication failed\n";
            }

            if (!session_->is_authenticated) {
                return "ERROR: Authentication required. Use: LOGIN <user> <pass>\n";
            }

            // Handle USE <db>
            if (stmt->GetType() == StatementType::USE_DB) {
                auto *use_db = dynamic_cast<UseDatabaseStatement *>(stmt.get());
                if (!use_db) return "ERROR: Invalid USE\n";
                
                // Check if user has access to this database
                if (!auth_manager_->HasDatabaseAccess(session_->current_user, use_db->db_name_)) {
                    return "ERROR: Access denied to database " + use_db->db_name_ + "\n";
                }
                
                session_->current_db = use_db->db_name_;
                // Get role for the new database - this should return SUPERADMIN if user is SUPERADMIN
                session_->role = auth_manager_->GetUserRole(session_->current_user, session_->current_db);
                // Ensure SUPERADMIN users always get SUPERADMIN role
                if (auth_manager_->IsSuperAdmin(session_->current_user)) {
                    session_->role = UserRole::SUPERADMIN;
                }
                return "Using database: " + use_db->db_name_ + "\n";
            }

            // Management commands
            if (stmt->GetType() == StatementType::WHOAMI) {
                return "User: " + session_->current_user + " | Role: " +
                       std::string(session_->role == UserRole::SUPERADMIN ? "SUPERADMIN" :
                                   session_->role == UserRole::ADMIN ? "ADMIN" :
                                   session_->role == UserRole::USER ? "USER" :
                                   session_->role == UserRole::READONLY ? "READONLY" : "DENIED") + "\n";
            }
            if (stmt->GetType() == StatementType::SHOW_STATUS) {
                return "User: " + session_->current_user + "\nDB: " + session_->current_db + "\n";
            }
            // CREATE USER
            if (stmt->GetType() == StatementType::CREATE_USER) {
                if (session_->role != UserRole::SUPERADMIN && session_->role != UserRole::ADMIN) return "ERROR: Permission denied. CREATE USER requires ADMIN or SUPERADMIN role.\n";
                auto *create_user = dynamic_cast<CreateUserStatement *>(stmt.get());
                if (!create_user) return "ERROR: Invalid CREATE USER\n";
                UserRole role;
                std::string input_role = create_user->role_;
                std::transform(input_role.begin(), input_role.end(), input_role.begin(), ::toupper);
                if (input_role == "SUPERADMIN") role = UserRole::SUPERADMIN;
                else if (input_role == "ADMIN") role = UserRole::ADMIN;
                else if (input_role == "USER") role = UserRole::USER;
                else if (input_role == "READONLY") role = UserRole::READONLY;
                else if (input_role == "DENIED") role = UserRole::DENIED;
                else return "ERROR: Invalid role. Must be SUPERADMIN, ADMIN, USER, READONLY, or DENIED\n";
                if (auth_manager_->CreateUser(create_user->username_, create_user->password_, role)) {
                    return "CREATE USER " + create_user->username_ + " OK\n";
                }
                return "ERROR: User already exists\n";
            }
            // ALTER USER ROLE IN DB
            if (stmt->GetType() == StatementType::ALTER_USER_ROLE) {
                if (session_->role != UserRole::SUPERADMIN && session_->role != UserRole::ADMIN) return "ERROR: Permission denied. ALTER USER requires ADMIN or SUPERADMIN role.\n";
                auto *alter_user = dynamic_cast<AlterUserRoleStatement *>(stmt.get());
                if (!alter_user) return "ERROR: Invalid ALTER USER\n";
                UserRole new_role;
                std::string input_role = alter_user->role_;
                std::transform(input_role.begin(), input_role.end(), input_role.begin(), ::toupper);
                if (input_role == "SUPERADMIN") {
                    // Only SUPERADMIN can assign SUPERADMIN role
                    if (session_->role != UserRole::SUPERADMIN) {
                        return "ERROR: Permission denied. Only SUPERADMIN can assign SUPERADMIN role.\n";
                    }
                    new_role = UserRole::SUPERADMIN;
                } else if (input_role == "ADMIN") new_role = UserRole::ADMIN;
                else if (input_role == "USER") new_role = UserRole::USER;
                else if (input_role == "READONLY") new_role = UserRole::READONLY;
                else if (input_role == "DENIED") new_role = UserRole::DENIED;
                else return "ERROR: Invalid role. Must be SUPERADMIN, ADMIN, USER, READONLY, or DENIED\n";
                // Support ALTER USER <username> ROLE <role> IN <db>
                // If db_name is empty, use current database
                std::string db = session_->current_db;
                if (!alter_user->db_name_.empty()) {
                    db = alter_user->db_name_;
                }
                if (auth_manager_->SetUserRole(alter_user->username_, db, new_role)) {
                    return "ALTER USER " + alter_user->username_ + " ROLE " + input_role + " IN " + db + " OK\n";
                }
                return "ERROR: User not found\n";
            }
            // DELETE USER
            if (stmt->GetType() == StatementType::DELETE_USER) {
                if (session_->role != UserRole::SUPERADMIN && session_->role != UserRole::ADMIN) return "ERROR: Permission denied. DELETE USER requires ADMIN or SUPERADMIN role.\n";
                auto *delete_user = dynamic_cast<DeleteUserStatement *>(stmt.get());
                if (!delete_user) return "ERROR: Invalid DELETE USER\n";
                if (auth_manager_->DeleteUser(delete_user->username_)) {
                    return "DELETE USER " + delete_user->username_ + " OK\n";
                }
                return "ERROR: User not found or cannot be deleted\n";
            }
            // SHOW USERS
            if (stmt->GetType() == StatementType::SHOW_USERS) {
                if (session_->role != UserRole::SUPERADMIN && session_->role != UserRole::ADMIN) return "ERROR: Permission denied.\n";
                auto users = auth_manager_->GetAllUsers();
                if (users.empty()) return "No users found\n";
                std::ostringstream out;
                out << "Users:\n";
                out << "  username | db | role\n";
                out << "  ----------------------\n";
                for (const auto &u : users) {
                    for (const auto& [db, role] : u.db_roles) {
                        std::string role_str =
                            (role == UserRole::SUPERADMIN) ? "SUPERADMIN" :
                            (role == UserRole::ADMIN) ? "ADMIN" :
                            (role == UserRole::USER) ? "USER" :
                            (role == UserRole::READONLY) ? "READONLY" : "DENIED";
                        out << "  " << u.username << " | " << db << " | " << role_str << "\n";
                    }
                }
                return out.str();
            }
            // SHOW DATABASES
            if (stmt->GetType() == StatementType::SHOW_DATABASES) {
                std::ostringstream out;
                out << "Databases:\n";
                
                // Check if user is SUPERADMIN (can see all databases)
                bool is_superadmin = auth_manager_->IsSuperAdmin(session_->current_user);
                
                if (std::filesystem::exists("data")) {
                    for (const auto& entry : std::filesystem::directory_iterator("data")) {
                        if (entry.is_regular_file() && entry.path().extension() == ".francodb") {
                            std::string db_name = entry.path().stem().string();
                            
                            // SUPERADMIN can see all databases, others only see accessible ones
                            if (is_superadmin || auth_manager_->HasDatabaseAccess(session_->current_user, db_name)) {
                                UserRole r = auth_manager_->GetUserRole(session_->current_user, db_name);
                                std::string role_str = (r == UserRole::SUPERADMIN) ? " (SUPERADMIN)" :
                                                       (r == UserRole::ADMIN) ? " (ADMIN)" :
                                                       (r == UserRole::USER) ? " (USER)" :
                                                       (r == UserRole::READONLY) ? " (READONLY)" : "";
                                out << "  " << db_name << role_str << "\n";
                            }
                        }
                    }
                }
                return out.str();
            }
            // SHOW TABLES
            if (stmt->GetType() == StatementType::SHOW_TABLES) {
                std::ostringstream out;
                out << "Tables in " << session_->current_db << ":\n";
                
                // Get catalog from execution engine
                Catalog* catalog = engine_->GetCatalog();
                if (catalog == nullptr) {
                    return "ERROR: Catalog not available\n";
                }
                
                // Get all table names from catalog
                std::vector<std::string> table_names = catalog->GetAllTableNames();
                if (table_names.empty()) {
                    out << "  (no tables)\n";
                } else {
                    for (const auto& table_name : table_names) {
                        out << "  " << table_name << "\n";
                    }
                }
                return out.str();
            }
            // CREATE DATABASE
            if (stmt->GetType() == StatementType::CREATE_DB) {
                if (!auth_manager_->HasPermission(session_->role, StatementType::CREATE_DB)) {
                    return "ERROR: Permission denied. CREATE DATABASE requires ADMIN role.\n";
                }
                auto *create_db = dynamic_cast<CreateDatabaseStatement *>(stmt.get());
                if (!create_db) return "ERROR: Invalid CREATE DATABASE\n";
                try {
                    std::filesystem::create_directories("data");
                    DiskManager new_db("data/" + create_db->db_name_);
                    // Set creator's role in new db: SUPERADMIN if they're SUPERADMIN, otherwise ADMIN
                    UserRole creator_role = (session_->role == UserRole::SUPERADMIN || 
                                            auth_manager_->IsSuperAdmin(session_->current_user)) 
                                            ? UserRole::SUPERADMIN : UserRole::ADMIN;
                    auth_manager_->SetUserRole(session_->current_user, create_db->db_name_, creator_role);
                    // Note: Other users will get DENIED by default (handled in GetUserRole when role not found)
                    return "CREATE DATABASE " + create_db->db_name_ + " OK\n";
                } catch (const std::exception &e) {
                    return std::string("ERROR: Failed to create database: ") + e.what() + "\n";
                }
            }
            // RBAC Permission Check before executing statement
            session_->role = auth_manager_->GetUserRole(session_->current_user, session_->current_db);
            if (!auth_manager_->HasPermission(session_->role, stmt->GetType())) {
                return "ERROR: Permission denied. Your role (" + 
                       std::string(session_->role == UserRole::SUPERADMIN ? "SUPERADMIN" : 
                                   session_->role == UserRole::ADMIN ? "ADMIN" : 
                                  session_->role == UserRole::USER ? "USER" : 
                                  session_->role == UserRole::READONLY ? "READONLY" : "DENIED") + 
                       ") does not have permission for this operation.\n";
            }

            ExecutionResult res = engine_->Execute(stmt.get());
            
            if (!res.success) {
                return "ERROR: " + res.message + "\n";
            } else if (res.result_set) {
                return ResultFormatter::Format(res.result_set);
            } else {
                return res.message + "\n";
            }
        } catch (const std::exception &e) {
            return "SYSTEM ERROR: " + std::string(e.what()) + "\n";
        }
    }

    // ApiConnectionHandler (JSON protocol)
    ApiConnectionHandler::ApiConnectionHandler(ExecutionEngine *engine, AuthManager *auth_manager)
        : ConnectionHandler(ProtocolType::JSON, engine, auth_manager) {
    }

    std::string ApiConnectionHandler::ProcessRequest(const std::string &request) {
        // Simple JSON request parsing - expects {"query": "SELECT ..."}
        std::string sql;
        
        // Extract SQL from JSON-like request
        size_t query_pos = request.find("\"query\"");
        if (query_pos != std::string::npos) {
            size_t colon = request.find(':', query_pos);
            size_t start = request.find('"', colon);
            size_t end = request.find('"', start + 1);
            if (start != std::string::npos && end != std::string::npos) {
                sql = request.substr(start + 1, end - start - 1);
            }
        } else {
            // Fallback: treat entire request as SQL
            sql = request;
        }

        try {
            Lexer lexer(sql);
            Parser parser(std::move(lexer));
            auto stmt = parser.ParseQuery();

            if (!stmt) {
                return protocol_->SerializeError("Failed to parse query");
            }

            // Handle LOGIN
            if (stmt->GetType() == StatementType::LOGIN) {
                auto *login = dynamic_cast<LoginStatement *>(stmt.get());
                if (!login) return protocol_->SerializeError("Invalid LOGIN");
                
                UserRole role;
                if (auth_manager_->Authenticate(login->username_, login->password_, role)) {
                    session_->is_authenticated = true;
                    session_->current_user = login->username_;
                    session_->role = role;
                    return protocol_->Serialize(ExecutionResult::Message("LOGIN OK (Role: " + 
                        std::string(role == UserRole::SUPERADMIN ? "SUPERADMIN" : 
                                   role == UserRole::ADMIN ? "ADMIN" : 
                                   role == UserRole::USER ? "USER" : "READONLY") + ")"));
                }
                return protocol_->SerializeError("Authentication failed");
            }

            if (!session_->is_authenticated) {
                return protocol_->SerializeError("Authentication required. Use LOGIN");
            }

            // Handle USE <db>
            if (stmt->GetType() == StatementType::USE_DB) {
                auto *use_db = dynamic_cast<UseDatabaseStatement *>(stmt.get());
                if (!use_db) return protocol_->SerializeError("Invalid USE");
                
                // Check if user has access to this database
                if (!auth_manager_->HasDatabaseAccess(session_->current_user, use_db->db_name_)) {
                    return protocol_->SerializeError("Access denied to database " + use_db->db_name_);
                }
                
                session_->current_db = use_db->db_name_;
                session_->role = auth_manager_->GetUserRole(session_->current_user, session_->current_db);
                // Ensure SUPERADMIN users always get SUPERADMIN role
                if (auth_manager_->IsSuperAdmin(session_->current_user)) {
                    session_->role = UserRole::SUPERADMIN;
                }
                return protocol_->Serialize(ExecutionResult::Message("Using database: " + use_db->db_name_));
            }

            // Handle CREATE DATABASE
            if (stmt->GetType() == StatementType::CREATE_DB) {
                if (!auth_manager_->HasPermission(session_->role, StatementType::CREATE_DB)) {
                    return protocol_->SerializeError("Permission denied. CREATE DATABASE requires ADMIN role.");
                }
                auto *create_db = dynamic_cast<CreateDatabaseStatement *>(stmt.get());
                if (!create_db) return protocol_->SerializeError("Invalid CREATE DATABASE");

                try {
                    std::filesystem::create_directories("data");
                    DiskManager new_db("data/" + create_db->db_name_);
                    // Set creator's role in new db: SUPERADMIN if they're SUPERADMIN, otherwise ADMIN
                    UserRole creator_role = (session_->role == UserRole::SUPERADMIN || 
                                            auth_manager_->IsSuperAdmin(session_->current_user)) 
                                            ? UserRole::SUPERADMIN : UserRole::ADMIN;
                    auth_manager_->SetUserRole(session_->current_user, create_db->db_name_, creator_role);
                    // Note: Other users will get DENIED by default (handled in GetUserRole when role not found)
                    return protocol_->Serialize(ExecutionResult::Message("CREATE DATABASE " + create_db->db_name_ + " OK"));
                } catch (const std::exception &e) {
                    return protocol_->SerializeError("Failed to create database: " + std::string(e.what()));
                }
            }

            // RBAC Permission Check
            if (!auth_manager_->HasPermission(session_->role, stmt->GetType())) {
                return protocol_->SerializeError("Permission denied. Your role does not have permission for this operation.");
            }

            // Handle CREATE DATABASE using separate file per DB (Option B)
            if (stmt->GetType() == StatementType::CREATE_DB) {
                auto *create_db = dynamic_cast<CreateDatabaseStatement *>(stmt.get());
                if (!create_db) return protocol_->SerializeError("Invalid CREATE DATABASE");

                try {
                    std::filesystem::create_directories("data");
                    DiskManager new_db("data/" + create_db->db_name_);
                    return protocol_->Serialize(
                        ExecutionResult::Message("CREATE DATABASE " + create_db->db_name_ + " OK"));
                } catch (const std::exception &e) {
                    return protocol_->SerializeError(std::string("Failed to create database: ") + e.what());
                }
            }

            ExecutionResult res = engine_->Execute(stmt.get());
            return protocol_->Serialize(res);
        } catch (const std::exception &e) {
            return protocol_->SerializeError(std::string(e.what()));
        }
    }

    // BinaryConnectionHandler (BINARY protocol)
    BinaryConnectionHandler::BinaryConnectionHandler(ExecutionEngine *engine, AuthManager *auth_manager)
        : ConnectionHandler(ProtocolType::BINARY, engine, auth_manager) {
    }

    std::string BinaryConnectionHandler::ProcessRequest(const std::string &request) {
        // For binary protocol, we'll treat it as text for now
        // In a real implementation, you'd parse binary packets
        std::string sql = request;
        
        // Skip binary header if present
        if (!sql.empty() && (sql[0] == 0x01 || sql[0] == 0x02)) {
            sql = sql.substr(1);
        }

        try {
            Lexer lexer(sql);
            Parser parser(std::move(lexer));
            auto stmt = parser.ParseQuery();

            if (!stmt) {
                return protocol_->SerializeError("Failed to parse query");
            }

            // Handle LOGIN
            if (stmt->GetType() == StatementType::LOGIN) {
                auto *login = dynamic_cast<LoginStatement *>(stmt.get());
                if (!login) return protocol_->SerializeError("Invalid LOGIN");
                
                UserRole role;
                if (auth_manager_->Authenticate(login->username_, login->password_, role)) {
                    session_->is_authenticated = true;
                    session_->current_user = login->username_;
                    session_->role = role;
                    return protocol_->Serialize(ExecutionResult::Message("LOGIN OK (Role: " + 
                        std::string(role == UserRole::SUPERADMIN ? "SUPERADMIN" : 
                                   role == UserRole::ADMIN ? "ADMIN" : 
                                   role == UserRole::USER ? "USER" : "READONLY") + ")"));
                }
                return protocol_->SerializeError("Authentication failed");
            }

            if (!session_->is_authenticated) {
                return protocol_->SerializeError("Authentication required. Use LOGIN");
            }

            // Handle USE <db>
            if (stmt->GetType() == StatementType::USE_DB) {
                auto *use_db = dynamic_cast<UseDatabaseStatement *>(stmt.get());
                if (!use_db) return protocol_->SerializeError("Invalid USE");
                
                // Check if user has access to this database
                if (!auth_manager_->HasDatabaseAccess(session_->current_user, use_db->db_name_)) {
                    return protocol_->SerializeError("Access denied to database " + use_db->db_name_);
                }
                
                session_->current_db = use_db->db_name_;
                session_->role = auth_manager_->GetUserRole(session_->current_user, session_->current_db);
                // Ensure SUPERADMIN users always get SUPERADMIN role
                if (auth_manager_->IsSuperAdmin(session_->current_user)) {
                    session_->role = UserRole::SUPERADMIN;
                }
                return protocol_->Serialize(ExecutionResult::Message("Using database: " + use_db->db_name_));
            }

            // Handle CREATE DATABASE
            if (stmt->GetType() == StatementType::CREATE_DB) {
                if (!auth_manager_->HasPermission(session_->role, StatementType::CREATE_DB)) {
                    return protocol_->SerializeError("Permission denied. CREATE DATABASE requires ADMIN role.");
                }
                auto *create_db = dynamic_cast<CreateDatabaseStatement *>(stmt.get());
                if (!create_db) return protocol_->SerializeError("Invalid CREATE DATABASE");

                try {
                    std::filesystem::create_directories("data");
                    DiskManager new_db("data/" + create_db->db_name_);
                    // Set creator's role in new db: SUPERADMIN if they're SUPERADMIN, otherwise ADMIN
                    UserRole creator_role = (session_->role == UserRole::SUPERADMIN || 
                                            auth_manager_->IsSuperAdmin(session_->current_user)) 
                                            ? UserRole::SUPERADMIN : UserRole::ADMIN;
                    auth_manager_->SetUserRole(session_->current_user, create_db->db_name_, creator_role);
                    // Note: Other users will get DENIED by default (handled in GetUserRole when role not found)
                    return protocol_->Serialize(ExecutionResult::Message("CREATE DATABASE " + create_db->db_name_ + " OK"));
                } catch (const std::exception &e) {
                    return protocol_->SerializeError("Failed to create database: " + std::string(e.what()));
                }
            }

            // RBAC Permission Check
            if (!auth_manager_->HasPermission(session_->role, stmt->GetType())) {
                return protocol_->SerializeError("Permission denied. Your role does not have permission for this operation.");
            }

            // Handle CREATE DATABASE using separate file per DB (Option B)
            if (stmt->GetType() == StatementType::CREATE_DB) {
                auto *create_db = dynamic_cast<CreateDatabaseStatement *>(stmt.get());
                if (!create_db) return protocol_->SerializeError("Invalid CREATE DATABASE");

                try {
                    std::filesystem::create_directories("data");
                    DiskManager new_db("data/" + create_db->db_name_);
                    return protocol_->Serialize(
                        ExecutionResult::Message("CREATE DATABASE " + create_db->db_name_ + " OK"));
                } catch (const std::exception &e) {
                    return protocol_->SerializeError(std::string("Failed to create database: ") + e.what());
                }
            }

            ExecutionResult res = engine_->Execute(stmt.get());
            return protocol_->Serialize(res);
        } catch (const std::exception &e) {
            return protocol_->SerializeError(std::string(e.what()));
        }
    }

} // namespace francodb
