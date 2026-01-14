#pragma once

#include <string>
#include <vector>
#include "common/franco_net_config.h"

namespace francodb {

    class FrancoClient {
    public:
        FrancoClient();
        ~FrancoClient();

        // High-level API
        bool Connect(const std::string& ip = net::DEFAULT_SERVER_IP, int port = net::DEFAULT_PORT);
        std::string Query(const std::string& sql);
        void Disconnect();

    private:
        // Hide the ugly OS-specific socket types
#ifdef _WIN32
        uintptr_t sock_{0}; // SOCKET in Windows is unsigned long long
#else
        int sock_{-1};
#endif
        bool is_connected_{false};
    };

} // namespace francodb