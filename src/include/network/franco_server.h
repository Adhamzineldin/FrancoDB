// network/franco_server.h (updated)
#pragma once
#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "network/connection_handler.h"
#include "network/protocol.h"
#include "common/franco_net_config.h"

#include <thread>
#include <atomic>
#include <map>

namespace francodb {

    class FrancoServer {
    private:
        BufferPoolManager* bpm_;
        Catalog* catalog_;
        std::atomic<bool> running_{false};
        uintptr_t listen_sock_{0};
        std::map<uintptr_t, std::thread> client_threads_;
        
        // Protocol detection
        ProtocolType DetectProtocol(const std::string& initial_data);
        
    public:
        FrancoServer(BufferPoolManager* bpm, Catalog* catalog);
        ~FrancoServer();
        
        void Start(int port = net::DEFAULT_PORT);
        void Shutdown();
        
    private:
        void HandleClient(uintptr_t client_socket);
        std::unique_ptr<ConnectionHandler> CreateHandler(
            ProtocolType type, uintptr_t client_socket);
    };
}
