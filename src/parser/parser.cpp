#include "parser/parser.h"
#include "common/exception.h"

namespace francodb {

    std::unique_ptr<Statement> Parser::ParseQuery() {
        // Dispatch based on the first token (The Command Word)
        if (current_token_.type == TokenType::CREATE) {
            return ParseCreate();
        } else if (current_token_.type == TokenType::INSERT) {
            return ParseInsert();
        } else if (current_token_.type == TokenType::SELECT) {
            return ParseSelect();
        } else if (current_token_.type == TokenType::UPDATE) {
            return ParseUpdate();
        } else if (current_token_.type == TokenType::DELETE) {
            return ParseDelete();
        }

        throw Exception(ExceptionType::PARSER, "Unknown command start: " + current_token_.text);
    }

    // 2E3MEL GADWAL users (id RAKAM, is_active BOOL, price KASR);
    std::unique_ptr<CreateStatement> Parser::ParseCreate() {
        auto stmt = std::make_unique<CreateStatement>();
        Advance(); // Eat 2E3MEL

        if (!Match(TokenType::TABLE)) throw Exception(ExceptionType::PARSER, "Expected GADWAL");

        if (current_token_.type != TokenType::IDENTIFIER) throw Exception(ExceptionType::PARSER, "Expected table name");
        stmt->table_name_ = current_token_.text;
        Advance();

        if (!Match(TokenType::L_PAREN)) throw Exception(ExceptionType::PARSER, "Expected (");

        while (current_token_.type != TokenType::R_PAREN) {
            if (current_token_.type != TokenType::IDENTIFIER)
                throw Exception(ExceptionType::PARSER, "Expected column name");
            std::string col_name = current_token_.text;
            Advance();

            TypeId type;
            if (Match(TokenType::INT_TYPE)) type = TypeId::INTEGER;
            else if (Match(TokenType::STRING_TYPE)) type = TypeId::VARCHAR;
            else if (Match(TokenType::BOOL_TYPE)) type = TypeId::BOOLEAN;
            else if (Match(TokenType::DATE_TYPE)) type = TypeId::TIMESTAMP;
            else if (Match(TokenType::DECIMAL_TYPE)) type = TypeId::DECIMAL;
            else throw Exception(ExceptionType::PARSER, "Unknown type for column " + col_name);

            stmt->columns_.emplace_back(col_name, type);

            if (current_token_.type == TokenType::COMMA) Advance();
            else if (current_token_.type != TokenType::R_PAREN)
                throw Exception(ExceptionType::PARSER, "Expected , or )");
        }
        Advance(); // Eat )
        
        // STRICT CHECK: Must end with ;
        if (!Match(TokenType::SEMICOLON)) 
            throw Exception(ExceptionType::PARSER, "Expected ; at end of command, found: " + current_token_.text);
            
        return stmt;
    }

    // EMLA GOWA users ELKEYAM (1, 'Ahmed', AH, 10.5);
    std::unique_ptr<InsertStatement> Parser::ParseInsert() {
        auto stmt = std::make_unique<InsertStatement>();
        Advance(); // EMLA

        if (!Match(TokenType::INTO)) throw Exception(ExceptionType::PARSER, "Expected GOWA");

        stmt->table_name_ = current_token_.text;
        Advance();

        if (!Match(TokenType::VALUES)) throw Exception(ExceptionType::PARSER, "Expected ELKEYAM");
        if (!Match(TokenType::L_PAREN)) throw Exception(ExceptionType::PARSER, "Expected (");

        while (current_token_.type != TokenType::R_PAREN) {
            stmt->values_.push_back(ParseValue()); // Use helper

            if (current_token_.type == TokenType::COMMA) Advance();
        }
        Advance(); // )
        
        // STRICT CHECK
        if (!Match(TokenType::SEMICOLON)) 
            throw Exception(ExceptionType::PARSER, "Expected ; at end of command, found: " + current_token_.text);

        return stmt;
    }

    // 2E5TAR * MEN users LAMA ... ;
    std::unique_ptr<SelectStatement> Parser::ParseSelect() {
        auto stmt = std::make_unique<SelectStatement>();
        Advance(); // 2E5TAR

        if (Match(TokenType::STAR)) {
            stmt->select_all_ = true;
        } else {
            while (current_token_.type == TokenType::IDENTIFIER) {
                stmt->columns_.push_back(current_token_.text);
                Advance();
                if (current_token_.type == TokenType::COMMA) Advance();
                else break;
            }
        }

        if (!Match(TokenType::FROM)) throw Exception(ExceptionType::PARSER, "Expected MEN");
        stmt->table_name_ = current_token_.text;
        Advance();

        stmt->where_clause_ = ParseWhereClause();
        
        // STRICT CHECK (This fixes the 'Invalid Token' test failure)
        if (!Match(TokenType::SEMICOLON)) 
            throw Exception(ExceptionType::PARSER, "Expected ; at end of command, found: " + current_token_.text);

        return stmt;
    }

    // 2EMSA7 MEN users LAMA ... ;
    std::unique_ptr<Statement> Parser::ParseDelete() {
        Advance(); // Eat 2EMSA7

        if (Match(TokenType::TABLE)) {
            // DROP TABLE
            auto stmt = std::make_unique<DropStatement>();
            stmt->table_name_ = current_token_.text;
            Advance();
            
            if (!Match(TokenType::SEMICOLON)) 
                throw Exception(ExceptionType::PARSER, "Expected ; after Drop Table");
            return stmt;
            
        } else if (Match(TokenType::FROM)) {
            // DELETE FROM
            auto stmt = std::make_unique<DeleteStatement>();
            stmt->table_name_ = current_token_.text;
            Advance();

            stmt->where_clause_ = ParseWhereClause();

            if (!Match(TokenType::SEMICOLON)) 
                throw Exception(ExceptionType::PARSER, "Expected ; at end of command");
            return stmt;
        }
        throw Exception(ExceptionType::PARSER, "Syntax error after 2EMSA7: Expected GADWAL or MEN");
    }

    // 3ADEL GOWA users 5ALY ... ;
    std::unique_ptr<UpdateStatement> Parser::ParseUpdate() {
        auto stmt = std::make_unique<UpdateStatement>();
        Advance(); // Eat 3ADEL
        if (!Match(TokenType::INTO)) throw Exception(ExceptionType::PARSER, "Expected GOWA after Update");

        stmt->table_name_ = current_token_.text;
        Advance();

        if (!Match(TokenType::UPDATE_SET)) throw Exception(ExceptionType::PARSER, "Expected 5ALY (Set)");

        stmt->target_column_ = current_token_.text;
        Advance();

        if (!Match(TokenType::EQUALS)) throw Exception(ExceptionType::PARSER, "Expected =");
        stmt->new_value_ = ParseValue();

        stmt->where_clause_ = ParseWhereClause();

        if (!Match(TokenType::SEMICOLON)) 
            throw Exception(ExceptionType::PARSER, "Expected ; at end of command");

        return stmt;
    }

    // --- HELPER FUNCTIONS ---

    Value Parser::ParseValue() {
        // Handle Negative Numbers (The Lexer produces NUMBER for -50, so this logic is simple)
        if (current_token_.type == TokenType::NUMBER) {
            Value v(TypeId::INTEGER, std::stoi(current_token_.text));
            Advance();
            return v;
        } else if (current_token_.type == TokenType::STRING_LIT) {
            Value v(TypeId::VARCHAR, current_token_.text);
            Advance();
            return v;
        } else if (current_token_.type == TokenType::TRUE_LIT) {
            Value v(TypeId::BOOLEAN, 1);
            Advance();
            return v;
        } else if (current_token_.type == TokenType::FALSE_LIT) {
            Value v(TypeId::BOOLEAN, 0);
            Advance();
            return v;
        } else if (current_token_.type == TokenType::DECIMAL_LITERAL) {
            Value v(TypeId::DECIMAL, std::stod(current_token_.text));
            Advance();
            return v;
        }
        throw Exception(ExceptionType::PARSER, "Expected a valid value at: " + current_token_.text);
    }

    std::vector<WhereCondition> Parser::ParseWhereClause() {
        std::vector<WhereCondition> conditions;

        if (!Match(TokenType::WHERE)) {
            return conditions;
        }

        while (true) {
            WhereCondition cond;

            if (current_token_.type != TokenType::IDENTIFIER) 
                throw Exception(ExceptionType::PARSER, "Expected column name in LAMA clause");
            cond.column = current_token_.text;
            Advance();

            if (!Match(TokenType::EQUALS)) 
                throw Exception(ExceptionType::PARSER, "Expected =");
            cond.op = "=";

            cond.value = ParseValue();

            if (Match(TokenType::AND)) {
                cond.next_logic = LogicType::AND;
                conditions.push_back(cond);
            } else if (Match(TokenType::OR)) {
                cond.next_logic = LogicType::OR;
                conditions.push_back(cond);
            } else {
                cond.next_logic = LogicType::NONE;
                conditions.push_back(cond);
                break; 
            }
        }
        return conditions;
    }

    bool Parser::Match(TokenType type) {
        if (current_token_.type == type) {
            Advance();
            return true;
        }
        return false;
    }
} // namespace francodb