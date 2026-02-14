#include "parser/lexer.h"
#include <map>
#include <cctype>
#include <algorithm>

namespace chronosdb {
    // Keywords map - accessible via GetKeywords() static method
    // BOTH Franco (Arabic transliteration) AND English keywords are supported
    static const std::map<std::string, TokenType> kKeywords = {
        // =====================================================================
        // COMMANDS - DML (Data Manipulation Language)
        // =====================================================================
        // SELECT
        {"SELECT",  TokenType::SELECT},     // English
        {"2E5TAR",  TokenType::SELECT},     // Franco: "e5tar" = choose
        
        // FROM
        {"FROM",    TokenType::FROM},       // English
        {"MEN",     TokenType::FROM},       // Franco: "men" = from
        
        // WHERE
        {"WHERE",   TokenType::WHERE},      // English
        {"LAMA",    TokenType::WHERE},      // Franco: "lama" = when/where
        
        // INSERT
        {"INSERT",  TokenType::INSERT},     // English
        {"EMLA",    TokenType::INSERT},     // Franco: "emla" = fill
        
        // INTO
        {"INTO",    TokenType::INTO},       // English
        {"GOWA",    TokenType::INTO},       // Franco: "gowa" = inside
        
        // VALUES
        {"VALUES",  TokenType::VALUES},     // English
        {"ELKEYAM", TokenType::VALUES},     // Franco: "elkeyam" = the values
        
        // UPDATE
        {"UPDATE",  TokenType::UPDATE_CMD}, // English
        {"3ADEL",   TokenType::UPDATE_CMD}, // Franco: "3adel" = modify
        
        // SET (for UPDATE ... SET)
        {"SET",     TokenType::UPDATE_SET}, // English
        {"5ALY",    TokenType::UPDATE_SET}, // Franco: "5aly" = make it
        
        // DELETE
        {"DELETE",  TokenType::DELETE_CMD}, // English
        {"2EMSA7",  TokenType::DELETE_CMD}, // Franco: "emsa7" = erase
        
        // =====================================================================
        // COMMANDS - DDL (Data Definition Language)
        // =====================================================================
        // CREATE
        {"CREATE",  TokenType::CREATE},     // English
        {"2E3MEL",  TokenType::CREATE},     // Franco: "e3mel" = make
        
        // DROP
        {"DROP",    TokenType::DROP},       // English (also used for DELETE in context)
        
        // ALTER
        {"ALTER",   TokenType::ALTER},      // English
        
        // TABLE
        {"TABLE",   TokenType::TABLE},      // English
        {"GADWAL",  TokenType::TABLE},      // Franco: "gadwal" = table
        
        // DATABASE
        {"DATABASE",  TokenType::DATABASE},
        {"DATABASES", TokenType::DATABASES},
        
        // INDEX
        {"INDEX",   TokenType::INDEX},      // English
        {"FEHRIS",  TokenType::INDEX},      // Franco: "fehris" = index
        
        // =====================================================================
        // DATABASE MANAGEMENT
        // =====================================================================
        {"USE",       TokenType::USE},
        {"2ESTA5DEM", TokenType::USE},      // Franco: "esta5dem" = use
        
        {"LOGIN",     TokenType::LOGIN},
        
        // =====================================================================
        // USER MANAGEMENT
        // =====================================================================
        {"USER",      TokenType::USER},
        {"MOSTA5DEM", TokenType::USER},     // Franco: "mosta5dem" = user
        {"3ABD",      TokenType::USER},     // Franco: slang for user
        
        {"ROLE",      TokenType::ROLE},
        {"WAZEFA",    TokenType::ROLE},     // Franco: "wazefa" = job/role
        {"DOWR",      TokenType::ROLE},     // Franco: "dowr" = role
        
        {"PASSWORD",  TokenType::PASS},
        {"PASS",      TokenType::PASS},
        
        // =====================================================================
        // SYSTEM COMMANDS
        // =====================================================================
        {"SHOW",      TokenType::SHOW},
        {"WARENY",    TokenType::SHOW},     // Franco: "wareny" = show me
        
        {"WHOAMI",    TokenType::WHOAMI},
        {"ANAMEEN",   TokenType::WHOAMI},   // Franco: "ana meen" = who am I
        
        {"STATUS",    TokenType::STATUS},
        {"7ALAH",     TokenType::STATUS},   // Franco: "7alah" = status
        
        {"DESCRIBE",  TokenType::DESCRIBE},
        {"DESC",      TokenType::DESCRIBE}, // Alias (when standalone)
        {"WASF",      TokenType::DESCRIBE}, // Franco: "wasf" = describe
        
        // =====================================================================
        // COLUMN OPERATIONS
        // =====================================================================
        {"ADD",       TokenType::ADD},
        {"ADAF",      TokenType::ADD},      // Franco: "adaf" = add
        
        {"RENAME",    TokenType::RENAME},
        {"GHAYER_ESM", TokenType::RENAME},  // Franco: "ghayer esm" = change name
        
        {"COLUMN",    TokenType::COLUMN},
        {"3AMOD",     TokenType::COLUMN},   // Franco: "3amod" = column

        // =====================================================================
        // USER ROLES
        // =====================================================================
        {"SUPERADMIN", TokenType::ROLE_SUPERADMIN},
        {"ADMIN",      TokenType::ROLE_ADMIN},
        {"MODEER",     TokenType::ROLE_ADMIN},    // Franco: "modeer" = manager
        {"NORMAL",     TokenType::ROLE_NORMAL},
        {"3ADI",       TokenType::ROLE_NORMAL},   // Franco: "3adi" = normal
        {"READONLY",   TokenType::ROLE_READONLY},
        {"MOSHAHED",   TokenType::ROLE_READONLY}, // Franco: "moshahed" = viewer
        {"DENIED",     TokenType::ROLE_DENIED},
        {"MAMNO3",     TokenType::ROLE_DENIED},   // Franco: "mamno3" = forbidden

        // =====================================================================
        // DATA TYPES
        // =====================================================================
        {"INT",       TokenType::INT_TYPE},
        {"INTEGER",   TokenType::INT_TYPE},
        {"RAKAM",     TokenType::INT_TYPE},     // Franco: "rakam" = number
        
        {"VARCHAR",   TokenType::STRING_TYPE},
        {"TEXT",      TokenType::STRING_TYPE},
        {"STRING",    TokenType::STRING_TYPE},
        {"GOMLA",     TokenType::STRING_TYPE},  // Franco: "gomla" = sentence
        
        {"BOOL",      TokenType::BOOL_TYPE},
        {"BOOLEAN",   TokenType::BOOL_TYPE},
        
        {"DATE",      TokenType::DATE_TYPE},
        {"DATETIME",  TokenType::DATE_TYPE},
        {"TARE5",     TokenType::DATE_TYPE},    // Franco: "tare5" = date
        
        {"DECIMAL",   TokenType::DECIMAL_TYPE},
        {"FLOAT",     TokenType::DECIMAL_TYPE},
        {"DOUBLE",    TokenType::DECIMAL_TYPE},
        {"KASR",      TokenType::DECIMAL_TYPE}, // Franco: "kasr" = fraction
        
        // =====================================================================
        // BOOLEAN LITERALS
        // =====================================================================
        {"TRUE",      TokenType::TRUE_LIT},
        {"AH",        TokenType::TRUE_LIT},     // Franco: "ah" = yes
        
        {"FALSE",     TokenType::FALSE_LIT},
        {"LA",        TokenType::FALSE_LIT},    // Franco: "la" = no

        // =====================================================================
        // LOGICAL OPERATORS
        // =====================================================================
        {"AND",       TokenType::AND},
        {"WE",        TokenType::AND},          // Franco: "we" = and
        
        {"OR",        TokenType::OR},
        {"AW",        TokenType::OR},           // Franco: "aw" = or
        
        {"IN",        TokenType::IN_OP},
        {"FE",        TokenType::IN_OP},        // Franco: "fe" = in
        
        {"ON",        TokenType::ON},
        {"3ALA",      TokenType::ON},           // Franco: "3ala" = on
        
        // =====================================================================
        // PRIMARY KEY / INDEX
        // =====================================================================
        {"PRIMARY",   TokenType::PRIMARY_KEY},
        {"ASASI",     TokenType::PRIMARY_KEY},  // Franco: "asasi" = primary
        
        {"KEY",       TokenType::KEY},
        {"MOFTA7",    TokenType::KEY},          // Franco: "mofta7" = key

        // =====================================================================
        // TRANSACTIONS
        // =====================================================================
        {"BEGIN",     TokenType::BEGIN_TXN},
        {"START",     TokenType::BEGIN_TXN},
        {"2EBDA2",    TokenType::BEGIN_TXN},    // Franco: "ebda2" = start
        
        {"COMMIT",    TokenType::COMMIT},
        {"2AKED",     TokenType::COMMIT},       // Franco: "2aked" = confirm
        
        {"ROLLBACK",  TokenType::ROLLBACK},
        {"2ERGA3",    TokenType::ROLLBACK},     // Franco: "erga3" = go back
        {"UNDO",      TokenType::ROLLBACK},     // Alias
        
        // =====================================================================
        // RECOVERY / TIME TRAVEL
        // =====================================================================
        {"CHECKPOINT", TokenType::CHECKPOINT},
        {"SAVE",       TokenType::CHECKPOINT},  // Alias
        
        {"RECOVER",    TokenType::RECOVER},
        {"ERGA3",      TokenType::RECOVER},     // Franco: "erga3" = return
        
        {"TO",         TokenType::TO},
        {"ELA",        TokenType::TO},          // Franco: "ela" = to
        
        {"LATEST",     TokenType::LATEST},
        {"A5ER",       TokenType::LATEST},      // Franco: "a5er" = latest
        {"ASLHA",      TokenType::LATEST},      // Franco: "aslha" = original
        
        {"NOW",        TokenType::NOW},
        {"DELWA2TY",   TokenType::NOW},         // Franco: "delwa2ty" = now
        
        {"CURRENT",    TokenType::CURRENT},
        {"7ALY",       TokenType::CURRENT},     // Franco: "7aly" = current
        
        {"AS",         TokenType::AS},
        {"K",          TokenType::AS},          // Franco: "k" = as
        
        {"OF",         TokenType::OF},
        
        // =====================================================================
        // CONDITIONAL (IF EXISTS)
        // =====================================================================
        {"IF",         TokenType::IF},
        {"LAW",        TokenType::IF},          // Franco: "law" = if

        {"EXISTS",     TokenType::EXISTS},
        {"MAWGOOD",    TokenType::EXISTS},      // Franco: "mawgood" = exists

        // =====================================================================
        // GROUP BY & AGGREGATES
        // =====================================================================
        {"GROUP",     TokenType::GROUP},
        {"MAGMO3A",   TokenType::GROUP},        // Franco: "magmo3a" = group
        
        {"BY",        TokenType::BY},
        {"B",         TokenType::BY},           // Franco shorthand
        
        {"HAVING",    TokenType::HAVING},
        {"ETHA",      TokenType::HAVING},       // Franco: "etha" = if
        {"LAKEN",     TokenType::HAVING},       // Franco: "laken" = but
        
        {"COUNT",     TokenType::COUNT},
        {"3ADD",      TokenType::COUNT},        // Franco: "3add" = count
        
        {"SUM",       TokenType::SUM},
        {"MAG3MO3",   TokenType::SUM},          // Franco: "mag3mo3" = sum
        
        {"AVG",       TokenType::AVG},
        {"AVERAGE",   TokenType::AVG},
        {"MOTOWASET", TokenType::AVG},          // Franco: "motowaset" = average
        
        {"MIN",       TokenType::MIN_AGG},
        {"ASGAR",     TokenType::MIN_AGG},      // Franco: "asgar" = smallest
        
        {"MAX",       TokenType::MAX_AGG},
        {"AKBAR",     TokenType::MAX_AGG},      // Franco: "akbar" = biggest
        
        // =====================================================================
        // ORDER BY
        // =====================================================================
        {"ORDER",     TokenType::ORDER},
        {"RATEB",     TokenType::ORDER},        // Franco: "rateb" = arrange
        
        {"ASC",       TokenType::ASC},
        {"ASCENDING", TokenType::ASC},
        {"TASE3DI",   TokenType::ASC},          // Franco: "tase3di" = ascending
        {"TALE3",     TokenType::ASC},          // Franco: "tale3" = going up
        
        {"DESC",      TokenType::DESC},         // Note: Also DESCRIBE when standalone
        {"DESCENDING", TokenType::DESC},
        {"TANAZOLI",  TokenType::DESC},         // Franco: "tanazoli" = descending
        {"NAZL",      TokenType::DESC},         // Franco: "nazl" = going down
        
        // =====================================================================
        // LIMIT / OFFSET
        // =====================================================================
        {"LIMIT",     TokenType::LIMIT},
        {"7ADD",      TokenType::LIMIT},        // Franco: "7add" = limit
        
        {"OFFSET",    TokenType::OFFSET},
        {"SKIP",      TokenType::OFFSET},       // Alias
        {"EBDA2MEN",  TokenType::OFFSET},       // Franco: "ebda2 men" = start from
        
        // =====================================================================
        // DISTINCT / ALL
        // =====================================================================
        {"DISTINCT",  TokenType::DISTINCT},
        {"UNIQUE",    TokenType::DISTINCT},     // Sometimes used as alias
        {"MOTA3MEZ",  TokenType::DISTINCT},     // Franco: "mota3mez" = distinct
        
        {"ALL",       TokenType::ALL},
        {"KOL",       TokenType::ALL},          // Franco: "kol" = all
        
        // =====================================================================
        // JOINS
        // =====================================================================
        {"JOIN",      TokenType::JOIN},
        {"ENTEDAH",   TokenType::JOIN},         // Franco: "entedah" = join
        
        {"INNER",     TokenType::INNER},
        {"DA5ELY",    TokenType::INNER},        // Franco: "da5ely" = inner
        
        {"LEFT",      TokenType::LEFT},
        {"SHMAL",     TokenType::LEFT},         // Franco: "shmal" = left
        
        {"RIGHT",     TokenType::RIGHT},
        {"YAMEN",     TokenType::RIGHT},        // Franco: "yamen" = right
        
        {"OUTER",     TokenType::OUTER},
        {"5AREGY",    TokenType::OUTER},        // Franco: "5aregy" = outer
        
        {"CROSS",     TokenType::CROSS},
        {"TAQATE3",   TokenType::CROSS},        // Franco: "taqate3" = intersect
        
        {"FULL",      TokenType::OUTER},        // FULL OUTER JOIN
        
        // =====================================================================
        // FOREIGN KEYS
        // =====================================================================
        {"FOREIGN",    TokenType::FOREIGN},
        
        {"REFERENCES", TokenType::REFERENCES},
        {"YOSHEER",    TokenType::REFERENCES},  // Franco: "yosheer" = points to
        
        {"CASCADE",    TokenType::CASCADE},
        {"TATABE3",    TokenType::CASCADE},     // Franco: "tatabe3" = follow
        
        {"RESTRICT",   TokenType::RESTRICT},
        {"MANE3",      TokenType::RESTRICT},    // Franco: "mane3" = prevent
        
        {"NO",         TokenType::NO},
        
        {"ACTION",     TokenType::ACTION},
        {"E3RA2",      TokenType::ACTION},      // Franco: "e3ra2" = action
        
        // =====================================================================
        // CONSTRAINTS
        // =====================================================================
        {"NULL",       TokenType::NULL_LIT},
        {"FADY",       TokenType::NULL_LIT},    // Franco: "fady" = empty
        
        {"NOT",        TokenType::NOT},
        {"MESH",       TokenType::NOT},         // Franco: "mesh" = not
        
        {"DEFAULT",    TokenType::DEFAULT_KW},
        {"EFRADY",     TokenType::DEFAULT_KW},  // Franco: "efrady" = default
        
        {"UNIQUE",     TokenType::UNIQUE},
        {"WAHED",      TokenType::UNIQUE},      // Franco: "wahed" = one/unique
        
        {"CHECK",      TokenType::CHECK},
        {"FA7S",       TokenType::CHECK},       // Franco: "fa7s" = check
        
        {"AUTO_INCREMENT", TokenType::AUTO_INCREMENT},
        {"AUTOINCREMENT",  TokenType::AUTO_INCREMENT},
        {"SERIAL",         TokenType::AUTO_INCREMENT},  // PostgreSQL style
        {"TAZAYED",        TokenType::AUTO_INCREMENT},  // Franco: "tazayed" = increment
        
        // =====================================================================
        // AI LAYER
        // =====================================================================
        {"AI",         TokenType::AI},
        {"ZAKA2",      TokenType::AI},           // Franco: "zaka2" = intelligence
        {"ANOMALIES",  TokenType::ANOMALIES},
        {"SHOZOOZ",    TokenType::ANOMALIES},    // Franco: "shozooz" = anomalies
        {"EXECUTION",  TokenType::EXECUTION},
        {"TANFEEZ",    TokenType::EXECUTION},    // Franco: "tanfeez" = execution
        {"STATS",      TokenType::STATS},
        {"E7SA2EYAT",  TokenType::STATS},        // Franco: "e7sa2eyat" = statistics

        // =====================================================================
        // SERVER CONTROL
        // =====================================================================
        {"STOP",       TokenType::STOP},
        {"WA2AF",      TokenType::STOP},        // Franco: "wa2af" = stop
        {"SHUTDOWN",   TokenType::SHUTDOWN},
        {"2AFOL",      TokenType::SHUTDOWN}     // Franco: "2afol" = close/shutdown
    };

    Token Lexer::NextToken() {
        SkipWhitespace();
        if (cursor_ >= input_.length()) return {TokenType::EOF_TOKEN, ""};

        char c = input_[cursor_];

        // 1. Handle Words and Positive Numbers
        if (std::isalnum(c)) {
            return ReadIdentifierOrNumber();
        }

        // 2. NEW: Handle Negative Numbers (Start with '-')
        // We verify: Is it a '-', and is the NEXT char a digit? (e.g. -5)
        if (c == '-') {
            if (cursor_ + 1 < input_.length() && std::isdigit(input_[cursor_ + 1])) {
                return ReadIdentifierOrNumber();
            }
        }

        // 3. Handle Strings
        if (c == '\'') return ReadString();

        // 4. Handle Symbols
        cursor_++;
        switch (c) {
            case '*': return {TokenType::STAR, "*"};
            case ',': return {TokenType::COMMA, ","};
            case '(': return {TokenType::L_PAREN, "("};
            case ')': return {TokenType::R_PAREN, ")"};
            case ';': return {TokenType::SEMICOLON, ";"};
            case '=': return {TokenType::EQUALS, "="};
            case '>':
                if (cursor_ < input_.length() && input_[cursor_] == '=') {
                    cursor_++;
                    return {TokenType::IDENTIFIER, ">="};
                } else {
                    return {TokenType::IDENTIFIER, ">"};
                }
            case '<':
                if (cursor_ < input_.length() && input_[cursor_] == '=') {
                    cursor_++;
                    return {TokenType::IDENTIFIER, "<="};
                } else {
                    return {TokenType::IDENTIFIER, "<"};
                }
            // Note: If we support math later, independent '-' would go here.
            default:  return {TokenType::INVALID, std::string(1, c)};
        }
    }

    Token Lexer::ReadIdentifierOrNumber() {
        size_t start = cursor_;
        bool has_letter = false;
        bool has_decimal_point = false;

        // Handle optional leading negative sign
        if (input_[cursor_] == '-') {
            cursor_++;
        }

        while (cursor_ < input_.length()) {
            char c = input_[cursor_];
        
            // 1. Alphanumeric? Keep reading
            if (std::isalnum(c) || c == '_') {
                if (std::isalpha(c)) has_letter = true;
                cursor_++;
            } 
            // 2. Decimal Point logic
            else if (c == '.' && !has_letter && !has_decimal_point) {
                if (cursor_ + 1 < input_.length() && std::isdigit(input_[cursor_ + 1])) {
                    has_decimal_point = true;
                    cursor_++;
                } else {
                    break; 
                }
            } 
            else {
                break; 
            }
        }

        std::string text = input_.substr(start, cursor_ - start);

        // If it's a Keyword/Identifier, we check the map
        // (Note: Identifiers usually don't start with -, so -5 is safe)
        if (has_letter) {
            // Uppercase conversion for Case Insensitivity
            std::string upper_text = text;
            std::transform(upper_text.begin(), upper_text.end(), upper_text.begin(), ::toupper);

            auto it = kKeywords.find(upper_text);
            if (it != kKeywords.end()) {
                return {it->second, text}; 
            }
            return {TokenType::IDENTIFIER, text};
        }

        // If we are here, it's a number
        if (has_decimal_point) {
            return {TokenType::DECIMAL_LITERAL, text};
        }

        return {TokenType::NUMBER, text};
    }

    Token Lexer::ReadString() {
        cursor_++; // Skip opening '
        size_t start = cursor_;
        while (cursor_ < input_.length() && input_[cursor_] != '\'') {
            cursor_++;
        }
        std::string text = input_.substr(start, cursor_ - start);
        if (cursor_ < input_.length()) cursor_++; // Skip closing '
        return {TokenType::STRING_LIT, text};
    }

    void Lexer::SkipWhitespace() {
        while (cursor_ < input_.length() && std::isspace(input_[cursor_])) {
            cursor_++;
        }
    }

    // Helper for bulk tokenization
    std::vector<Token> Lexer::Tokenize() {
        std::vector<Token> tokens;
        Token tok;
        while ((tok = NextToken()).type != TokenType::EOF_TOKEN) {
            tokens.push_back(tok);
        }
        tokens.push_back(tok); // Add EOF
        return tokens;
    }

    // Static method to access keywords map
    const std::map<std::string, TokenType>& Lexer::GetKeywords() {
        return kKeywords;
    }

    // Helper to get English name for a token type
    std::string Lexer::GetTokenTypeName(TokenType type) {
        switch(type) {
            // Commands
            case TokenType::SELECT: return "SELECT";
            case TokenType::FROM: return "FROM";
            case TokenType::WHERE: return "WHERE";
            case TokenType::CREATE: return "CREATE";
            case TokenType::DATABASE: return "DATABASE";
            case TokenType::DATABASES: return "DATABASES";
            case TokenType::TABLE: return "TABLE";
            case TokenType::USE: return "USE";
            case TokenType::LOGIN: return "LOGIN";
            case TokenType::DELETE_CMD: return "DELETE";
            case TokenType::UPDATE_SET: return "SET";
            case TokenType::UPDATE_CMD: return "UPDATE";
            case TokenType::INSERT: return "INSERT";
            case TokenType::INTO: return "INTO";
            case TokenType::VALUES: return "VALUES";
            
            // User Management
            case TokenType::USER: return "USER";
            case TokenType::ROLE: return "ROLE";
            case TokenType::PASS: return "PASSWORD";
            case TokenType::SHOW: return "SHOW";
            case TokenType::WHOAMI: return "WHOAMI";
            case TokenType::STATUS: return "STATUS";
            
            // Roles
            case TokenType::ROLE_SUPERADMIN: return "SUPERADMIN";
            case TokenType::ROLE_ADMIN: return "ADMIN";
            case TokenType::ROLE_NORMAL: return "NORMAL";
            case TokenType::ROLE_READONLY: return "READONLY";
            case TokenType::ROLE_DENIED: return "DENIED";
            
            // Types
            case TokenType::INT_TYPE: return "INT";
            case TokenType::STRING_TYPE: return "VARCHAR/STRING";
            case TokenType::BOOL_TYPE: return "BOOL";
            case TokenType::DATE_TYPE: return "DATE";
            case TokenType::DECIMAL_TYPE: return "DECIMAL/FLOAT";
            
            // Boolean Values
            case TokenType::TRUE_LIT: return "TRUE";
            case TokenType::FALSE_LIT: return "FALSE";
            
            // Logical Operators
            case TokenType::AND: return "AND";
            case TokenType::OR: return "OR";
            case TokenType::IN_OP: return "IN";
            case TokenType::ON: return "ON";
            
            // Index & Constraints
            case TokenType::INDEX: return "INDEX";
            case TokenType::PRIMARY_KEY: return "PRIMARY KEY";
            
            // Transactions
            case TokenType::BEGIN_TXN: return "BEGIN";
            case TokenType::COMMIT: return "COMMIT";
            case TokenType::ROLLBACK: return "ROLLBACK";
            
                
            case TokenType::CHECKPOINT: return "CHECKPOINT";
            case TokenType::RECOVER:    return "RECOVER";
            case TokenType::TO:         return "TO";
            case TokenType::LATEST:     return "LATEST";
            case TokenType::NOW:        return "NOW";
            case TokenType::CURRENT:    return "CURRENT";
            case TokenType::AS:         return "AS";
            case TokenType::OF:         return "OF";
            case TokenType::STOP:       return "STOP";
            case TokenType::SHUTDOWN:   return "SHUTDOWN";

            // AI Layer
            case TokenType::AI:         return "AI";
            case TokenType::ANOMALIES:  return "ANOMALIES";
            case TokenType::EXECUTION:  return "EXECUTION";
            case TokenType::STATS:      return "STATS";

            // GROUP BY & Aggregates
            case TokenType::GROUP: return "GROUP";
            case TokenType::BY: return "BY";
            case TokenType::HAVING: return "HAVING";
            case TokenType::COUNT: return "COUNT";
            case TokenType::SUM: return "SUM";
            case TokenType::AVG: return "AVG";
            case TokenType::MIN_AGG: return "MIN";
            case TokenType::MAX_AGG: return "MAX";
            
            // ORDER BY
            case TokenType::ORDER: return "ORDER";
            case TokenType::ASC: return "ASC";
            case TokenType::DESC: return "DESC";
            
            // LIMIT/OFFSET
            case TokenType::LIMIT: return "LIMIT";
            case TokenType::OFFSET: return "OFFSET";
            
            // DISTINCT
            case TokenType::DISTINCT: return "DISTINCT";
            case TokenType::ALL: return "ALL";
            
            // JOINS
            case TokenType::JOIN: return "JOIN";
            case TokenType::INNER: return "INNER";
            case TokenType::LEFT: return "LEFT";
            case TokenType::RIGHT: return "RIGHT";
            case TokenType::OUTER: return "OUTER";
            case TokenType::CROSS: return "CROSS";
            
            // FOREIGN KEYS
            case TokenType::FOREIGN: return "FOREIGN";
            case TokenType::KEY: return "KEY";
            case TokenType::REFERENCES: return "REFERENCES";
            case TokenType::CASCADE: return "CASCADE";
            case TokenType::RESTRICT: return "RESTRICT";
            case TokenType::SET: return "SET";
            case TokenType::NO: return "NO";
            case TokenType::ACTION: return "ACTION";
            
            // CONSTRAINTS
            case TokenType::NULL_LIT: return "NULL";
            case TokenType::NOT: return "NOT";
            case TokenType::DEFAULT_KW: return "DEFAULT";
            case TokenType::UNIQUE: return "UNIQUE";
            case TokenType::CHECK: return "CHECK";
            case TokenType::AUTO_INCREMENT: return "AUTO_INCREMENT";
            
            default: return "UNKNOWN";
        }
    }
} // namespace chronosdb
