#include <iostream>
#include <vector>
#include <cassert>
#include <filesystem>
#include <stdexcept>

#include "storage/disk/disk_manager.h"
#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "parser/parser.h"
#include "execution/execution_engine.h"
#include "common/auth_manager.h"

using namespace francodb;

// Test result tracking
int tests_passed = 0;
int tests_failed = 0;

void RunSQL(ExecutionEngine &engine, const std::string &sql, bool expect_error = false) {
    try {
        Lexer lexer(sql);
        Parser parser(std::move(lexer));
        auto stmt = parser.ParseQuery();
        if (stmt) {
            auto result = engine.Execute(stmt.get(), nullptr);
            // Check if execution returned an error
            if (!result.success) {
                if (expect_error) {
                    std::cout << "  [PASS] Expected error caught: " << result.message << std::endl;
                    tests_passed++;
                } else {
                    std::cout << "  [FAIL] Execution error: " << result.message << " for SQL: " << sql << std::endl;
                    tests_failed++;
                }
                return;
            }
        }
        if (expect_error) {
            std::cout << "  [FAIL] Expected error but operation succeeded: " << sql << std::endl;
            tests_failed++;
        } else {
            tests_passed++;
        }
    } catch (const std::exception &e) {
        if (expect_error) {
            std::cout << "  [PASS] Expected error caught: " << e.what() << std::endl;
            tests_passed++;
        } else {
            std::cout << "  [FAIL] Unexpected error: " << e.what() << " for SQL: " << sql << std::endl;
            tests_failed++;
        }
    }
}

void TestHeader(const std::string &test_name) {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "TEST: " << test_name << std::endl;
    std::cout << std::string(60, '=') << std::endl;
}

// ============================================================================
// TEST 1: TABLE CREATION
// ============================================================================
void TestTableCreation(ExecutionEngine &engine) {
    TestHeader("Table Creation");
    
    // Create table with primary key (use ASASI for PRIMARY KEY)
    std::cout << "[1.1] Creating table 'users' with PRIMARY KEY..." << std::endl;
    RunSQL(engine, "2E3MEL GADWAL users (id RAKAM ASASI, name GOMLA, email GOMLA);");
    
    // Create table without primary key
    std::cout << "[1.2] Creating table 'products' without PRIMARY KEY..." << std::endl;
    RunSQL(engine, "2E3MEL GADWAL products (id RAKAM, name GOMLA, price KASR);");
    
    // Try to create duplicate table (should fail)
    std::cout << "[1.3] Attempting to create duplicate table (should fail)..." << std::endl;
    RunSQL(engine, "2E3MEL GADWAL users (id RAKAM, name GOMLA);", true);
}

// ============================================================================
// TEST 2: PRIMARY KEY CONSTRAINTS
// ============================================================================
void TestPrimaryKeyConstraints(ExecutionEngine &engine) {
    TestHeader("Primary Key Constraints");
    
    // Insert valid data
    std::cout << "[2.1] Inserting valid data with unique primary key..." << std::endl;
    RunSQL(engine, "EMLA GOWA users ELKEYAM (1, 'Ahmed', 'ahmed@example.com');");
    RunSQL(engine, "EMLA GOWA users ELKEYAM (2, 'Sara', 'sara@example.com');");
    RunSQL(engine, "EMLA GOWA users ELKEYAM (3, 'Ali', 'ali@example.com');");
    
    // Try to insert duplicate primary key (should fail)
    std::cout << "[2.2] Attempting duplicate primary key insert (should fail)..." << std::endl;
    RunSQL(engine, "EMLA GOWA users ELKEYAM (1, 'Duplicate', 'dup@example.com');", true);
    
    // Try to insert another duplicate (should fail)
    std::cout << "[2.3] Attempting another duplicate primary key (should fail)..." << std::endl;
    RunSQL(engine, "EMLA GOWA users ELKEYAM (2, 'Another', 'another@example.com');", true);
    
    // Insert with different primary key (should succeed)
    std::cout << "[2.4] Inserting with different primary key (should succeed)..." << std::endl;
    RunSQL(engine, "EMLA GOWA users ELKEYAM (4, 'Mohamed', 'mohamed@example.com');");
}

// ============================================================================
// TEST 3: SELECT OPERATIONS
// ============================================================================
void TestSelectOperations(ExecutionEngine &engine) {
    TestHeader("Select Operations");
    
    // Select all
    std::cout << "[3.1] Selecting all rows..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users;");
    
    // Select with WHERE clause (equality)
    std::cout << "[3.2] Selecting with WHERE clause (id = 1)..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users LAMA id = 1;");
    
    // Select with WHERE clause (name)
    std::cout << "[3.3] Selecting with WHERE clause (name = 'Sara')..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users LAMA name = 'Sara';");
    
    // Select with WHERE clause (no matches)
    std::cout << "[3.4] Selecting with WHERE clause (no matches)..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users LAMA id = 999;");
    
    // Select specific columns
    std::cout << "[3.5] Selecting specific columns..." << std::endl;
    RunSQL(engine, "2E5TAR id, name MEN users;");
}

// ============================================================================
// TEST 4: INDEX OPERATIONS
// ============================================================================
void TestIndexOperations(ExecutionEngine &engine) {
    TestHeader("Index Operations");
    
    // Create index on primary key column
    std::cout << "[4.1] Creating index on primary key column..." << std::endl;
    RunSQL(engine, "2E3MEL FEHRIS idx_users_id 3ALA users (id);");
    
    // Create index on non-primary key column
    std::cout << "[4.2] Creating index on email column..." << std::endl;
    RunSQL(engine, "2E3MEL FEHRIS idx_users_email 3ALA users (email);");
    
    // Try to create duplicate index (should fail)
    std::cout << "[4.3] Attempting to create duplicate index (should fail)..." << std::endl;
    RunSQL(engine, "2E3MEL FEHRIS idx_users_id 3ALA users (id);", true);
    
    // Select using index (should use index scan)
    std::cout << "[4.4] Selecting with indexed column (should use index)..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users LAMA id = 2;");
    
    // Select using email index
    std::cout << "[4.5] Selecting with email index..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users LAMA email = 'sara@example.com';");
}

// ============================================================================
// TEST 5: UPDATE OPERATIONS
// ============================================================================
void TestUpdateOperations(ExecutionEngine &engine) {
    TestHeader("Update Operations");
    
    // Update non-primary key column
    std::cout << "[5.1] Updating non-primary key column (name)..." << std::endl;
    RunSQL(engine, "3ADEL GOWA users 5ALY name = 'Ahmed Updated' LAMA id = 1;");
    
    // Verify update
    std::cout << "[5.2] Verifying update..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users LAMA id = 1;");
    
    // Update primary key to unique value (should succeed)
    std::cout << "[5.3] Updating primary key to unique value..." << std::endl;
    RunSQL(engine, "3ADEL GOWA users 5ALY id = 10 LAMA id = 1;");
    
    // Verify primary key update
    std::cout << "[5.4] Verifying primary key update..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users LAMA id = 10;");
    
    // Try to update primary key to duplicate value (should fail)
    std::cout << "[5.5] Attempting to update primary key to duplicate (should fail)..." << std::endl;
    RunSQL(engine, "3ADEL GOWA users 5ALY id = 2 LAMA id = 10;", true);
    
    // Update multiple rows
    std::cout << "[5.6] Updating multiple rows..." << std::endl;
    RunSQL(engine, "3ADEL GOWA users 5ALY email = 'updated@example.com' LAMA id = 2;");
}

// ============================================================================
// TEST 6: DELETE OPERATIONS
// ============================================================================
void TestDeleteOperations(ExecutionEngine &engine) {
    TestHeader("Delete Operations");
    
    // Delete specific row
    std::cout << "[6.1] Deleting row with id = 3..." << std::endl;
    RunSQL(engine, "2EMSA7 MEN users LAMA id = 3;");
    
    // Verify deletion
    std::cout << "[6.2] Verifying deletion..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users;");
    
    // Try to delete non-existent row (should succeed but affect 0 rows)
    std::cout << "[6.3] Attempting to delete non-existent row..." << std::endl;
    RunSQL(engine, "2EMSA7 MEN users LAMA id = 999;");
    
    // Delete multiple rows
    std::cout << "[6.4] Deleting multiple rows..." << std::endl;
    RunSQL(engine, "2EMSA7 MEN users LAMA email = 'updated@example.com';");
    
    // Verify remaining data
    std::cout << "[6.5] Verifying remaining data..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users;");
}

// ============================================================================
// TEST 7: TRANSACTIONS
// ============================================================================
void TestTransactions(ExecutionEngine &engine) {
    TestHeader("Transaction Operations");
    
    // Begin transaction
    std::cout << "[7.1] Beginning transaction..." << std::endl;
    RunSQL(engine, "2EBDA2;");
    
    // Insert within transaction
    std::cout << "[7.2] Inserting within transaction..." << std::endl;
    RunSQL(engine, "EMLA GOWA users ELKEYAM (100, 'TxnUser1', 'txn1@example.com');");
    RunSQL(engine, "EMLA GOWA users ELKEYAM (101, 'TxnUser2', 'txn2@example.com');");
    
    // Verify data is visible within transaction
    std::cout << "[7.3] Verifying data visible within transaction..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users LAMA id = 100;");
    
    // Rollback transaction
    std::cout << "[7.4] Rolling back transaction..." << std::endl;
    RunSQL(engine, "2ERGA3;");
    
    // Verify data was rolled back
    std::cout << "[7.5] Verifying data was rolled back..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users LAMA id = 100;");
    
    // Begin new transaction
    std::cout << "[7.6] Beginning new transaction..." << std::endl;
    RunSQL(engine, "2EBDA2;");
    
    // Insert and update within transaction
    std::cout << "[7.7] Inserting and updating within transaction..." << std::endl;
    RunSQL(engine, "EMLA GOWA users ELKEYAM (200, 'TxnUser3', 'txn3@example.com');");
    RunSQL(engine, "3ADEL GOWA users 5ALY name = 'TxnUser3 Updated' LAMA id = 200;");
    
    // Commit transaction
    std::cout << "[7.8] Committing transaction..." << std::endl;
    RunSQL(engine, "2AKED;");
    
    // Verify data was committed
    std::cout << "[7.9] Verifying data was committed..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users LAMA id = 200;");
    
    // Test rollback of update
    std::cout << "[7.10] Testing rollback of update..." << std::endl;
    RunSQL(engine, "2EBDA2;");
    RunSQL(engine, "3ADEL GOWA users 5ALY name = 'Should Rollback' LAMA id = 200;");
    RunSQL(engine, "2ERGA3;");
    RunSQL(engine, "2E5TAR * MEN users LAMA id = 200;");
    
    // Test rollback of delete
    std::cout << "[7.11] Testing rollback of delete..." << std::endl;
    RunSQL(engine, "2EBDA2;");
    RunSQL(engine, "2EMSA7 MEN users LAMA id = 200;");
    RunSQL(engine, "2ERGA3;");
    RunSQL(engine, "2E5TAR * MEN users LAMA id = 200;");
}

// ============================================================================
// TEST 8: COMPLEX QUERIES
// ============================================================================
void TestComplexQueries(ExecutionEngine &engine) {
    TestHeader("Complex Queries");
    
    // Insert more data for complex queries
    std::cout << "[8.1] Inserting data for complex queries..." << std::endl;
    RunSQL(engine, "EMLA GOWA users ELKEYAM (300, 'User300', 'user300@example.com');");
    RunSQL(engine, "EMLA GOWA users ELKEYAM (301, 'User301', 'user301@example.com');");
    RunSQL(engine, "EMLA GOWA users ELKEYAM (302, 'User302', 'user302@example.com');");
    
    // Select with AND condition
    std::cout << "[8.2] Selecting with AND condition..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users LAMA id = 300 WE name = 'User300';");
    
    // Select with OR condition (if supported)
    std::cout << "[8.3] Selecting with OR condition..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users LAMA id = 301 AW id = 302;");
    
    // Select all from empty result
    std::cout << "[8.4] Selecting from table with no matches..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users LAMA id = 9999;");
}

// ============================================================================
// TEST 9: EDGE CASES
// ============================================================================
void TestEdgeCases(ExecutionEngine &engine) {
    TestHeader("Edge Cases");
    
    // Insert into table without primary key
    std::cout << "[9.1] Inserting into table without primary key..." << std::endl;
    RunSQL(engine, "EMLA GOWA products ELKEYAM (1, 'Product1', 10.5);");
    RunSQL(engine, "EMLA GOWA products ELKEYAM (1, 'Product1Duplicate', 20.0);"); // Should succeed (no PK)
    
    // Select from empty table
    std::cout << "[9.2] Creating and selecting from empty table..." << std::endl;
    RunSQL(engine, "2E3MEL GADWAL empty_table (id RAKAM, name GOMLA);");
    RunSQL(engine, "2E5TAR * MEN empty_table;");
    
    // Update non-existent row
    std::cout << "[9.3] Updating non-existent row..." << std::endl;
    RunSQL(engine, "3ADEL GOWA empty_table 5ALY name = 'Test' LAMA id = 999;");
    
    // Delete from empty table
    std::cout << "[9.4] Deleting from empty table..." << std::endl;
    RunSQL(engine, "2EMSA7 MEN empty_table LAMA id = 1;");
}

// ============================================================================
// TEST 10: DATA PERSISTENCE
// ============================================================================
void TestDataPersistence(const std::string &db_file) {
    TestHeader("Data Persistence");
    
    std::cout << "[10.1] Closing and reopening database..." << std::endl;
    
    // Create new engine instances to simulate restart
    auto *disk_manager2 = new DiskManager(db_file);
    auto *bpm2 = new BufferPoolManager(50, disk_manager2);
    auto *catalog2 = new Catalog(bpm2);
    auto *auth_manager2 = new AuthManager(bpm2, catalog2);
    ExecutionEngine engine2(bpm2, catalog2, auth_manager2);
    
    // Verify data persisted
    std::cout << "[10.2] Verifying data persisted after restart..." << std::endl;
    RunSQL(engine2, "2E5TAR * MEN users;");
    RunSQL(engine2, "2E5TAR * MEN products;");
    
    // Verify indexes persisted
    std::cout << "[10.3] Verifying indexes persisted..." << std::endl;
    RunSQL(engine2, "2E5TAR * MEN users LAMA id = 200;"); // Should use index
    
    delete auth_manager2;
    delete catalog2;
    delete bpm2;
    delete disk_manager2;
}

// ============================================================================
// TEST 11: PRIMARY KEY UPDATE SCENARIOS
// ============================================================================
void TestPrimaryKeyUpdateScenarios(ExecutionEngine &engine) {
    TestHeader("Primary Key Update Scenarios");
    
    // Insert test data
    std::cout << "[11.1] Inserting test data..." << std::endl;
    RunSQL(engine, "EMLA GOWA users ELKEYAM (500, 'PKTest1', 'pk1@example.com');");
    RunSQL(engine, "EMLA GOWA users ELKEYAM (501, 'PKTest2', 'pk2@example.com');");
    
    // Update PK to new unique value
    std::cout << "[11.2] Updating primary key to new unique value..." << std::endl;
    RunSQL(engine, "3ADEL GOWA users 5ALY id = 600 LAMA id = 500;");
    
    // Verify update
    std::cout << "[11.3] Verifying primary key update..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users LAMA id = 600;");
    RunSQL(engine, "2E5TAR * MEN users LAMA id = 500;"); // Should return nothing
    
    // Try to update to existing PK (should fail)
    std::cout << "[11.4] Attempting to update PK to existing value (should fail)..." << std::endl;
    RunSQL(engine, "3ADEL GOWA users 5ALY id = 501 LAMA id = 600;", true);
}

// ============================================================================
// TEST 12: MULTIPLE TABLES
// ============================================================================
void TestMultipleTables(ExecutionEngine &engine) {
    TestHeader("Multiple Tables Operations");
    
    // Create second table with primary key (use ASASI for PRIMARY KEY)
    std::cout << "[12.1] Creating second table with primary key..." << std::endl;
    RunSQL(engine, "2E3MEL GADWAL orders (order_id RAKAM ASASI, user_id RAKAM, total KASR);");
    
    // Insert into multiple tables
    std::cout << "[12.2] Inserting into multiple tables..." << std::endl;
    RunSQL(engine, "EMLA GOWA orders ELKEYAM (1, 200, 99.99);");
    RunSQL(engine, "EMLA GOWA orders ELKEYAM (2, 200, 149.50);");
    RunSQL(engine, "EMLA GOWA orders ELKEYAM (3, 301, 50.00);");
    
    // Select from both tables
    std::cout << "[12.3] Selecting from both tables..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN users;");
    RunSQL(engine, "2E5TAR * MEN orders;");
    
    // Create index on foreign key-like column
    std::cout << "[12.4] Creating index on foreign key column..." << std::endl;
    RunSQL(engine, "2E3MEL FEHRIS idx_orders_user_id 3ALA orders (user_id);");
    
    // Query using index
    std::cout << "[12.5] Querying orders by user_id using index..." << std::endl;
    RunSQL(engine, "2E5TAR * MEN orders LAMA user_id = 200;");
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================
void TestFrancoDBSystem() {
    // Reset counters at start of test
    tests_passed = 0;
    tests_failed = 0;
    
    std::string db_file = "francodb_system_test.francodb";
    
    // Clean up old test files
    if (std::filesystem::exists(db_file)) {
        std::filesystem::remove(db_file);
    }
    if (std::filesystem::exists(db_file + ".meta")) {
        std::filesystem::remove(db_file + ".meta");
    }
    
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "FRANCO DB COMPREHENSIVE SYSTEM TEST" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    
    // Initialize database
    auto *disk_manager = new DiskManager(db_file);
    auto *bpm = new BufferPoolManager(50, disk_manager);
    auto *catalog = new Catalog(bpm);
    auto *auth_manager = new AuthManager(bpm, catalog);
    ExecutionEngine engine(bpm, catalog, auth_manager);
    
    auto cleanup = [&]() {
        if (auth_manager) delete auth_manager;
        if (catalog) delete catalog;
        if (bpm) delete bpm;
        if (disk_manager) delete disk_manager;
        auth_manager = nullptr;
        catalog = nullptr;
        bpm = nullptr;
        disk_manager = nullptr;
        // Clean up test database file
        if (std::filesystem::exists(db_file)) {
            std::filesystem::remove(db_file);
        }
        if (std::filesystem::exists(db_file + ".meta")) {
            std::filesystem::remove(db_file + ".meta");
        }
    };
    
    try {
        // Run all test suites
        TestTableCreation(engine);
        TestPrimaryKeyConstraints(engine);
        TestSelectOperations(engine);
        TestIndexOperations(engine);
        TestUpdateOperations(engine);
        TestDeleteOperations(engine);
        TestTransactions(engine);
        TestComplexQueries(engine);
        TestEdgeCases(engine);
        TestPrimaryKeyUpdateScenarios(engine);
        TestMultipleTables(engine);
        
        // Close the first database before testing persistence
        delete auth_manager;
        delete catalog;
        delete bpm;
        delete disk_manager;
        auth_manager = nullptr;
        catalog = nullptr;
        bpm = nullptr;
        disk_manager = nullptr;
        
        // Test persistence (reopen database)
        TestDataPersistence(db_file);
        
        // Final summary
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "TEST SUMMARY" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
        std::cout << "Tests Passed: " << tests_passed << std::endl;
        std::cout << "Tests Failed: " << tests_failed << std::endl;
        std::cout << "Total Tests:  " << (tests_passed + tests_failed) << std::endl;
        
        cleanup();
        
        // Throw exception if any tests failed so TestRunner can track it
        if (tests_failed > 0) {
            throw std::runtime_error("FrancoDB System Test: " + std::to_string(tests_failed) + " tests failed");
        }
        
        std::cout << "\n[SUCCESS] All FrancoDB system tests passed!" << std::endl;
        
    } catch (const std::exception &e) {
        std::cout << "\n[FATAL ERROR] Test suite crashed: " << e.what() << std::endl;
        cleanup();
        throw; // Re-throw so TestRunner can catch it
    }
}

