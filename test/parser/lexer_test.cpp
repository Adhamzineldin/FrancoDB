#include <iostream>
#include <vector>
#include <cassert>
#include "parser/lexer.h"

using namespace francodb;

void TestLexer() {
    std::cout << "[TEST] Starting Franco Lexer Test..." << std::endl;

    // A complete Franco Create Table query
    std::string input = "2E3MEL gadwal users (id RAKAM, name GOMLA);";
    std::cout << "Test Input: " << input << std::endl;
    Lexer lexer(input);

    struct ExpectedToken {
        TokenType type;
        std::string text;
    };

    std::vector<ExpectedToken> expected = {
        {TokenType::CREATE, "2E3MEL"},
        {TokenType::TABLE, "GADWAL"},
        {TokenType::IDENTIFIER, "users"},
        {TokenType::L_PAREN, "("},
        {TokenType::IDENTIFIER, "id"},
        {TokenType::INT_TYPE, "RAKAM"},
        {TokenType::COMMA, ","},
        {TokenType::IDENTIFIER, "name"},
        {TokenType::STRING_TYPE, "GOMLA"},
        {TokenType::R_PAREN, ")"},
        {TokenType::SEMICOLON, ";"}
    };

    for (const auto& exp : expected) {
        Token tok = lexer.NextToken();
        std::cout << "  -> Found Token: " << tok.text << std::endl;
        assert(tok.type == exp.type);
    }

    // Test a SELECT/WHERE query
    std::cout << "[STEP 2] Testing SELECT with LAMA (WHERE)..." << std::endl;
    std::string input2 = "2E5TAR * MEN users LAMA id = 42;";
    Lexer lexer2(input2);

    // Skip to LAMA
    lexer2.NextToken(); // 2E5TAR
    lexer2.NextToken(); // * (This will currently be INVALID until we add '*' to switch case)
    lexer2.NextToken(); // MEN
    lexer2.NextToken(); // users
    
    Token lama_tok = lexer2.NextToken();
    assert(lama_tok.type == TokenType::WHERE);
    std::cout << "  -> Successfully recognized 'LAMA' as WHERE." << std::endl;

    std::cout << "[SUCCESS] Lexer speaks Franco fluently!" << std::endl;
}

