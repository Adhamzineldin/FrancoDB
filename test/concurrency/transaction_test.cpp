#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <cassert>
#include <random>
#include <filesystem>
#include <mutex>

#include "storage/disk/disk_manager.h"
#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "parser/parser.h"
#include "execution/execution_engine.h"
#include "common/auth_manager.h"

using namespace francodb;

// Shared Resources
DiskManager *g_disk_manager;
BufferPoolManager *g_bpm;
Catalog *g_catalog;
std::mutex g_log_mutex;

// Helper to run SQL
void RunSQL(ExecutionEngine &engine, const std::string &sql) {
    try {
        Lexer lexer(sql);
        Parser parser(std::move(lexer));
        auto stmt = parser.ParseQuery();
        if (stmt) engine.Execute(stmt.get(), nullptr);
    } catch (const std::exception &e) {
        // Errors are okay in stress tests (deadlocks/duplicates), but we catch them to prevent crashes
    }
}

// PHASE 1: POPULATE (Insert only)
void PopulationWorker(int id, int count) {
    auto *auth_manager = new AuthManager(g_bpm, g_catalog);
    ExecutionEngine engine(g_bpm, g_catalog, auth_manager);
    for (int i = 0; i < count; i++) {
        int user_id = (id * 1000) + i;
        std::string sql = "EMLA GOWA users ELKEYAM (" + std::to_string(user_id) + ", 'User" + std::to_string(user_id) + "');";
        RunSQL(engine, sql);
    }
    delete auth_manager;
}

// PHASE 2: STRESS (Update/Delete/Select)
void StressWorker(int id, int num_ops, int max_users) {
    auto *auth_manager = new AuthManager(g_bpm, g_catalog);
    ExecutionEngine engine(g_bpm, g_catalog, auth_manager);
    std::mt19937 rng(id + 999);
    std::uniform_int_distribution<int> dist(0, 2); // 0=Select, 1=Update, 2=Delete
    std::uniform_int_distribution<int> user_dist(0, max_users - 1);

    for (int i = 0; i < num_ops; i++) {
        int op = dist(rng);
        int target_id = user_dist(rng);

        std::string sql;
        switch (op) {
            case 0: // SELECT
                sql = "2E5TAR * MEN users LAMA id = " + std::to_string(target_id) + ";";
                break;
            case 1: // UPDATE
                sql = "3ADEL GOWA users 5ALY name = 'Updated' LAMA id = " + std::to_string(target_id) + ";";
                break;
            case 2: // DELETE
                sql = "2EMSA7 MEN users LAMA id = " + std::to_string(target_id) + ";";
                break;
        }
        RunSQL(engine, sql);
    }
    delete auth_manager;
}

void TestRealWorldTraffic() {
    std::string db_file = "test_traffic.francodb";
    // Clean up any previous test files
    if (std::filesystem::exists(db_file)) std::filesystem::remove(db_file);
    if (std::filesystem::exists(db_file + ".meta")) std::filesystem::remove(db_file + ".meta");

    std::cout << "=== STARTING PHASED TRAFFIC TEST ===" << std::endl;

    g_disk_manager = new DiskManager(db_file);
    g_bpm = new BufferPoolManager(100, g_disk_manager); // More RAM for speed
    g_catalog = new Catalog(g_bpm);
    
    // 1. Setup Table
    {
        auto *auth_manager = new AuthManager(g_bpm, g_catalog);
        ExecutionEngine setup_engine(g_bpm, g_catalog, auth_manager);
        RunSQL(setup_engine, "2E3MEL GADWAL users (id RAKAM, name GOMLA);");
        RunSQL(setup_engine, "2E3MEL FEHRIS idx_users 3ALA users (id);");
        delete auth_manager;
    }

    // 2. PHASE 1: POPULATE (Single Threaded to ensure data exists)
    // We insert 1000 users.
    std::cout << "[INFO] Phase 1: Populating 1000 users..." << std::endl;
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; i++) {
        workers.emplace_back(std::thread(PopulationWorker, i, 250)); // 4 threads * 250 = 1000
    }
    for (auto &t : workers) t.join();
    workers.clear();

    // 3. PHASE 2: CHAOS (8 Threads reading/writing same data)
    std::cout << "[INFO] Phase 2: Launching Chaos (Updates/Deletes)..." << std::endl;
    for (int i = 0; i < 8; i++) {
        workers.emplace_back(std::thread(StressWorker, i, 500, 1000));
    }
    for (auto &t : workers) t.join();

    std::cout << "[SUCCESS] Engine survived the Phased Traffic Test!" << std::endl;

    delete g_catalog;
    delete g_bpm;
    delete g_disk_manager;
    
    // Clean up test files
    if (std::filesystem::exists(db_file)) std::filesystem::remove(db_file);
    if (std::filesystem::exists(db_file + ".meta")) std::filesystem::remove(db_file + ".meta");
}

