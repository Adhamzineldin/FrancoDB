#pragma once
#include <string>

namespace francodb {

    enum class TokenType {
        // Keywords (Your Custom Franco Style)
        SELECT,      // 2E5TAR
        FROM,        // MEN
        WHERE,       // LAMA
        CREATE,      // 2E3MEL
        DELETE_CMD,      // 2EMSA7
        UPDATE_SET,  // 5ALY
        UPDATE_CMD,      // 3ADEL
        TABLE,       // GADWAL
        DATABASE,    // DATABASE
        USE,         // 2ESTA5DEM / USE
        LOGIN,       // LOGIN
        USER,        // USER
        ROLE,        // ROLE
        SHOW,        // SHOW / WARENY
        WHOAMI,      // WHOAMI
        STATUS,      // STATUS
        DATABASES,   // DATABASES
        PASS,        // PASS (for CREATE USER)
        INSERT,      // EMLA
        INTO,        // GOWA
        VALUES,      // ELKEYAM
    
        // Types
        INT_TYPE,    // RAKAM
        STRING_TYPE, // GOMLA
        BOOL_TYPE,   // BOOL
        DATE_TYPE,   // TARE5
        DECIMAL_TYPE,    // KASR
        INDEX, // FEHRIS
        PRIMARY_KEY, // RAKAM ASASI or MOFTA7 ASASI
        BEGIN_TXN, // 2EBDA2
        ROLLBACK, // 2ERGA3
        COMMIT, // 2AKED
    
        DECIMAL_LITERAL,
        TRUE_LIT,    // AH
        FALSE_LIT,   // LA
    
        // LOGICAL OPERATORS
        AND,         // WE
        OR,          // AW
        ON,     // 3ALA
        
        
        // Literals & Symbols
        IDENTIFIER,  // The name used in code (e.g., users, id)
        NUMBER,      // 123
        STRING_LIT,  // 'Ahmed'
        COMMA,       // ,
        L_PAREN,     // (
        R_PAREN,     // )
        SEMICOLON,   // ;
        EQUALS,      // =
        STAR,        // *
        EOF_TOKEN,   // End of query
        INVALID      // For characters the lexer doesn't recognize
    };

    struct Token {
        TokenType type;
        std::string text;
    };

} // namespace francodb