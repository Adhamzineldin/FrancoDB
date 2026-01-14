#pragma once

#include <memory>
#include "parser/lexer.h"
#include "parser/statement.h"

namespace francodb {

    class Parser {
    public:
        explicit Parser(Lexer lexer);
        std::unique_ptr<Statement> ParseQuery();

    private:
        void Advance();
    
        // Returns true and advances if current_token matches type, else returns false
        bool Match(TokenType type); 
    
        // Helper to turn a NUMBER or STRING_LIT token into a Value object
        Value ParseValue();

        // --- Specific Command Parsers ---
    
        std::unique_ptr<CreateStatement> ParseCreateTable();
        std::unique_ptr<InsertStatement> ParseInsert();
        std::unique_ptr<SelectStatement> ParseSelect();
        std::unique_ptr<UpdateStatement> ParseUpdate();
        std::unique_ptr<Statement> ParseDelete(); // Handles DROP and DELETE
        std::vector<WhereCondition> ParseWhereClause();

        std::unique_ptr<CreateIndexStatement> ParseCreateIndex();
        std::unique_ptr<CreateDatabaseStatement> ParseCreateDatabase();
        std::unique_ptr<UseDatabaseStatement> ParseUseDatabase();
        std::unique_ptr<LoginStatement> ParseLogin();
        std::unique_ptr<CreateUserStatement> ParseCreateUser();
        std::unique_ptr<AlterUserRoleStatement> ParseAlterUserRole();
        std::unique_ptr<ShowUsersStatement> ParseShowUsers();
        std::unique_ptr<ShowDatabasesStatement> ParseShowDatabases();
        std::unique_ptr<WhoAmIStatement> ParseWhoAmI();
        std::unique_ptr<ShowStatusStatement> ParseShowStatus();

        Lexer lexer_;
        Token current_token_;
    };

} // namespace francodb