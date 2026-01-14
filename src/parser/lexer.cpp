#include "parser/lexer.h"
#include <map>
#include <cctype>
#include <algorithm>

namespace francodb {
    static const std::map<std::string, TokenType> kKeywords = {
        {"2E5TAR", TokenType::SELECT},
        {"MEN", TokenType::FROM},
        {"LAMA", TokenType::WHERE},
        {"2E3MEL", TokenType::CREATE},
        {"2EMSA7",  TokenType::DELETE_CMD}, 
        {"5ALY",    TokenType::UPDATE_SET},
        {"3ADEL",  TokenType::UPDATE_CMD},
        {"GADWAL", TokenType::TABLE},
        {"EMLA", TokenType::INSERT},
        {"GOWA", TokenType::INTO},
        {"ELKEYAM", TokenType::VALUES},
        {"RAKAM", TokenType::INT_TYPE},
        {"GOMLA", TokenType::STRING_TYPE},
        {"WE",     TokenType::AND},
        {"AW",     TokenType::OR},
        {"BOOL", TokenType::BOOL_TYPE},
        {"TARE5",  TokenType::DATE_TYPE},
        {"AH",      TokenType::TRUE_LIT},  // True
        {"LA",      TokenType::FALSE_LIT},
        {"KASR",    TokenType::DECIMAL_TYPE},
        {"FEHRIS", TokenType::INDEX},
        {"3ALA",   TokenType::ON},
        {"ASASI", TokenType::PRIMARY_KEY},  // PRIMARY KEY keyword (used after RAKAM or MIFTAH)
        {"MOFTA7", TokenType::PRIMARY_KEY}, // Alternative: MIFTAH ASASI (but we'll use ASASI separately)
        {"2EBDA2", TokenType::BEGIN_TXN},     // BEGIN transaction
        {"2ERGA3", TokenType::ROLLBACK},     // ROLLBACK transaction
        {"2AKED", TokenType::COMMIT},       // COMMIT transaction
        
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
} // namespace francodb
