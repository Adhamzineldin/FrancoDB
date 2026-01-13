#pragma once

#include <string>
#include <vector>
#include <memory>
#include "storage/table/schema.h"
#include "common/value.h"

namespace francodb {
    enum class StatementType { CREATE, INSERT, SELECT, DELETE_CMD, UPDATE_CMD, DROP };

    enum class LogicType { NONE, AND, OR };

    class Statement {
    public:
        virtual ~Statement() = default;

        virtual StatementType GetType() const = 0;
    };


    struct WhereCondition {
        std::string column;
        std::string op; // "="
        Value value;
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
} // namespace francodb
