#pragma once

#include "recovery/log_manager.h"
#include "recovery/log_record.h"
#include <map>

namespace francodb {

    class RecoveryManager {
    public:
        explicit RecoveryManager(LogManager* log_manager) : log_manager_(log_manager) {}

        void RunRecoveryLoop(uint64_t stop_at_time, uint64_t start_offset);

        void ARIES();
        
        void RecoverToTime(uint64_t target_time_microseconds);

    private:
        LogManager* log_manager_;
    };

} // namespace francodb