#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>
#include "parser/parser.h"

using namespace francodb;

// Helper to catch errors
void ExpectException(const std::string &sql, const std::string &test_name) {
    try {
        Lexer lexer(sql);
        Parser parser(std::move(lexer));
        parser.ParseQuery();
        std::cout << "[FAIL] " << test_name << " should have thrown error but didn't!" << std::endl;
        exit(1);
    } catch (const Exception &e) {
        std::cout << "[PASS] " << test_name << " correctly threw: " << e.what() << std::endl;
    }
}

void TestParser() {
    std::cout << "========================================" << std::endl;
    std::cout << "   STARTING ULTIMATE PARSER STRESS TEST" << std::endl;
    std::cout << "========================================" << std::endl;

    // --- TEST 1: NEGATIVE NUMBERS & DECIMALS ---
    std::cout << "\n[TEST 1] Negative Numbers & Decimals..." << std::endl;
    std::string neg_sql = "EMLA GOWA data ELKEYAM (-50, -10.55, 0, 0.0);";
    Lexer lexer1(neg_sql);
    Parser parser1(std::move(lexer1));
    auto stmt1 = parser1.ParseQuery();

    auto *insert = dynamic_cast<InsertStatement *>(stmt1.get());
    assert(insert->values_[0].GetAsInteger() == -50);
    assert(std::abs(insert->values_[1].GetAsDouble() - (-10.55)) < 0.001);
    assert(insert->values_[2].GetAsInteger() == 0);
    std::cout << " -> SUCCESS: Handled -50 and -10.55 correctly." << std::endl;


    // --- TEST 2: DATES (TARE5) ---
    // Note: Parsers treat dates as Strings first. 
    std::cout << "\n[TEST 2] Dates (TARE5) handling..." << std::endl;
    std::string date_sql = "2E3MEL GADWAL events (event_date TARE5);";
    Lexer lexer2(date_sql);
    Parser parser2(std::move(lexer2));
    auto stmt2 = parser2.ParseQuery();

    auto *create = dynamic_cast<CreateStatement *>(stmt2.get());
    assert(create->columns_[0].GetType() == TypeId::TIMESTAMP);

    // Test inserting a date string
    std::string date_ins = "EMLA GOWA events ELKEYAM ('2026-01-13');";
    Lexer lexer2b(date_ins);
    Parser parser2b(std::move(lexer2b));
    auto stmt2b = parser2b.ParseQuery();
    auto *insert_date = dynamic_cast<InsertStatement *>(stmt2b.get());

    // It should parse as a String Literal (Value class handles format later)
    assert(insert_date->values_[0].GetTypeId() == TypeId::VARCHAR);
    assert(insert_date->values_[0].GetAsString() == "2026-01-13");
    std::cout << " -> SUCCESS: TARE5 type recognized, Date String parsed safely." << std::endl;


    // --- TEST 3: CASE INSENSITIVITY ---
    std::cout << "\n[TEST 3] Case Insensitivity (Mix of upper/lower)..." << std::endl;
    std::string mix_sql = "2e3mel gadwal USERS (id rakam);"; // lowercase keywords
    Lexer lexer3(mix_sql);
    Parser parser3(std::move(lexer3));
    auto stmt3 = parser3.ParseQuery();
    assert(stmt3->GetType() == StatementType::CREATE);
    std::cout << " -> SUCCESS: '2e3mel' handled same as '2E3MEL'." << std::endl;


    // --- TEST 4: COMPLEX WHERE CLAUSE ---
    std::cout << "\n[TEST 4] Complex Logic Chain (AND/OR/Negative)..." << std::endl;
    // "SELECT * FROM t WHERE col1 = -5 AND col2 = 10.5 OR col3 = AH"
    std::string where_sql = "2E5TAR * MEN t LAMA col1 = -5 WE col2 = 10.5 AW col3 = AH;";
    Lexer lexer4(where_sql);
    Parser parser4(std::move(lexer4));
    auto stmt4 = parser4.ParseQuery();
    auto *sel = dynamic_cast<SelectStatement *>(stmt4.get());

    assert(sel->where_clause_.size() == 3);
    assert(sel->where_clause_[0].value.GetAsInteger() == -5);
    assert(sel->where_clause_[0].next_logic == LogicType::AND); // WE
    assert(sel->where_clause_[1].next_logic == LogicType::OR); // AW
    std::cout << " -> SUCCESS: Parsed negative value in WHERE with logic chain." << std::endl;


    // --- TEST 5: EDGE CASE - SYNTAX ERRORS ---
    std::cout << "\n[TEST 5] Edge Case: Bad Syntax..." << std::endl;

    // Case A: Missing Table Name
    ExpectException("2E3MEL GADWAL (id RAKAM);", "Missing Table Name");

    // Case B: Garbage Token
    ExpectException("2E5TAR * MEN users @@@;", "Invalid Token");

    // Case C: Missing Parenthesis
    ExpectException("EMLA GOWA users ELKEYAM 1, 2;", "Missing Parenthesis");
    
    // Case D: Garbage before Semicolon
    // This ensures "SELECT * FROM users GARBAGE ;" fails
    ExpectException("2E5TAR * MEN users @@@ ;", "Garbage inside command");

    std::cout << "========================================" << std::endl;
    std::cout << "[SUCCESS] ALL EDGE CASES PASSED." << std::endl;
    std::cout << "========================================" << std::endl;
}

int main() {
    TestParser();
    return 0;
}
