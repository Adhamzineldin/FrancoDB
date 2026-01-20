#include "parser/lexer.h"
#include <map>
#include <cctype>
#include <algorithm>

namespace francodb {
    // Keywords map - accessible via GetKeywords() static method
    static const std::map<std::string, TokenType> kKeywords = {
        // --- COMMANDS ---
        {"2E5TAR", TokenType::SELECT},
        {"MEN", TokenType::FROM},
        {"LAMA", TokenType::WHERE},
        {"2E3MEL", TokenType::CREATE},
        {"DATABASE", TokenType::DATABASE},
        {"DATABASES", TokenType::DATABASES},
        {"GADWAL", TokenType::TABLE},
        {"2ESTA5DEM", TokenType::USE},
        {"USE", TokenType::USE},
        {"LOGIN", TokenType::LOGIN},
        
        // --- USER MGMT ---
        {"MOSTA5DEM", TokenType::USER},
        {"USER", TokenType::USER},
        {"3ABD", TokenType::USER},
        {"WAZEFA", TokenType::ROLE},
        {"ROLE",   TokenType::ROLE},
        {"DOWR",   TokenType::ROLE},
        {"PASSWORD", TokenType::PASS},
        {"WARENY", TokenType::SHOW},
        {"SHOW",   TokenType::SHOW},
        {"ANAMEEN", TokenType::WHOAMI},
        {"WHOAMI",  TokenType::WHOAMI},
        {"7ALAH",   TokenType::STATUS},
        {"STATUS",  TokenType::STATUS},
        {"WASF",    TokenType::DESCRIBE},
        {"DESCRIBE", TokenType::DESCRIBE},
        {"ALTER",   TokenType::ALTER},
        {"ADAF",    TokenType::ADD},
        {"ADD",     TokenType::ADD},
        {"GHAYER_ESM", TokenType::RENAME},
        {"RENAME",  TokenType::RENAME},
        {"3AMOD",   TokenType::COLUMN},
        {"COLUMN",  TokenType::COLUMN},

        // --- DATA MODIFICATION ---
        {"2EMSA7",  TokenType::DELETE_CMD}, 
        {"5ALY",    TokenType::UPDATE_SET},
        {"3ADEL",   TokenType::UPDATE_CMD},
        {"EMLA",    TokenType::INSERT},
        {"GOWA",    TokenType::INTO},
        {"ELKEYAM", TokenType::VALUES},

        // --- ROLES (NEW) ---
        {"SUPERADMIN", TokenType::ROLE_SUPERADMIN},
        {"ADMIN",      TokenType::ROLE_ADMIN},
        {"MODEER",     TokenType::ROLE_ADMIN},
        {"NORMAL",     TokenType::ROLE_NORMAL},
        {"3ADI",       TokenType::ROLE_NORMAL},
        {"READONLY",   TokenType::ROLE_READONLY},
        {"MOSHAHED",   TokenType::ROLE_READONLY},
        {"DENIED",     TokenType::ROLE_DENIED},
        {"MAMNO3",     TokenType::ROLE_DENIED},

        // --- TYPES ---
        {"RAKAM", TokenType::INT_TYPE},
        {"GOMLA", TokenType::STRING_TYPE},
        {"BOOL",  TokenType::BOOL_TYPE},
        {"TARE5", TokenType::DATE_TYPE},
        {"KASR",  TokenType::DECIMAL_TYPE},
        
        // --- VALUES ---
        {"AH",    TokenType::TRUE_LIT},
        {"LA",    TokenType::FALSE_LIT},

        // --- LOGIC / OPS ---
        {"WE",    TokenType::AND},
        {"AW",    TokenType::OR},
        {"FE",    TokenType::IN_OP},
        {"3ALA",  TokenType::ON},
        
        // --- INDEX / PK ---
        {"FEHRIS", TokenType::INDEX},
        {"ASASI",  TokenType::PRIMARY_KEY},
        {"MOFTA7", TokenType::KEY},

        // --- TRANSACTIONS ---
        {"2EBDA2", TokenType::BEGIN_TXN},
        {"2ERGA3", TokenType::ROLLBACK},
        {"2AKED",  TokenType::COMMIT},
        
        // --- GROUP BY & AGGREGATES ---
        {"MAGMO3A", TokenType::GROUP},
        {"GROUP",   TokenType::GROUP},
        {"B",       TokenType::BY},
        {"BY",      TokenType::BY},
        {"ETHA",    TokenType::HAVING},
        {"LAKEN",   TokenType::HAVING},
        {"HAVING",  TokenType::HAVING},
        {"3ADD",    TokenType::COUNT},
        {"COUNT",   TokenType::COUNT},
        {"MAG3MO3", TokenType::SUM},
        {"SUM",     TokenType::SUM},
        {"MOTOWASET", TokenType::AVG},
        {"AVG",     TokenType::AVG},
        {"ASGAR",   TokenType::MIN_AGG},
        {"MIN",     TokenType::MIN_AGG},
        {"AKBAR",   TokenType::MAX_AGG},
        {"MAX",     TokenType::MAX_AGG},
        
        // --- ORDER BY ---
        {"RATEB",    TokenType::ORDER},
        {"ORDER",    TokenType::ORDER},
        {"TASE3DI",  TokenType::ASC},
        {"TALE3",    TokenType::ASC},
        {"ASC",      TokenType::ASC},
        {"TANAZOLI", TokenType::DESC},
        {"NAZL",     TokenType::DESC},
        {"DESC",     TokenType::DESC},
        
        // --- LIMIT / OFFSET ---
        {"7ADD",      TokenType::LIMIT},
        {"LIMIT",     TokenType::LIMIT},
        {"EBDA2MEN",  TokenType::OFFSET},
        {"OFFSET",    TokenType::OFFSET},
        
        // --- DISTINCT ---
        {"MOTA3MEZ", TokenType::DISTINCT},
        {"DISTINCT", TokenType::DISTINCT},
        {"KOL",      TokenType::ALL},
        {"ALL",      TokenType::ALL},
        
        // --- JOINS ---
        {"ENTEDAH",  TokenType::JOIN},
        {"JOIN",     TokenType::JOIN},
        {"DA5ELY",   TokenType::INNER},
        {"INNER",    TokenType::INNER},
        {"SHMAL",    TokenType::LEFT},
        {"LEFT",     TokenType::LEFT},
        {"YAMEN",    TokenType::RIGHT},
        {"RIGHT",    TokenType::RIGHT},
        {"5AREGY",   TokenType::OUTER},
        {"OUTER",    TokenType::OUTER},
        {"TAQATE3",  TokenType::CROSS},
        {"CROSS",    TokenType::CROSS},
        
        // --- FOREIGN KEYS ---
        {"FOREIGN",    TokenType::FOREIGN},
        {"KEY",        TokenType::KEY},
        {"YOSHEER",    TokenType::REFERENCES},
        {"REFERENCES", TokenType::REFERENCES},
        {"TATABE3",    TokenType::CASCADE},
        {"CASCADE",    TokenType::CASCADE},
        {"MANE3",      TokenType::RESTRICT},
        {"RESTRICT",   TokenType::RESTRICT},
        {"SET",        TokenType::SET},
        {"NO",         TokenType::NO},
        {"E3RA2",      TokenType::ACTION},
        {"ACTION",     TokenType::ACTION},
        
        // --- CONSTRAINTS ---
        {"FADY",         TokenType::NULL_LIT},
        {"NULL",         TokenType::NULL_LIT},
        {"MESH",         TokenType::NOT},
        {"NOT",          TokenType::NOT},
        {"EFRADY",       TokenType::DEFAULT_KW},
        {"DEFAULT",      TokenType::DEFAULT_KW},
        {"WAHED",        TokenType::UNIQUE},
        {"UNIQUE",       TokenType::UNIQUE},
        {"FA7S",         TokenType::CHECK},
        {"CHECK",        TokenType::CHECK},
        {"TAZAYED",      TokenType::AUTO_INCREMENT},
        {"AUTO_INCREMENT", TokenType::AUTO_INCREMENT}
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
} // namespace francodb
