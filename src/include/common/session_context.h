// common/session_context.h
#pragma once

#include <string>
#include "common/auth_manager.h"

namespace francodb {

struct SessionContext {
    std::string current_user;
    std::string current_db = "default";
    bool is_authenticated = false;
    uint32_t session_id = 0;
    UserRole role = UserRole::READONLY;  // Default to readonly until authenticated
};

} // namespace francodb
