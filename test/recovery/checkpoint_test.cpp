#include <iostream>
#include <filesystem>
#include <thread>
#include "recovery/log_manager.h"
#include "recovery/recovery_manager.h"
#include "recovery/checkpoint_manager.h"
#include "buffer/buffer_pool_manager.h"
#include "storage/disk/disk_manager.h"

using namespace francodb;

void TestCheckpoint() {
    std::string db_file = "test_chk.db";
    std::string log_file = "test_chk.log";
    
    // Cleanup
    if (std::filesystem::exists(db_file)) std::filesystem::remove(db_file);
    if (std::filesystem::exists(log_file)) std::filesystem::remove(log_file);

    // --- PHASE 1: PRE-CHECKPOINT ---
    std::cout << "\n[1/4] Generating Pre-Checkpoint Data..." << std::endl;
    {
        DiskManager dm(db_file);
        BufferPoolManager bpm(10, &dm);
        LogManager log_mgr(log_file);
        CheckpointManager cp_mgr(&bpm, &log_mgr);

        // 1. Write Log "A"
        Value valA(TypeId::VARCHAR, "Data_Before_Checkpoint");
        LogRecord recA(100, -1, LogRecordType::INSERT, "TableA", valA);
        log_mgr.AppendLogRecord(recA);
        
        // 2. TRIGGER CHECKPOINT
        // This flushes "Data_Before_Checkpoint" to the DB file and marks the Log.
        cp_mgr.BeginCheckpoint();

        // 3. Write Log "B"
        Value valB(TypeId::VARCHAR, "Data_After_Checkpoint");
        LogRecord recB(101, 0, LogRecordType::INSERT, "TableA", valB);
        log_mgr.AppendLogRecord(recB);
        
        // Flush log B to ensure it exists for recovery
        log_mgr.Flush(true);
    } 

    // --- PHASE 2: RECOVERY ---
    std::cout << "\n[2/4] Restarting System..." << std::endl;
    {
        DiskManager dm(db_file);
        BufferPoolManager bpm(10, &dm);
        LogManager log_mgr(log_file);
        CheckpointManager cp_mgr(&bpm, &log_mgr);
        RecoveryManager recovery(&log_mgr, nullptr, &bpm, &cp_mgr);
        
        // RUN ARIES
        // Expected Behavior: 
        // 1. Scan finds Checkpoint.
        // 2. Skips "Data_Before_Checkpoint" log.
        // 3. Replays "Data_After_Checkpoint" log.
        recovery.ARIES();
    }
}

// int main() {
//     TestCheckpoint();
//     return 0;
// }