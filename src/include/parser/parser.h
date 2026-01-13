#pragma once

#include <memory>
#include "parser/lexer.h"
#include "parser/statement.h"

namespace francodb {

    class Parser {
    public:
        explicit Parser(Lexer lexer) : lexer_(std::move(lexer)) {
            // Prepare the first token
            current_token_ = lexer_.NextToken();
        }

        // The main entry point
        std::unique_ptr<Statement> ParseQuery();

    private:
        // --- Basic Navigation ---
        void Advance() { current_token_ = lexer_.NextToken(); }
    
        // Returns true and advances if current_token matches type, else returns false
        bool Match(TokenType type); 
    
        // Helper to turn a NUMBER or STRING_LIT token into a Value object
        Value ParseValue();

        // --- Specific Command Parsers ---
    
        std::unique_ptr<CreateStatement> ParseCreate();
        std::unique_ptr<InsertStatement> ParseInsert();
        std::unique_ptr<SelectStatement> ParseSelect();
        std::unique_ptr<UpdateStatement> ParseUpdate();
        std::unique_ptr<Statement> ParseDelete(); // Handles DROP and DELETE
        std::vector<WhereCondition> ParseWhereClause();

        Lexer lexer_;
        Token current_token_;
    };

} // namespace francodb