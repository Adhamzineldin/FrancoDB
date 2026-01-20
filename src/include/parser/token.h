#pragma once
#include <string>

namespace francodb {

    enum class TokenType {
        // --- KEYWORDS ---
        SELECT,      // 2E5TAR
        FROM,        // MEN
        WHERE,       // LAMA
        CREATE,      // 2E3MEL
        DELETE_CMD,  // 2EMSA7
        UPDATE_SET,  // 5ALY
        UPDATE_CMD,  // 3ADEL
        TABLE,       // GADWAL
        DATABASE,    // DATABASE
        USE,         // 2ESTA5DEM / USE
        LOGIN,       // LOGIN
        USER,        // USER / MOSTA5DEM
        ROLE,        // ROLE / WAZEFA / DOWR
        SHOW,        // SHOW / WARENY
        WHOAMI,      // ANAMEEN / WHOAMI
        STATUS,      // 7ALAH / STATUS
        DATABASES,   // DATABASES
        PASS,        // PASSWORD
        DESCRIBE,    // WASF / DESCRIBE / DESC (when standalone)
        ALTER,       // 3ADEL / ALTER (for ALTER TABLE)
        ADD,         // ADAF / ADD
        DROP,        // 2EMSA7 (when in context of ALTER)
        MODIFY,      // 3ADEL (when modifying column)
        RENAME,      // GHAYER_ESM / RENAME
        COLUMN,      // 3AMOD / COLUMN
        INSERT,      // EMLA
        INTO,        // GOWA
        VALUES,      // ELKEYAM

        // --- SPECIFIC ROLE TOKENS (For differentiation) ---
        ROLE_SUPERADMIN, // SUPERADMIN
        ROLE_ADMIN,      // ADMIN / MODEER
        ROLE_NORMAL,     // NORMAL / 3ADI
        ROLE_READONLY,   // READONLY / MOSHAHED
        ROLE_DENIED,     // DENIED / MAMNO3

        // --- TYPES ---
        INT_TYPE,    // RAKAM
        STRING_TYPE, // GOMLA
        BOOL_TYPE,   // BOOL
        DATE_TYPE,   // TARE5
        DECIMAL_TYPE,// KASR
        
        // --- CONSTRAINTS / INDEX ---
        INDEX,       // FEHRIS
        PRIMARY_KEY, // ASASI / MOFTA7
        ON,          // 3ALA

        // --- GROUP BY & AGGREGATES ---
        GROUP,       // MAGMO3A / GROUP
        BY,          // B / BY
        HAVING,      // ETHA / HAVING
        COUNT,       // 3ADD / COUNT
        SUM,         // MAG3MO3 / SUM
        AVG,         // MOTO3ASET / AVG
        MIN_AGG,     // ASGAR / MIN
        MAX_AGG,     // AKBAR / MAX
        
        // --- ORDER BY ---
        ORDER,       // RATEB / ORDER
        ASC,         // TASE3DI / ASC
        DESC,        // TANAZOLI / DESC
        
        // --- LIMIT / OFFSET ---
        LIMIT,       // 7ADD / LIMIT
        OFFSET,      // EBDA2MEN / OFFSET
        
        // --- DISTINCT ---
        DISTINCT,    // MOTA3MEZ / DISTINCT
        ALL,         // KOL / ALL
        
        // --- JOINS ---
        JOIN,        // ENTEDAH / JOIN
        INNER,       // DA5ELY / INNER
        LEFT,        // SHMAL / LEFT
        RIGHT,       // YAMEN / RIGHT
        OUTER,       // 5AREGY / OUTER
        CROSS,       // TAQATE3 / CROSS
        
        // --- FOREIGN KEYS ---
        FOREIGN,     // 5AREGY / FOREIGN
        KEY,         // MOFTA7 / KEY
        REFERENCES,  // YOSHEER / REFERENCES
        CASCADE,     // TATABE3 / CASCADE
        RESTRICT,    // MANE3 / RESTRICT
        SET,         // 5ALY / SET
        NO,          // LA / NO
        ACTION,      // E3RA2 / ACTION
        
        // --- CONSTRAINTS ---
        NULL_LIT,    // FADY / NULL
        NOT,         // MESH / NOT
        DEFAULT_KW,  // EFRADY / DEFAULT
        UNIQUE,      // WAHED / UNIQUE
        CHECK,       //فحص / CHECK
        AUTO_INCREMENT, // TAZAYED / AUTO_INCREMENT

        // --- LITERALS ---
        DECIMAL_LITERAL,
        TRUE_LIT,    // AH
        FALSE_LIT,   // LA
        IDENTIFIER,  // names
        NUMBER,      // 123
        STRING_LIT,  // 'text'

        // --- TRANSACTIONS ---
        BEGIN_TXN,   // 2EBDA2
        ROLLBACK,    // 2ERGA3
        COMMIT,      // 2AKED

        // --- OPERATORS ---
        AND,         // WE
        OR,          // AW
        IN_OP,       // FE
        
        // --- SYMBOLS ---
        COMMA,       // ,
        L_PAREN,     // (
        R_PAREN,     // )
        SEMICOLON,   // ;
        EQUALS,      // =
        STAR,        // *
        GT,          // >
        LT,          // <
        EOF_TOKEN,   // End
        INVALID      // Error
    };

    struct Token {
        TokenType type;
        std::string text;
    };

} // namespace francodb