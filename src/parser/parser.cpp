    #include "parser/parser.h"
    #include "parser/lexer.h"
    #include "parser/token.h"
    #include "common/exception.h"

    namespace francodb {
        Parser::Parser(Lexer lexer) : lexer_(std::move(lexer)) {
            // Prepare the first token
            current_token_ = lexer_.NextToken();
        }

        void Parser::Advance() {
            current_token_ = lexer_.NextToken();
        }

        std::unique_ptr<Statement> Parser::ParseQuery() {
            // --- DISPATCHER ---

            // 1. CREATE ...
            if (current_token_.type == TokenType::CREATE) {
                Advance(); // Eat '2E3MEL'

                if (current_token_.type == TokenType::TABLE) {
                    Advance(); // Eat 'GADWAL'
                    return ParseCreateTable();
                } else if (current_token_.type == TokenType::INDEX) {
                    Advance(); // Eat 'FEHRIS'
                    return ParseCreateIndex();
                } else if (current_token_.type == TokenType::DATABASE) {
                    Advance(); // Eat 'DATABASE'
                    return ParseCreateDatabase();
                } else if (current_token_.type == TokenType::USER) {
                    Advance(); // Eat 'USER'
                    return ParseCreateUser();
                }
                throw Exception(ExceptionType::PARSER, "Expected GADWAL, FEHRIS, DATABASE, or USER after 2E3MEL");
            }

            // 2. INSERT
            else if (current_token_.type == TokenType::INSERT) {
                return ParseInsert();
            }
            // 3. SELECT
            else if (current_token_.type == TokenType::SELECT) {
                return ParseSelect();
            }
            // 4. UPDATE / ALTER USER
            else if (current_token_.type == TokenType::UPDATE_CMD) {
                Advance(); // Eat '3ADEL'
                // Check if it's ALTER USER ROLE
                if (current_token_.type == TokenType::USER) {
                    Advance(); // Eat USER
                    return ParseAlterUserRole();
                }
                // Otherwise it's UPDATE table.
                // NOTE: We already ate '3ADEL', so ParseUpdate should start at TableName
                return ParseUpdate();
            }
            // 5. DELETE
            else if (current_token_.type == TokenType::DELETE_CMD) {
                Advance(); // Eat '2EMSA7'

                // Check if it's DELETE USER
                if (current_token_.type == TokenType::USER) {
                    Advance(); // Eat USER
                    return ParseDeleteUser();
                }

                // Check if it's DROP DATABASE
                if (current_token_.type == TokenType::DATABASE) {
                    Advance(); // Eat DATABASE
                    return ParseDropDatabase();
                }

                // Otherwise it's DELETE FROM table or DROP TABLE
                return ParseDelete();
            }
            // 6. BEGIN TRANSACTION
            else if (current_token_.type == TokenType::BEGIN_TXN) {
                Advance(); // Eat '2EBDA2'
                if (!Match(TokenType::SEMICOLON))
                    throw Exception(ExceptionType::PARSER, "Expected ; after 2EBDA2");
                return std::make_unique<BeginStatement>();
            }
            // 7. ROLLBACK TRANSACTION
            else if (current_token_.type == TokenType::ROLLBACK) {
                Advance(); // Eat '2ERGA3'
                if (!Match(TokenType::SEMICOLON))
                    throw Exception(ExceptionType::PARSER, "Expected ; after 2ERGA3");
                return std::make_unique<RollbackStatement>();
            }
            // 8. COMMIT TRANSACTION
            else if (current_token_.type == TokenType::COMMIT) {
                Advance(); // Eat '2AKED'
                if (!Match(TokenType::SEMICOLON))
                    throw Exception(ExceptionType::PARSER, "Expected ; after 2AKED");
                return std::make_unique<CommitStatement>();
            }
            // 9. USE database
            else if (current_token_.type == TokenType::USE) {
                return ParseUseDatabase();
            }
            // 10. LOGIN user pass
            else if (current_token_.type == TokenType::LOGIN) {
                return ParseLogin();
            }
            // 11. WHOAMI
            else if (current_token_.type == TokenType::WHOAMI) {
                return ParseWhoAmI();
            }
            // 12. STATUS
            else if (current_token_.type == TokenType::STATUS) {
                return ParseShowStatus();
            }
            // 13. SHOW USERS / SHOW DATABASES / SHOW CREATE TABLE
            else if (current_token_.type == TokenType::SHOW) {
                Advance(); // Eat SHOW / WARENY
                if (current_token_.type == TokenType::USER) {
                    Advance(); // Eat USER
                    if (!Match(TokenType::SEMICOLON))
                        throw Exception(ExceptionType::PARSER, "Expected ; after SHOW USER");
                    return std::make_unique<ShowUsersStatement>();
                } else if (current_token_.type == TokenType::DATABASES) {
                    Advance(); // Eat DATABASES
                    if (!Match(TokenType::SEMICOLON))
                        throw Exception(ExceptionType::PARSER, "Expected ; after SHOW DATABASES");
                    return std::make_unique<ShowDatabasesStatement>();
                } else if (current_token_.type == TokenType::CREATE) {
                    // SHOW CREATE TABLE
                    Advance(); // Eat CREATE
                    if (current_token_.type != TokenType::TABLE) {
                        throw Exception(ExceptionType::PARSER, "Expected TABLE after SHOW CREATE");
                    }
                    Advance(); // Eat TABLE
                    auto stmt = std::make_unique<ShowCreateTableStatement>();
                    if (current_token_.type != TokenType::IDENTIFIER) {
                        throw Exception(ExceptionType::PARSER, "Expected table name");
                    }
                    stmt->table_name_ = current_token_.text;
                    Advance();
                    if (!Match(TokenType::SEMICOLON))
                        throw Exception(ExceptionType::PARSER, "Expected ; after SHOW CREATE TABLE");
                    return stmt;
                } else if (current_token_.type == TokenType::TABLE) {
                    Advance(); // Eat TABLE / GADWAL
                    if (!Match(TokenType::SEMICOLON))
                        throw Exception(ExceptionType::PARSER, "Expected ; after SHOW TABLES");
                    return std::make_unique<ShowTablesStatement>();
                }
                throw Exception(ExceptionType::PARSER, "Expected USER, DATABASES, CREATE, or TABLES after SHOW");
            }
            // 14. DESCRIBE / DESC
            else if (current_token_.type == TokenType::DESCRIBE) {
                Advance(); // Eat DESCRIBE/WASF
                auto stmt = std::make_unique<DescribeTableStatement>();
                if (current_token_.type != TokenType::IDENTIFIER) {
                    throw Exception(ExceptionType::PARSER, "Expected table name after DESCRIBE");
                }
                stmt->table_name_ = current_token_.text;
                Advance();
                if (!Match(TokenType::SEMICOLON))
                    throw Exception(ExceptionType::PARSER, "Expected ; after DESCRIBE");
                return stmt;
            }

            throw Exception(ExceptionType::PARSER, "Unknown command start: " + current_token_.text);
        }

        // Pre-condition: '2E3MEL' and 'GADWAL' are already consumed.
        // Expects: TableName ( ... ) ; (ENTERPRISE ENHANCED)
        std::unique_ptr<CreateStatement> Parser::ParseCreateTable() {
            auto stmt = std::make_unique<CreateStatement>();

            if (current_token_.type != TokenType::IDENTIFIER)
                throw Exception(ExceptionType::PARSER, "Expected table name");
            stmt->table_name_ = current_token_.text;
            Advance();

            if (!Match(TokenType::L_PAREN)) throw Exception(ExceptionType::PARSER, "Expected (");

            while (current_token_.type != TokenType::R_PAREN) {
                // Check for table-level constraints
                if (Match(TokenType::FOREIGN)) {
                    // Parse FOREIGN KEY constraint
                    if (!Match(TokenType::KEY))
                        throw Exception(ExceptionType::PARSER, "Expected KEY after FOREIGN");

                    if (!Match(TokenType::L_PAREN))
                        throw Exception(ExceptionType::PARSER, "Expected ( after FOREIGN KEY");

                    CreateStatement::ForeignKey fk;

                    // Parse column list
                    do {
                        if (current_token_.type != TokenType::IDENTIFIER)
                            throw Exception(ExceptionType::PARSER, "Expected column name in FOREIGN KEY");
                        fk.columns.push_back(current_token_.text);
                        Advance();
                    } while (Match(TokenType::COMMA));

                    if (!Match(TokenType::R_PAREN))
                        throw Exception(ExceptionType::PARSER, "Expected ) after FK columns");

                    if (!Match(TokenType::REFERENCES))
                        throw Exception(ExceptionType::PARSER, "Expected REFERENCES");

                    // Parse referenced table
                    if (current_token_.type != TokenType::IDENTIFIER)
                        throw Exception(ExceptionType::PARSER, "Expected referenced table name");
                    fk.ref_table = current_token_.text;
                    Advance();

                    if (!Match(TokenType::L_PAREN))
                        throw Exception(ExceptionType::PARSER, "Expected ( after referenced table");

                    // Parse referenced columns
                    do {
                        if (current_token_.type != TokenType::IDENTIFIER)
                            throw Exception(ExceptionType::PARSER, "Expected column name in REFERENCES");
                        fk.ref_columns.push_back(current_token_.text);
                        Advance();
                    } while (Match(TokenType::COMMA));

                    if (!Match(TokenType::R_PAREN))
                        throw Exception(ExceptionType::PARSER, "Expected ) after referenced columns");

                    // Optional: ON DELETE / ON UPDATE
                    while (Match(TokenType::ON)) {
                        if (Match(TokenType::DELETE_CMD)) {
                            fk.on_delete = ParseReferentialAction();
                        } else if (Match(TokenType::UPDATE_CMD)) {
                            fk.on_update = ParseReferentialAction();
                        } else {
                            throw Exception(ExceptionType::PARSER, "Expected DELETE or UPDATE after ON");
                        }
                    }

                    stmt->foreign_keys_.push_back(fk);
                } else if (Match(TokenType::CHECK)) {
                    // Parse CHECK constraint
                    CreateStatement::CheckConstraint check;

                    // Optional constraint name
                    if (current_token_.type == TokenType::IDENTIFIER) {
                        check.name = current_token_.text;
                        Advance();
                    }

                    if (!Match(TokenType::L_PAREN))
                        throw Exception(ExceptionType::PARSER, "Expected ( after CHECK");

                    check.expression = ParseCheckExpression();

                    if (!Match(TokenType::R_PAREN))
                        throw Exception(ExceptionType::PARSER, "Expected ) after CHECK expression");

                    stmt->check_constraints_.push_back(check);
                } else {
                    // Parse column definition
                    if (current_token_.type != TokenType::IDENTIFIER)
                        throw Exception(ExceptionType::PARSER, "Expected column name");
                    std::string col_name = current_token_.text;
                    Advance();

                    // Parse type
                    TypeId type;
                    uint32_t length = 0;
                    if (Match(TokenType::INT_TYPE)) type = TypeId::INTEGER;
                    else if (Match(TokenType::STRING_TYPE)) {
                        type = TypeId::VARCHAR;
                        if (current_token_.type == TokenType::L_PAREN) {
                            Advance();
                            if (current_token_.type == TokenType::NUMBER) {
                                length = std::stoi(current_token_.text);
                                Advance();
                            }
                            if (!Match(TokenType::R_PAREN))
                                throw Exception(ExceptionType::PARSER, "Expected ) after string length");
                        } else {
                            length = 255;
                        }
                    } else if (Match(TokenType::BOOL_TYPE)) type = TypeId::BOOLEAN;
                    else if (Match(TokenType::DATE_TYPE)) type = TypeId::TIMESTAMP;
                    else if (Match(TokenType::DECIMAL_TYPE)) type = TypeId::DECIMAL;
                    else throw Exception(ExceptionType::PARSER, "Unknown type for column " + col_name);

                    // Parse column constraints
                    bool is_primary_key = false;
                    bool is_nullable = true;
                    bool is_unique = false;
                    bool is_auto_increment = false;
                    std::string check_expr;

                    // Parse multiple constraints
                    while (true) {
                        if (Match(TokenType::PRIMARY_KEY)) {
                            is_primary_key = true;
                            is_nullable = false; // PK implies NOT NULL
                        } else if (Match(TokenType::NOT)) {
                            if (!Match(TokenType::NULL_LIT))
                                throw Exception(ExceptionType::PARSER, "Expected NULL after NOT");
                            is_nullable = false;
                        } else if (Match(TokenType::NULL_LIT)) {
                            is_nullable = true;
                        } else if (Match(TokenType::UNIQUE)) {
                            is_unique = true;
                        } else if (Match(TokenType::AUTO_INCREMENT)) {
                            is_auto_increment = true;
                        } else if (Match(TokenType::DEFAULT_KW)) {
                            // Skip default value for now (would need to store it)
                            ParseValue();
                        } else if (Match(TokenType::CHECK)) {
                            if (!Match(TokenType::L_PAREN))
                                throw Exception(ExceptionType::PARSER, "Expected ( after CHECK");
                            check_expr = ParseCheckExpression();
                            if (!Match(TokenType::R_PAREN))
                                throw Exception(ExceptionType::PARSER, "Expected ) after CHECK");
                        } else {
                            break; // No more constraints
                        }
                    }

                    // Create column with constraints
                    Column col(col_name, type, (type == TypeId::VARCHAR ? length : 0),
                               is_primary_key, is_nullable, is_unique);

                    if (is_auto_increment) {
                        col.SetAutoIncrement(true);
                    }

                    if (!check_expr.empty()) {
                        col.SetCheckConstraint(check_expr);
                    }

                    stmt->columns_.push_back(col);
                }

                // Expect comma or closing paren
                if (current_token_.type == TokenType::COMMA) {
                    Advance();
                } else if (current_token_.type != TokenType::R_PAREN) {
                    throw Exception(ExceptionType::PARSER, "Expected , or )");
                }
            }
            Advance(); // Eat )

            if (!Match(TokenType::SEMICOLON))
                throw Exception(ExceptionType::PARSER, "Expected ; at end of command");

            return stmt;
        }

        // Pre-condition: '2E3MEL' and 'FEHRIS' are already consumed.
        // Expects: IndexName 3ALA TableName ( Column ) ;
        std::unique_ptr<CreateIndexStatement> Parser::ParseCreateIndex() {
            auto stmt = std::make_unique<CreateIndexStatement>();

            if (current_token_.type != TokenType::IDENTIFIER) throw Exception(ExceptionType::PARSER, "Expected Index Name");
            stmt->index_name_ = current_token_.text;
            Advance();

            if (!Match(TokenType::ON)) throw Exception(ExceptionType::PARSER, "Expected 3ALA (ON)");

            if (current_token_.type != TokenType::IDENTIFIER) throw Exception(ExceptionType::PARSER, "Expected Table Name");
            stmt->table_name_ = current_token_.text;
            Advance();

            if (!Match(TokenType::L_PAREN)) throw Exception(ExceptionType::PARSER, "Expected (");
            if (current_token_.type != TokenType::IDENTIFIER) throw
                    Exception(ExceptionType::PARSER, "Expected Column Name");
            stmt->column_name_ = current_token_.text;
            Advance();

            if (!Match(TokenType::R_PAREN)) throw Exception(ExceptionType::PARSER, "Expected )");
            if (!Match(TokenType::SEMICOLON)) throw Exception(ExceptionType::PARSER, "Expected ;");

            return stmt;
        }

        std::unique_ptr<CreateDatabaseStatement> Parser::ParseCreateDatabase() {
            auto stmt = std::make_unique<CreateDatabaseStatement>();
            if (current_token_.type != TokenType::IDENTIFIER)
                throw Exception(ExceptionType::PARSER, "Expected database name");
            stmt->db_name_ = current_token_.text;
            Advance();
            if (!Match(TokenType::SEMICOLON))
                throw Exception(ExceptionType::PARSER, "Expected ;");
            return stmt;
        }

        std::unique_ptr<UseDatabaseStatement> Parser::ParseUseDatabase() {
            Advance(); // Eat USE
            auto stmt = std::make_unique<UseDatabaseStatement>();
            if (current_token_.type != TokenType::IDENTIFIER)
                throw Exception(ExceptionType::PARSER, "Expected database name");
            stmt->db_name_ = current_token_.text;
            Advance();
            if (!Match(TokenType::SEMICOLON))
                throw Exception(ExceptionType::PARSER, "Expected ;");
            return stmt;
        }

        std::unique_ptr<LoginStatement> Parser::ParseLogin() {
            Advance(); // Eat LOGIN
            auto stmt = std::make_unique<LoginStatement>();

            if (current_token_.type != TokenType::IDENTIFIER && current_token_.type != TokenType::STRING_LIT)
                throw Exception(ExceptionType::PARSER, "Expected username");
            stmt->username_ = current_token_.text;
            Advance();

            if (current_token_.type != TokenType::IDENTIFIER && current_token_.type != TokenType::STRING_LIT &&
                current_token_.type != TokenType::NUMBER)
                throw Exception(ExceptionType::PARSER, "Expected password");
            stmt->password_ = current_token_.text;
            Advance();

            if (!Match(TokenType::SEMICOLON))
                throw Exception(ExceptionType::PARSER, "Expected ;");
            return stmt;
        }

        std::unique_ptr<WhoAmIStatement> Parser::ParseWhoAmI() {
            Advance();
            if (!Match(TokenType::SEMICOLON)) throw Exception(ExceptionType::PARSER, "Expected ;");
            return std::make_unique<WhoAmIStatement>();
        }

        std::unique_ptr<ShowStatusStatement> Parser::ParseShowStatus() {
            Advance();
            if (!Match(TokenType::SEMICOLON)) throw Exception(ExceptionType::PARSER, "Expected ;");
            return std::make_unique<ShowStatusStatement>();
        }

        std::unique_ptr<CreateUserStatement> Parser::ParseCreateUser() {
            auto stmt = std::make_unique<CreateUserStatement>();

            // Username
            if (current_token_.type != TokenType::IDENTIFIER && current_token_.type != TokenType::STRING_LIT)
                throw Exception(ExceptionType::PARSER, "Expected username after USER");
            stmt->username_ = current_token_.text;
            Advance();

            // PASS keyword
            if (!Match(TokenType::PASS))
                throw Exception(ExceptionType::PARSER, "Expected PASSWORD (PASS) keyword");

            // Password
            if (current_token_.type != TokenType::IDENTIFIER && current_token_.type != TokenType::STRING_LIT &&
                current_token_.type != TokenType::NUMBER)
                throw Exception(ExceptionType::PARSER, "Expected password value");
            stmt->password_ = current_token_.text;
            Advance();

            // ROLE keyword
            if (!Match(TokenType::ROLE))
                throw Exception(ExceptionType::PARSER, "Expected ROLE keyword");

            // [FIX] Handle Specific Role Tokens
            std::string role_str;
            switch (current_token_.type) {
                case TokenType::ROLE_SUPERADMIN: role_str = "SUPERADMIN";
                    break;
                case TokenType::ROLE_ADMIN: role_str = "ADMIN";
                    break;
                case TokenType::ROLE_NORMAL: role_str = "NORMAL";
                    break;
                case TokenType::ROLE_READONLY: role_str = "READONLY";
                    break;
                case TokenType::ROLE_DENIED: role_str = "DENIED";
                    break;
                case TokenType::IDENTIFIER: role_str = current_token_.text;
                    break; // Fallback
                default:
                    throw Exception(ExceptionType::PARSER, "Expected valid Role (SUPERADMIN, ADMIN, NORMAL, READONLY)");
            }
            stmt->role_ = role_str;
            Advance(); // Consume role token

            if (!Match(TokenType::SEMICOLON))
                throw Exception(ExceptionType::PARSER, "Expected ;");
            return stmt;
        }

        std::unique_ptr<AlterUserRoleStatement> Parser::ParseAlterUserRole() {
            auto stmt = std::make_unique<AlterUserRoleStatement>();

            // Username
            if (current_token_.type != TokenType::IDENTIFIER && current_token_.type != TokenType::STRING_LIT)
                throw Exception(ExceptionType::PARSER, "Expected username after USER");
            stmt->username_ = current_token_.text;
            Advance();

            // ROLE keyword
            if (!Match(TokenType::ROLE))
                throw Exception(ExceptionType::PARSER, "Expected ROLE keyword");

            // [FIX] Handle Specific Role Tokens
            std::string role_str;
            switch (current_token_.type) {
                case TokenType::ROLE_SUPERADMIN: role_str = "SUPERADMIN";
                    break;
                case TokenType::ROLE_ADMIN: role_str = "ADMIN";
                    break;
                case TokenType::ROLE_NORMAL: role_str = "NORMAL";
                    break;
                case TokenType::ROLE_READONLY: role_str = "READONLY";
                    break;
                case TokenType::ROLE_DENIED: role_str = "DENIED";
                    break;
                case TokenType::IDENTIFIER: role_str = current_token_.text;
                    break;
                default:
                    throw Exception(ExceptionType::PARSER, "Expected valid Role (SUPERADMIN, ADMIN, NORMAL, READONLY)");
            }
            stmt->role_ = role_str;
            Advance();

            // Optional: IN <db>
            if (Match(TokenType::IN_OP)) {
                // FE
                if (current_token_.type != TokenType::IDENTIFIER)
                    throw Exception(ExceptionType::PARSER, "Expected database name after IN");
                stmt->db_name_ = current_token_.text;
                Advance();
            }

            if (!Match(TokenType::SEMICOLON))
                throw Exception(ExceptionType::PARSER, "Expected ;");
            return stmt;
        }

        std::unique_ptr<DeleteUserStatement> Parser::ParseDeleteUser() {
            auto stmt = std::make_unique<DeleteUserStatement>();
            // Username
            if (current_token_.type != TokenType::IDENTIFIER && current_token_.type != TokenType::STRING_LIT)
                throw Exception(ExceptionType::PARSER, "Expected username");
            stmt->username_ = current_token_.text;
            Advance();
            if (!Match(TokenType::SEMICOLON))
                throw Exception(ExceptionType::PARSER, "Expected ;");
            return stmt;
        }

        std::unique_ptr<ShowDatabasesStatement> Parser::ParseShowDatabases() {
            if (!Match(TokenType::SEMICOLON)) throw Exception(ExceptionType::PARSER, "Expected ;");
            return std::make_unique<ShowDatabasesStatement>();
        }

        std::unique_ptr<ShowTablesStatement> Parser::ParseShowTables() {
            if (!Match(TokenType::SEMICOLON)) throw Exception(ExceptionType::PARSER, "Expected ;");
            return std::make_unique<ShowTablesStatement>();
        }

        // EMLA GOWA users ELKEYAM (1, 'Ahmed', AH, 10.5);
        std::unique_ptr<InsertStatement> Parser::ParseInsert() {
            auto stmt = std::make_unique<InsertStatement>();
            Advance(); // EMLA

            if (!Match(TokenType::INTO)) throw Exception(ExceptionType::PARSER, "Expected GOWA");

            stmt->table_name_ = current_token_.text;
            Advance();
            
            if (current_token_.type == TokenType::L_PAREN) {
                Advance(); // Eat (
                while (current_token_.type != TokenType::R_PAREN) {
                    if (current_token_.type != TokenType::IDENTIFIER)
                        throw Exception(ExceptionType::PARSER, "Expected column name");
                    stmt->column_names_.push_back(current_token_.text);
                    Advance();
                    if (current_token_.type == TokenType::COMMA) Advance();
                }
                Advance(); // Eat )
            }

            if (!Match(TokenType::VALUES)) throw Exception(ExceptionType::PARSER, "Expected ELKEYAM");
            if (!Match(TokenType::L_PAREN)) throw Exception(ExceptionType::PARSER, "Expected (");

            while (current_token_.type != TokenType::R_PAREN) {
                stmt->values_.push_back(ParseValue());
                if (current_token_.type == TokenType::COMMA) Advance();
            }
            Advance(); // )

            if (!Match(TokenType::SEMICOLON)) throw Exception(ExceptionType::PARSER, "Expected ;");
            return stmt;
        }

        // 2E5TAR ... (ENTERPRISE ENHANCED)
        std::unique_ptr<SelectStatement> Parser::ParseSelect() {
            auto stmt = std::make_unique<SelectStatement>();
            Advance(); // Consume SELECT token

            // [NEW] Check for DISTINCT
            if (Match(TokenType::DISTINCT)) {
                stmt->is_distinct_ = true;
            }

            // Parse SELECT columns or aggregates
            if (Match(TokenType::STAR)) {
                stmt->select_all_ = true;
            } else {
                // Parse columns or aggregate functions
                while (true) {
                    // Check if it's an aggregate function
                    if (IsAggregateFunction()) {
                        stmt->aggregates_.push_back(ParseAggregateFunction());
                    } else if (current_token_.type == TokenType::IDENTIFIER) {
                        stmt->columns_.push_back(current_token_.text);
                        Advance();
                    } else {
                        break;
                    }

                    if (current_token_.type == TokenType::COMMA) {
                        Advance();
                    } else {
                        break;
                    }
                }
            }

            // FROM clause
            if (!Match(TokenType::FROM)) throw Exception(ExceptionType::PARSER, "Expected MEN (FROM)");
            stmt->table_name_ = current_token_.text;
            Advance();

            // [NEW] Parse JOIN clauses
            while (IsJoinKeyword()) {
                stmt->joins_.push_back(ParseJoinClause());
            }

            // WHERE clause
            stmt->where_clause_ = ParseWhereClause();

            // [NEW] Parse GROUP BY
            if (Match(TokenType::GROUP)) {
                if (!Match(TokenType::BY)) throw Exception(ExceptionType::PARSER, "Expected B (BY) after GROUP");
                stmt->group_by_columns_ = ParseGroupByColumns();
            }

            // [NEW] Parse HAVING
            if (Match(TokenType::HAVING)) {
                stmt->having_clause_ = ParseWhereClause();
            }

            // [NEW] Parse ORDER BY
            if (Match(TokenType::ORDER)) {
                if (!Match(TokenType::BY)) throw Exception(ExceptionType::PARSER, "Expected B (BY) after ORDER");
                stmt->order_by_ = ParseOrderByClause();
            }

            // [NEW] Parse LIMIT
            if (Match(TokenType::LIMIT)) {
                stmt->limit_ = ParseNumber();

                // [NEW] Parse optional OFFSET
                if (Match(TokenType::OFFSET)) {
                    stmt->offset_ = ParseNumber();
                }
            }

            if (!Match(TokenType::SEMICOLON)) throw Exception(ExceptionType::PARSER, "Expected ;");
            return stmt;
        }

        // 2EMSA7 ... (called after DELETE_CMD token is already consumed)
        std::unique_ptr<Statement> Parser::ParseDelete() {
            if (Match(TokenType::TABLE)) {
                // DROP TABLE (2EMSA7 GADWAL)
                auto stmt = std::make_unique<DropStatement>();
                stmt->table_name_ = current_token_.text;
                Advance();
                if (!Match(TokenType::SEMICOLON)) throw Exception(ExceptionType::PARSER, "Expected ;");
                return stmt;
            } else if (Match(TokenType::FROM)) {
                // DELETE FROM (2EMSA7 MEN)
                auto stmt = std::make_unique<DeleteStatement>();
                stmt->table_name_ = current_token_.text;
                Advance();
                stmt->where_clause_ = ParseWhereClause();
                if (!Match(TokenType::SEMICOLON)) throw Exception(ExceptionType::PARSER, "Expected ;");
                return stmt;
            }
            throw Exception(ExceptionType::PARSER, "Expected GADWAL or MEN after 2EMSA7");
        }

        // 3ADEL ...
        // 3ADEL table 5ALY col = val LAMA ...
        std::unique_ptr<UpdateStatement> Parser::ParseUpdate() {
            auto stmt = std::make_unique<UpdateStatement>();
            // [FIX] Do NOT Advance() here. '3ADEL' was already consumed by ParseQuery dispatch.

            // [PATCH] Tolerate accidental 'GOWA' after UPDATE (e.g., "3ADEL GOWA users ...")
            // Some tests or inputs may include GOWA due to similarity with INSERT syntax.
            if (current_token_.type == TokenType::INTO) {
                // Skip optional INTO
                Advance();
            }

            if (current_token_.type != TokenType::IDENTIFIER)
                throw Exception(ExceptionType::PARSER, "Expected Table Name after 3ADEL");

            stmt->table_name_ = current_token_.text;
            Advance();

            if (!Match(TokenType::UPDATE_SET)) throw Exception(ExceptionType::PARSER, "Expected 5ALY");

            stmt->target_column_ = current_token_.text;
            Advance();

            if (!Match(TokenType::EQUALS)) throw Exception(ExceptionType::PARSER, "Expected =");
            stmt->new_value_ = ParseValue();

            stmt->where_clause_ = ParseWhereClause();

            if (!Match(TokenType::SEMICOLON)) throw Exception(ExceptionType::PARSER, "Expected ;");
            return stmt;
        }

        // --- HELPERS ---

        Value Parser::ParseValue() {
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
            throw Exception(ExceptionType::PARSER, "Expected value, found: " + current_token_.text);
        }

        std::vector<WhereCondition> Parser::ParseWhereClause() {
            std::vector<WhereCondition> conditions;
            if (!Match(TokenType::WHERE)) return conditions;

            while (true) {
                WhereCondition cond;
                if (current_token_.type != TokenType::IDENTIFIER) throw Exception(ExceptionType::PARSER, "Expected column");
                cond.column = current_token_.text;
                Advance();

                // Check for IN (FE) operator
                if (Match(TokenType::IN_OP)) {
                    cond.op = "IN";
                    if (!Match(TokenType::L_PAREN)) throw Exception(ExceptionType::PARSER, "Expected ( after FE");
                    while (current_token_.type != TokenType::R_PAREN) {
                        cond.in_values.push_back(ParseValue());
                        if (current_token_.type == TokenType::COMMA) Advance();
                        else if (current_token_.type != TokenType::R_PAREN)
                            throw Exception(ExceptionType::PARSER, "Expected , or ) in IN clause");
                    }
                    Advance();
                } else if (Match(TokenType::EQUALS)) {
                    cond.value = ParseValue();
                    cond.op = "=";
                } else if (current_token_.text == ">=" || current_token_.text == "<=" || current_token_.text == ">" ||
                           current_token_.text == "<") {
                    cond.op = current_token_.text;
                    Advance();
                    cond.value = ParseValue();
                } else {
                    throw Exception(ExceptionType::PARSER, "Expected =, >, <, >=, <= or FE (IN)");
                }

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

        // ============ PHASE 2B: ENTERPRISE FEATURE HELPERS ============

        // Check if current token is an aggregate function
        bool Parser::IsAggregateFunction() {
            TokenType type = current_token_.type;
            return type == TokenType::COUNT || type == TokenType::SUM ||
                   type == TokenType::AVG || type == TokenType::MIN_AGG ||
                   type == TokenType::MAX_AGG;
        }

        // Parse aggregate function: COUNT(*) or SUM(salary)
        std::pair<std::string, std::string> Parser::ParseAggregateFunction() {
            std::string func_name;

            // Identify function
            if (Match(TokenType::COUNT)) func_name = "COUNT";
            else if (Match(TokenType::SUM)) func_name = "SUM";
            else if (Match(TokenType::AVG)) func_name = "AVG";
            else if (Match(TokenType::MIN_AGG)) func_name = "MIN";
            else if (Match(TokenType::MAX_AGG)) func_name = "MAX";
            else throw Exception(ExceptionType::PARSER, "Expected aggregate function");

            if (!Match(TokenType::L_PAREN))
                throw Exception(ExceptionType::PARSER, "Expected ( after aggregate function");

            std::string column;
            if (Match(TokenType::STAR)) {
                column = "*";
            } else if (current_token_.type == TokenType::IDENTIFIER) {
                column = current_token_.text;
                Advance();
            } else {
                throw Exception(ExceptionType::PARSER, "Expected column name or * in aggregate");
            }

            if (!Match(TokenType::R_PAREN))
                throw Exception(ExceptionType::PARSER, "Expected ) after aggregate column");

            return {func_name, column};
        }

        // Check if current token starts a JOIN
        bool Parser::IsJoinKeyword() {
            TokenType type = current_token_.type;
            return type == TokenType::JOIN || type == TokenType::INNER ||
                   type == TokenType::LEFT || type == TokenType::RIGHT ||
                   type == TokenType::CROSS;
        }

        // Parse JOIN clause
        SelectStatement::JoinClause Parser::ParseJoinClause() {
            SelectStatement::JoinClause join;

            // Parse join type
            if (Match(TokenType::INNER)) {
                join.join_type = "INNER";
                if (!Match(TokenType::JOIN))
                    throw Exception(ExceptionType::PARSER, "Expected JOIN after INNER");
            } else if (Match(TokenType::LEFT)) {
                join.join_type = "LEFT";
                if (!Match(TokenType::JOIN))
                    throw Exception(ExceptionType::PARSER, "Expected JOIN after LEFT");
            } else if (Match(TokenType::RIGHT)) {
                join.join_type = "RIGHT";
                if (!Match(TokenType::JOIN))
                    throw Exception(ExceptionType::PARSER, "Expected JOIN after RIGHT");
            } else if (Match(TokenType::CROSS)) {
                join.join_type = "CROSS";
                if (!Match(TokenType::JOIN))
                    throw Exception(ExceptionType::PARSER, "Expected JOIN after CROSS");
            } else if (Match(TokenType::JOIN)) {
                // Just "JOIN" defaults to INNER
                join.join_type = "INNER";
            } else {
                throw Exception(ExceptionType::PARSER, "Expected JOIN keyword");
            }

            // Parse table name
            if (current_token_.type != TokenType::IDENTIFIER)
                throw Exception(ExceptionType::PARSER, "Expected table name after JOIN");
            join.table_name = current_token_.text;
            Advance();

            // Parse ON condition (except for CROSS JOIN)
            if (join.join_type != "CROSS") {
                if (!Match(TokenType::ON))
                    throw Exception(ExceptionType::PARSER, "Expected ON after JOIN table");
                join.condition = ParseJoinCondition();
            }

            return join;
        }

        // Parse JOIN condition (simplified: col1 = col2)
        std::string Parser::ParseJoinCondition() {
            std::string condition;

            // Left side (table.column or just column)
            if (current_token_.type != TokenType::IDENTIFIER)
                throw Exception(ExceptionType::PARSER, "Expected column in JOIN condition");
            condition += current_token_.text;
            Advance();

            // Check for dot notation (table.column)
            if (current_token_.text == ".") {
                condition += ".";
                Advance();
                if (current_token_.type != TokenType::IDENTIFIER)
                    throw Exception(ExceptionType::PARSER, "Expected column name after '.'");
                condition += current_token_.text;
                Advance();
            }

            // Operator (=, <, >, etc.)
            if (current_token_.type == TokenType::EQUALS) {
                condition += " = ";
                Advance();
            } else if (current_token_.text == ">" || current_token_.text == "<" ||
                       current_token_.text == ">=" || current_token_.text == "<=" ||
                       current_token_.text == "!=" || current_token_.text == "<>") {
                condition += " " + current_token_.text + " ";
                Advance();
            } else {
                throw Exception(ExceptionType::PARSER, "Expected comparison operator in JOIN");
            }

            // Right side (table.column or just column)
            if (current_token_.type != TokenType::IDENTIFIER)
                throw Exception(ExceptionType::PARSER, "Expected column in JOIN condition");
            condition += current_token_.text;
            Advance();

            // Check for dot notation (table.column)
            if (current_token_.text == ".") {
                condition += ".";
                Advance();
                if (current_token_.type != TokenType::IDENTIFIER)
                    throw Exception(ExceptionType::PARSER, "Expected column name after '.'");
                condition += current_token_.text;
                Advance();
            }

            return condition;
        }

        // Parse GROUP BY columns
        std::vector<std::string> Parser::ParseGroupByColumns() {
            std::vector<std::string> columns;

            do {
                if (current_token_.type != TokenType::IDENTIFIER)
                    throw Exception(ExceptionType::PARSER, "Expected column name in GROUP BY");
                columns.push_back(current_token_.text);
                Advance();
            } while (Match(TokenType::COMMA));

            return columns;
        }

        // Parse ORDER BY clause
        std::vector<SelectStatement::OrderByClause> Parser::ParseOrderByClause() {
            std::vector<SelectStatement::OrderByClause> order_by;

            do {
                SelectStatement::OrderByClause item;

                if (current_token_.type != TokenType::IDENTIFIER)
                    throw Exception(ExceptionType::PARSER, "Expected column name in ORDER BY");
                item.column = current_token_.text;
                Advance();

                // Check for ASC/DESC
                if (Match(TokenType::ASC)) {
                    item.direction = "ASC";
                } else if (Match(TokenType::DESC)) {
                    item.direction = "DESC";
                } else {
                    item.direction = "ASC"; // Default to ascending
                }

                order_by.push_back(item);
            } while (Match(TokenType::COMMA));

            return order_by;
        }

        // Parse integer literal (for LIMIT/OFFSET)
        int Parser::ParseNumber() {
            if (current_token_.type != TokenType::NUMBER) {
                throw Exception(ExceptionType::PARSER, "Expected number");
            }
            int value = std::stoi(current_token_.text);
            Advance();
            return value;
        }

        // Parse referential action (CASCADE, RESTRICT, etc.)
        std::string Parser::ParseReferentialAction() {
            if (Match(TokenType::CASCADE)) {
                return "CASCADE";
            } else if (Match(TokenType::RESTRICT)) {
                return "RESTRICT";
            } else if (Match(TokenType::SET)) {
                if (!Match(TokenType::NULL_LIT))
                    throw Exception(ExceptionType::PARSER, "Expected NULL after SET");
                return "SET_NULL";
            } else if (Match(TokenType::NO)) {
                if (!Match(TokenType::ACTION))
                    throw Exception(ExceptionType::PARSER, "Expected ACTION after NO");
                return "NO_ACTION";
            }
            throw Exception(ExceptionType::PARSER, "Expected referential action (CASCADE, RESTRICT, SET NULL, NO ACTION)");
        }

        // Parse CHECK constraint expression (simplified - just store as string)
        std::string Parser::ParseCheckExpression() {
            std::string expr;
            int paren_depth = 1; // We already consumed opening paren

            while (paren_depth > 0 && current_token_.type != TokenType::EOF_TOKEN) {
                if (current_token_.type == TokenType::L_PAREN) {
                    paren_depth++;
                } else if (current_token_.type == TokenType::R_PAREN) {
                    paren_depth--;
                    if (paren_depth == 0) break; // Don't include final closing paren
                }

                expr += current_token_.text + " ";
                Advance();
            }

            return expr;
        }
        
        
        
        std::unique_ptr<DropDatabaseStatement> Parser::ParseDropDatabase() {
            auto stmt = std::make_unique<DropDatabaseStatement>();
        
            if (current_token_.type != TokenType::IDENTIFIER) {
                throw Exception(ExceptionType::PARSER, "Expected database name after DROP DATABASE");
            }
        
            stmt->db_name_ = current_token_.text;
            Advance();
        
            if (!Match(TokenType::SEMICOLON)) {
                throw Exception(ExceptionType::PARSER, "Expected ; after DROP DATABASE");
            }
        
            return stmt;
        }
    } // namespace francodb
