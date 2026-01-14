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
                
                UserRole role;
                if (auth_manager_->Authenticate(login->username_, login->password_, role)) {
                    session_->is_authenticated = true;
                    session_->current_user = login->username_;
                    session_->role = role;
                    return "LOGIN OK (Role: " + std::string(role == UserRole::ADMIN ? "ADMIN" : 
                                                              role == UserRole::USER ? "USER" : "READONLY") + ")\n";
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
                session_->current_db = use_db->db_name_;
                return "Using database: " + use_db->db_name_ + "\n";
            }

            // Management commands
            if (stmt->GetType() == StatementType::WHOAMI) {
                return "User: " + session_->current_user + " | Role: " +
                       std::string(session_->role == UserRole::ADMIN ? "ADMIN" :
                                   session_->role == UserRole::USER ? "USER" : "READONLY") + "\n";
            }
            if (stmt->GetType() == StatementType::SHOW_STATUS) {
                return "User: " + session_->current_user + "\nDB: " + session_->current_db + "\n";
            }
            // CREATE USER
            if (stmt->GetType() == StatementType::CREATE_USER) {
                if (session_->role != UserRole::ADMIN) return "ERROR: Permission denied. CREATE USER requires ADMIN role.\n";
                auto *create_user = dynamic_cast<CreateUserStatement *>(stmt.get());
                if (!create_user) return "ERROR: Invalid CREATE USER\n";
                
                UserRole role;
                if (create_user->role_ == "ADMIN") role = UserRole::ADMIN;
                else if (create_user->role_ == "USER") role = UserRole::USER;
                else if (create_user->role_ == "READONLY") role = UserRole::READONLY;
                else return "ERROR: Invalid role. Must be ADMIN, USER, or READONLY\n";
                
                if (auth_manager_->CreateUser(create_user->username_, create_user->password_, role)) {
                    return "CREATE USER " + create_user->username_ + " OK\n";
                }
                return "ERROR: User already exists\n";
            }
            
            // ALTER USER ROLE
            if (stmt->GetType() == StatementType::ALTER_USER_ROLE) {
                if (session_->role != UserRole::ADMIN) return "ERROR: Permission denied. ALTER USER requires ADMIN role.\n";
                auto *alter_user = dynamic_cast<AlterUserRoleStatement *>(stmt.get());
                if (!alter_user) return "ERROR: Invalid ALTER USER\n";
                
                UserRole new_role;
                if (alter_user->role_ == "ADMIN") new_role = UserRole::ADMIN;
                else if (alter_user->role_ == "USER") new_role = UserRole::USER;
                else if (alter_user->role_ == "READONLY") new_role = UserRole::READONLY;
                else return "ERROR: Invalid role. Must be ADMIN, USER, or READONLY\n";
                
                if (auth_manager_->SetUserRole(alter_user->username_, new_role)) {
                    return "ALTER USER " + alter_user->username_ + " ROLE " + alter_user->role_ + " OK\n";
                }
                return "ERROR: User not found\n";
            }
            
            // SHOW USERS
            if (stmt->GetType() == StatementType::SHOW_USERS) {
                if (session_->role != UserRole::ADMIN) return "ERROR: Permission denied.\n";

                // Ask AuthManager for all users from the system DB cache
                auto users = auth_manager_->GetAllUsers();
                if (users.empty()) {
                    return "No users found\n";
                }

                std::ostringstream out;
                out << "Users:\n";
                out << "  username | role\n";
                out << "  ----------------\n";
                for (const auto &u : users) {
                    std::string role_str =
                        (u.role == UserRole::ADMIN) ? "ADMIN" :
                        (u.role == UserRole::USER) ? "USER" : "READONLY";
                    out << "  " << u.username << " | " << role_str << "\n";
                }
                return out.str();
            }
            
            // SHOW DATABASES
            if (stmt->GetType() == StatementType::SHOW_DATABASES) {
                std::ostringstream out;
                out << "Databases:\n";
                out << "  default\n";
                // List databases from data/ directory
                if (std::filesystem::exists("data")) {
                    for (const auto& entry : std::filesystem::directory_iterator("data")) {
                        if (entry.is_regular_file() && entry.path().extension() == ".francodb") {
                            std::string db_name = entry.path().stem().string();
                            out << "  " << db_name << "\n";
                        }
                    }
                }
                return out.str();
            }

            // Handle CREATE DATABASE using separate file per DB (Option B)
            if (stmt->GetType() == StatementType::CREATE_DB) {
                // Check RBAC permission
                if (!auth_manager_->HasPermission(session_->role, StatementType::CREATE_DB)) {
                    return "ERROR: Permission denied. CREATE DATABASE requires ADMIN role.\n";
                }

                auto *create_db = dynamic_cast<CreateDatabaseStatement *>(stmt.get());
                if (!create_db) return "ERROR: Invalid CREATE DATABASE\n";

                try {
                    std::filesystem::create_directories("data");
                    // Each database gets its own .francodb file under data/
                    DiskManager new_db("data/" + create_db->db_name_);
                    // DiskManager ctor ensures magic header and free page map
                    return "CREATE DATABASE " + create_db->db_name_ + " OK\n";
                } catch (const std::exception &e) {
                    return std::string("ERROR: Failed to create database: ") + e.what() + "\n";
                }
            }

            // RBAC Permission Check before executing statement
            if (!auth_manager_->HasPermission(session_->role, stmt->GetType())) {
                return "ERROR: Permission denied. Your role (" + 
                       std::string(session_->role == UserRole::ADMIN ? "ADMIN" : 
                                  session_->role == UserRole::USER ? "USER" : "READONLY") + 
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
                        std::string(role == UserRole::ADMIN ? "ADMIN" : 
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
                session_->current_db = use_db->db_name_;
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
                        std::string(role == UserRole::ADMIN ? "ADMIN" : 
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
                session_->current_db = use_db->db_name_;
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
