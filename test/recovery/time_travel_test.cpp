#include <iostream>
#include <filesystem>
#include <thread>
#include <vector>
#include "recovery/log_manager.h"
#include "recovery/recovery_manager.h"

using namespace francodb;

void TestTimeTravel() {
    std::string log_file = "time_machine.log";
    if (std::filesystem::exists(log_file)) std::filesystem::remove(log_file);

    uint64_t good_time_stamp = 0;

    // --- PHASE 1: GENERATE HISTORY ---
    std::cout << "[1/3] Generating History..." << std::endl;
    {
        LogManager log_mgr(log_file);
        
        // Event 1: The "Good" State
        Value v1(TypeId::VARCHAR, "Money = 1000$");
        LogRecord rec1(101, -1, LogRecordType::INSERT, "Accounts", v1);
        log_mgr.AppendLogRecord(rec1);
        std::cout << "  -> Written: Money = 1000$ (Timestamp: " << rec1.GetTimestamp() << ")" << std::endl;
        
        // CAPTURE THIS TIME! We want to return here.
        good_time_stamp = rec1.GetTimestamp();

        // Wait to ensure timestamps differ
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Event 2: The "Bad" State (Hack or Mistake)
        Value v2(TypeId::VARCHAR, "Money = 0$");
        LogRecord rec2(102, 0, LogRecordType::UPDATE, "Accounts", v1, v2);
        log_mgr.AppendLogRecord(rec2);
        std::cout << "  -> Written: Money = 0$    (Timestamp: " << rec2.GetTimestamp() << ")" << std::endl;

        log_mgr.Flush(true);
    } 

    // --- PHASE 2: STANDARD RECOVERY (Current State) ---
    std::cout << "\n[2/3] Testing Standard Recovery (Should see 0$)..." << std::endl;
    {
        LogManager log_mgr(log_file);
        RecoveryManager recovery(&log_mgr, nullptr, nullptr, nullptr);
        recovery.ARIES(); // Recover Everything
    }

    // --- PHASE 3: TIME TRAVEL RECOVERY ---
    std::cout << "\n[3/3] ACTIVATING TIME MACHINE (Target: " << good_time_stamp << ")..." << std::endl;
    {
        LogManager log_mgr(log_file);
        RecoveryManager recovery(&log_mgr, nullptr, nullptr, nullptr);
        
        // This is the magic line
        recovery.RecoverToTime(good_time_stamp);
    }
}

// int main() {
//     TestTimeTravel();
//     return 0;
// }