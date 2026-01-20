#include <iostream>
#include <memory>
#include <string>
#include <filesystem>

#include "storage/disk/disk_manager.h"
#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "parser/parser.h"
#include "execution/execution_engine.h"
#include "common/auth_manager.h"

using namespace francodb;

static void RunQuery(ExecutionEngine &engine, const std::string &sql) {
    std::cout << "SQL: " << sql << std::endl;
    
    // 1. Lex & Parse
    Lexer lexer(sql);
    Parser parser(std::move(lexer));
    auto stmt = parser.ParseQuery();
    
    // 2. Execute
    engine.Execute(stmt.get(), nullptr);
}

void TestExecutionEngine() {
    std::string db_file = "francodb.francodb";
    std::string meta_file = db_file + ".meta";
    
    // Remove any previous instance of the database files
    if (std::filesystem::exists(db_file)) {
        std::filesystem::remove(db_file);
    }
    if (std::filesystem::exists(meta_file)) {
        std::filesystem::remove(meta_file);
    }
    
    // 1. Setup Storage Engine
    auto *disk_manager = new DiskManager(db_file);
    auto *bpm = new BufferPoolManager(50, disk_manager); // 50 pages memory
    auto *catalog = new Catalog(bpm);
    auto *auth_manager = new AuthManager(bpm, catalog);
    
    // 2. Start Execution Engine
    ExecutionEngine engine(bpm, catalog, auth_manager);

    try {
        std::cout << "--- STARTING FRANCO DB ENGINE ---" << std::endl;

        // A. CREATE TABLE with PRIMARY KEY
        // 2E3MEL GADWAL users (id RAKAM ASASI, name GOMLA, points KASR);
        RunQuery(engine, "2E3MEL GADWAL users (id RAKAM ASASI, name GOMLA, points KASR);");

        // B. INSERT DATA
        // EMLA GOWA users ELKEYAM (1, 'Ahmed', 95.5);
        RunQuery(engine, "EMLA GOWA users ELKEYAM (1, 'Ahmed', 95.5);");
        
        // EMLA GOWA users ELKEYAM (2, 'Sara', 80.0);
        RunQuery(engine, "EMLA GOWA users ELKEYAM (2, 'Sara', 80.0);");
        
        // EMLA GOWA users ELKEYAM (3, 'Ali', 50.5);
        RunQuery(engine, "EMLA GOWA users ELKEYAM (3, 'Ali', 50.5);");

        // C. SELECT (Read Everything)
        // 2E5TAR * MEN users;
        std::cout << "\n[TEST] Selecting ALL users..." << std::endl;
        RunQuery(engine, "2E5TAR * MEN users;");

        // D. SELECT WITH FILTER (The Real Logic Test)
        // 2E5TAR * MEN users LAMA points = 95.5 WE id = 1;
        std::cout << "\n[TEST] Selecting Ahmed (points=95.5 AND id=1)..." << std::endl;
        RunQuery(engine, "2E5TAR * MEN users LAMA points = 95.5 WE id = 1;");
        
        // E. UPDATE
        // 3ADEL GOWA users 5ALY points = 100.0 LAMA name = 'Ali';
        std::cout << "\n[TEST] Updating Ali's points to 100.0..." << std::endl;
        RunQuery(engine, "3ADEL GOWA users 5ALY points = 100.0 LAMA name = 'Ali';");

        // Verify Update
        RunQuery(engine, "2E5TAR * MEN users LAMA name = 'Ali';");

        // F. DELETE
        // 2EMSA7 MEN users LAMA id = 2;
        std::cout << "\n[TEST] Deleting Sara (id=2)..." << std::endl;
        RunQuery(engine, "2EMSA7 MEN users LAMA id = 2;");

        // Verify Delete
        std::cout << "\n[TEST] Selecting ALL (Should be Ahmed and Ali)..." << std::endl;
        RunQuery(engine, "2E5TAR * MEN users;");

        // G. DROP
        // 2EMSA7 GADWAL users;
        std::cout << "\n[TEST] Dropping Table..." << std::endl;
        RunQuery(engine, "2EMSA7 GADWAL users;");

    } catch (const Exception &e) {
        std::cerr << "CRITICAL ERROR: " << e.what() << std::endl;
        // Cleanup on error
        delete auth_manager;
        delete catalog;
        delete bpm;
        delete disk_manager;
        if (std::filesystem::exists(db_file)) {
            std::filesystem::remove(db_file);
        }
        if (std::filesystem::exists(meta_file)) {
            std::filesystem::remove(meta_file);
        }
        throw;
    }

    // Cleanup
    delete auth_manager;
    delete catalog;
    delete bpm;
    delete disk_manager;
    
    // Remove database files to avoid conflicts with other tests
    if (std::filesystem::exists(db_file)) {
        std::filesystem::remove(db_file);
    }
    if (std::filesystem::exists(meta_file)) {
        std::filesystem::remove(meta_file);
    }
}



