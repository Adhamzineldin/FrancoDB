#pragma once
#include <string>

namespace francodb {
    namespace net { // Using a sub-namespace for even better isolation

        // --- CONNECTION SETTINGS ---
        static const std::string DEFAULT_SERVER_IP = "127.0.0.1";
        static const int DEFAULT_PORT = 2501; 

        // --- PROTOCOL SETTINGS ---
        static const int MAX_PACKET_SIZE = 4096; // 4KB
        static const int BACKLOG_QUEUE = 10;     // Max pending connections
    }
}