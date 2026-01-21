#include "recovery/recovery_manager.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>

namespace francodb {

    // Helper: Read a string from binary stream [Len][Chars]
    std::string ReadString(std::ifstream& in) {
        uint32_t len = 0;
        in.read(reinterpret_cast<char*>(&len), sizeof(uint32_t));
        if (in.gcount() != sizeof(uint32_t)) return "";
        
        std::vector<char> buf(len);
        in.read(buf.data(), len);
        return std::string(buf.begin(), buf.end());
    }

    // Helper: Read a Value from binary stream [Type][Len][Data]
    Value ReadValue(std::ifstream& in) {
        int type_id = 0;
        in.read(reinterpret_cast<char*>(&type_id), sizeof(int));
        
        // Use ReadString to get the serialized data
        std::string s_val = ReadString(in);
        
        // Convert string back to Value
        // In Phase 3, we will deserialize raw bytes. For now, string is safe.
        TypeId type = static_cast<TypeId>(type_id);
        
        if (type == TypeId::INTEGER) return Value(type, std::stoi(s_val));
        if (type == TypeId::BOOLEAN) return Value(type, static_cast<int8_t>(std::stoi(s_val)));
        // Default / Varchar
        return Value(type, s_val);
    }

    void RecoveryManager::ARIES() {
        std::string filename = log_manager_->GetLogFileName(); // You need to add this getter to LogManager!
        std::ifstream log_file(filename, std::ios::binary | std::ios::in);

        if (!log_file.is_open()) {
            std::cerr << "RecoveryManager: Could not open log file: " << filename << std::endl;
            return;
        }

        std::cout << "=== ARIES RECOVERY STARTED ===" << std::endl;

        while (log_file.peek() != EOF) {
            // 1. Read Total Size
            int32_t size = 0;
            log_file.read(reinterpret_cast<char*>(&size), sizeof(int32_t));
            if (log_file.gcount() != sizeof(int32_t)) break;

            // 2. Read Header Fields
            LogRecord::lsn_t lsn, prev_lsn;
            LogRecord::txn_id_t txn_id;
            int log_type_int;

            log_file.read(reinterpret_cast<char*>(&lsn), sizeof(lsn));
            log_file.read(reinterpret_cast<char*>(&prev_lsn), sizeof(prev_lsn));
            log_file.read(reinterpret_cast<char*>(&txn_id), sizeof(txn_id));
            log_file.read(reinterpret_cast<char*>(&log_type_int), sizeof(int));

            LogRecordType type = static_cast<LogRecordType>(log_type_int);

            // 3. Read Data Fields based on Type
            std::string table_name;
            // Placeholders
            Value v1; 
            Value v2;

            if (type == LogRecordType::INSERT) {
                table_name = ReadString(log_file);
                v1 = ReadValue(log_file); // New Value
                
                std::cout << "[REDO] Txn " << txn_id << ": INSERT into " << table_name 
                          << " Value: " << v1.ToString() << std::endl;
            } 
            else if (type == LogRecordType::UPDATE) {
                table_name = ReadString(log_file);
                v1 = ReadValue(log_file); // Old Value
                v2 = ReadValue(log_file); // New Value

                std::cout << "[REDO] Txn " << txn_id << ": UPDATE " << table_name 
                          << " Old: " << v1.ToString() << " New: " << v2.ToString() << std::endl;
            }
            else if (type == LogRecordType::APPLY_DELETE) {
                table_name = ReadString(log_file);
                v1 = ReadValue(log_file); // Old Value

                std::cout << "[REDO] Txn " << txn_id << ": DELETE from " << table_name 
                          << " Value: " << v1.ToString() << std::endl;
            }
        }
        
        std::cout << "=== RECOVERY COMPLETE ===" << std::endl;
    }

} // namespace francodb