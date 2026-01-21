#pragma once

#include "recovery/log_manager.h"
#include "recovery/log_record.h"
#include <map>

namespace francodb {

    class RecoveryManager {
    public:
        explicit RecoveryManager(LogManager* log_manager) : log_manager_(log_manager) {}

   
        void ARIES();

    private:
        // Helper to deserialize a single log from the buffer
        // Returns true if successful, false if end of file
        bool ParseLog(std::ifstream& log_stream, LogRecord& out_record);

    private:
        LogManager* log_manager_;
    
        // For ARIES Analysis Phase:
        // Keep track of active transactions found in the log
        std::map<int, int> active_txns_; 
    };

} // namespace francodb