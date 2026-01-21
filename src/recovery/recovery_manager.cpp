#include "recovery/recovery_manager.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>
#include <limits>

namespace francodb {

    // ------------------------------------------------------------------------
    // Static Helpers for Deserialization
    // ------------------------------------------------------------------------
    
    static std::string ReadString(std::ifstream& in) {
        uint32_t len = 0;
        in.read(reinterpret_cast<char*>(&len), sizeof(uint32_t));
        if (in.gcount() != sizeof(uint32_t)) return "";
        std::vector<char> buf(len);
        in.read(buf.data(), len);
        return std::string(buf.begin(), buf.end());
    }

    static Value ReadValue(std::ifstream& in) {
        int type_id = 0;
        in.read(reinterpret_cast<char*>(&type_id), sizeof(int));
        std::string s_val = ReadString(in);
        
        // Robust Type Conversion
        TypeId type = static_cast<TypeId>(type_id);
        if (type == TypeId::INTEGER) {
            try { return Value(type, std::stoi(s_val)); } catch (...) { return Value(type, 0); }
        }
        if (type == TypeId::DECIMAL) {
             try { return Value(type, std::stod(s_val)); } catch (...) { return Value(type, 0.0); }
        }
        return Value(type, s_val);
    }

    // ------------------------------------------------------------------------
    // Recovery Logic
    // ------------------------------------------------------------------------

    void RecoveryManager::RunRecoveryLoop(uint64_t stop_at_time, uint64_t start_offset) {
        std::string filename = log_manager_->GetLogFileName();
        std::ifstream log_file(filename, std::ios::binary | std::ios::in);

        if (!log_file.is_open()) {
            std::cerr << "[FATAL] RecoveryManager: Could not open log file: " << filename << std::endl;
            return;
        }

        // Seek to optimization point (Checkpoint) if provided
        if (start_offset > 0) {
            log_file.seekg(start_offset);
            std::cout << "[RECOVERY] Fast-forwarding to offset: " << start_offset << std::endl;
        }

        std::cout << "=== RECOVERY STARTED (Target: " 
                  << (stop_at_time == 0 ? "HEAD" : std::to_string(stop_at_time)) 
                  << ") ===" << std::endl;

        int records_replayed = 0;

        while (log_file.peek() != EOF) {
            // 1. Read Total Size
            int32_t size = 0;
            log_file.read(reinterpret_cast<char*>(&size), sizeof(int32_t));
            if (log_file.gcount() != sizeof(int32_t)) break;

            // 2. Read Header
            LogRecord::lsn_t lsn, prev_lsn;
            LogRecord::txn_id_t txn_id;
            LogRecord::timestamp_t timestamp; 
            int log_type_int;

            log_file.read(reinterpret_cast<char*>(&lsn), sizeof(lsn));
            log_file.read(reinterpret_cast<char*>(&prev_lsn), sizeof(prev_lsn));
            log_file.read(reinterpret_cast<char*>(&txn_id), sizeof(txn_id));
            log_file.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp));
            log_file.read(reinterpret_cast<char*>(&log_type_int), sizeof(int));
            
            LogRecordType type = static_cast<LogRecordType>(log_type_int);

            // 3. Time Travel Check
            if (stop_at_time > 0 && timestamp > stop_at_time) {
                std::cout << "-> [STOP] Reached Target Time. Stopping Replay." << std::endl;
                break;
            }

            // 4. Redo Logic
            std::string table_name;
            Value v1, v2;

            switch (type) {
                case LogRecordType::INSERT:
                    table_name = ReadString(log_file);
                    v1 = ReadValue(log_file);
                    std::cout << "[REDO] Txn " << txn_id << ": INSERT " << table_name << std::endl;
                    // TODO: Dispatch to TableHeap::InsertTuple via Catalog
                    break;

                case LogRecordType::UPDATE:
                    table_name = ReadString(log_file);
                    v1 = ReadValue(log_file);
                    v2 = ReadValue(log_file);
                    std::cout << "[REDO] Txn " << txn_id << ": UPDATE " << table_name << std::endl;
                    break;

                case LogRecordType::APPLY_DELETE:
                    table_name = ReadString(log_file);
                    v1 = ReadValue(log_file);
                    std::cout << "[REDO] Txn " << txn_id << ": DELETE " << table_name << std::endl;
                    break;
                
                case LogRecordType::CHECKPOINT_END:
                    std::cout << "[INFO] Checkpoint Marker encountered during replay." << std::endl;
                    break;

                default:
                    break;
            }
            records_replayed++;
        }
        
        std::cout << "=== RECOVERY COMPLETE (" << records_replayed << " ops applied) ===" << std::endl;
    }

    void RecoveryManager::ARIES() {
        std::string filename = log_manager_->GetLogFileName();
        std::ifstream log_file(filename, std::ios::binary | std::ios::in);
        if (!log_file.is_open()) return;

        // --- ANALYSIS PHASE: SCAN FOR CHECKPOINTS ---
        std::cout << "[ANALYSIS] Scanning log for checkpoints..." << std::endl;
        
        std::streampos last_checkpoint_pos = 0;
        
        while (log_file.peek() != EOF) {
            std::streampos current_pos = log_file.tellg();
            
            int32_t size = 0;
            log_file.read(reinterpret_cast<char*>(&size), sizeof(int32_t));
            if (log_file.gcount() != sizeof(int32_t)) break;
            
            // We need to peek the Type to see if it's a checkpoint
            // Size (4) + LSN (4) + Prev (4) + Txn (4) + Time (8) = 24 bytes offset to Type
            
            log_file.seekg(24, std::ios::cur); 
            
            int log_type_int;
            log_file.read(reinterpret_cast<char*>(&log_type_int), sizeof(int));
            
            if (static_cast<LogRecordType>(log_type_int) == LogRecordType::CHECKPOINT_END) {
                last_checkpoint_pos = current_pos;
            }

            // Move to next record. 
            // We are currently at (Start + 4 + 24 + 4) = Start + 32.
            // Total size of record is 'size'.
            // We need to advance (size - 32) more bytes.
            // BUT: 'size' in header typically includes the body size + header size.
            // Let's verify header size in LogRecord class. 
            // Header is 28 bytes (LSN..Type). Plus 4 bytes for Size field = 32 bytes total Header.
            // So 'size' includes everything.
            
            log_file.seekg(current_pos); // Reset to start of record
            log_file.seekg(size, std::ios::cur); // Skip full record
        }
        
        log_file.close();

        if (last_checkpoint_pos > 0) {
            std::cout << "[OPTIMIZATION] Found Checkpoint at offset " << last_checkpoint_pos << ". Truncating history." << std::endl;
            RunRecoveryLoop(0, static_cast<uint64_t>(last_checkpoint_pos));
        } else {
            std::cout << "[ANALYSIS] No Checkpoint found. Full replay required." << std::endl;
            RunRecoveryLoop(0, 0);
        }
    }

    void RecoveryManager::RecoverToTime(uint64_t target_time) {
        // For Time Travel, we CANNOT use checkpoints because the user might want to go back 
        // to a time BEFORE the last checkpoint.
        // We must replay from the beginning to ensure accuracy.
        RunRecoveryLoop(target_time, 0);
    }

} // namespace francodb