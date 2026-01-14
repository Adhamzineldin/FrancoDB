#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include "common/franco_net_config.h"
#include "execution/execution_engine.h"

namespace francodb {

    class FrancoServer {
    public:
        FrancoServer(BufferPoolManager *bpm, Catalog *catalog);
        ~FrancoServer();

        // Starts the server loop (blocking)
        void Start(int port = net::DEFAULT_PORT);
    
        // Stops the server
        void Shutdown();

    private:
        // Internal thread function to handle each client
        void HandleClient(uintptr_t client_socket);

        BufferPoolManager *bpm_;
        Catalog *catalog_;
    
#ifdef _WIN32
        uintptr_t listen_sock_{0};
#else
        int listen_sock_{-1};
#endif

        std::atomic<bool> running_{false};
    };

} // namespace francodb