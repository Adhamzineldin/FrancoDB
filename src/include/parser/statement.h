#pragma once

#include <string>
#include <vector>
#include <memory>
#include "storage/table/schema.h"
#include "common/value.h"

namespace francodb {
    enum class StatementType { CREATE, INSERT, SELECT, DELETE_CMD, UPDATE_CMD, DROP, CREATE_INDEX, BEGIN, ROLLBACK, COMMIT, CREATE_DB, USE_DB, LOGIN, CREATE_USER, ALTER_USER_ROLE, DELETE_USER, SHOW_USERS, SHOW_DATABASES, SHOW_TABLES, SHOW_STATUS, WHOAMI };

    enum class LogicType { NONE, AND, OR };

    class Statement {
    public:
        virtual ~Statement() = default;

        virtual StatementType GetType() const = 0;
    };


    struct WhereCondition {
        std::string column;
        std::string op; // "=" or "IN"
        Value value;  // For "=" operator
        std::vector<Value> in_values; // For "IN" operator
        LogicType next_logic; // Does "WE" or "AW" come after this?
    };

    // --- TABLE LEVEL OPS ---

    /** 2E3MEL GADWAL <name> (...) */
    class CreateStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::CREATE; }
        std::string table_name_;
        std::vector<Column> columns_;
    };

    /** EREMY GADWAL <name> (DROP TABLE) */
    class DropStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::DROP; }
        std::string table_name_;
    };

    // --- ROW LEVEL OPS ---

    /** EMLA GOWA <name> ELKEYAM (...) */
    class InsertStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::INSERT; }
        std::string table_name_;
        std::vector<Value> values_;
    };

    class SelectStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::SELECT; }

        bool select_all_ = false;
        std::vector<std::string> columns_;
        std::string table_name_;

        // THE UPGRADE: A list of conditions instead of single variables
        std::vector<WhereCondition> where_clause_;
    };

    class UpdateStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::UPDATE_CMD; }
        std::string table_name_;
        std::string target_column_;
        Value new_value_;

        std::vector<WhereCondition> where_clause_; // Upgrade
    };

    class DeleteStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::DELETE_CMD; }
        std::string table_name_;

        std::vector<WhereCondition> where_clause_; // Upgrade
    };
    
    struct CreateIndexStatement : public Statement {
        StatementType GetType() const override { return StatementType::CREATE_INDEX; }
    
        std::string index_name_;
        std::string table_name_;
        std::string column_name_; // Single column index for simplicity
    };
    
    /** BED2 (BEGIN TRANSACTION) */
    class BeginStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::BEGIN; }
    };
    
    /** ERGA3 (ROLLBACK TRANSACTION) */
    class RollbackStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::ROLLBACK; }
    };
    
    /** KAMEL (COMMIT TRANSACTION) */
    class CommitStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::COMMIT; }
    };

    // --- DATABASE & AUTH ---

    class CreateDatabaseStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::CREATE_DB; }
        std::string db_name_;
    };

    class UseDatabaseStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::USE_DB; }
        std::string db_name_;
    };

    class LoginStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::LOGIN; }
        std::string username_;
        std::string password_;
    };

    class CreateUserStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::CREATE_USER; }
        std::string username_;
        std::string password_;
        std::string role_; // ADMIN/USER/READONLY
    };

    class AlterUserRoleStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::ALTER_USER_ROLE; }
        std::string username_;
        std::string role_;
        std::string db_name_; // Added for ALTER USER ... ROLE ... IN ...
    };

    class DeleteUserStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::DELETE_USER; }
        std::string username_;
    };

    class ShowUsersStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::SHOW_USERS; }
    };

    class ShowDatabasesStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::SHOW_DATABASES; }
    };

    class ShowTablesStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::SHOW_TABLES; }
    };

    class WhoAmIStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::WHOAMI; }
    };

    class ShowStatusStatement : public Statement {
    public:
        StatementType GetType() const override { return StatementType::SHOW_STATUS; }
    };
} // namespace francodb
