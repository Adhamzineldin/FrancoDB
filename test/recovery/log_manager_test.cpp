#include <iostream>
#include <filesystem>
#include <thread>
#include "recovery/log_manager.h"
#include "recovery/recovery_manager.h" // Add this
#include "recovery/log_record.h"
#include "common/value.h"

using namespace francodb;

void TestRecovery() {
    std::string log_file = "recovery_test.log";
    if (std::filesystem::exists(log_file)) std::filesystem::remove(log_file);

    // --- PHASE 1: SIMULATE CRASH ---
    std::cout << "[1/2] Writing Logs to Disk..." << std::endl;
    {
        LogManager log_mgr(log_file);
        
        Value v1(TypeId::VARCHAR, "InitialData");
        LogRecord rec1(101, -1, LogRecordType::INSERT, "MyTable", v1);
        log_mgr.AppendLogRecord(rec1);

        Value vOld(TypeId::VARCHAR, "InitialData");
        Value vNew(TypeId::VARCHAR, "RecoveredData");
        LogRecord rec2(101, 0, LogRecordType::UPDATE, "MyTable", vOld, vNew);
        log_mgr.AppendLogRecord(rec2);

        log_mgr.Flush(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } // CRASH! (Destructor closes file)

    // --- PHASE 2: RESTART & RECOVER ---
    std::cout << "\n[2/2] Restarting System & Running Recovery..." << std::endl;
    {
        // 1. Start Log Manager (Empty state)
        LogManager log_mgr(log_file);
        
        // 2. Start Recovery Manager
        RecoveryManager recovery(&log_mgr);
        
        // 3. Run ARIES
        recovery.ARIES();
    }
}

int main() {
    TestRecovery();
    return 0;
}