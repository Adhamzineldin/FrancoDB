// network/franco_server.h (updated)
#pragma once
#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "network/connection_handler.h"
#include "network/protocol.h"
#include "common/franco_net_config.h"
#include "common/auth_manager.h"
#include "storage/disk/disk_manager.h"
#include "network/database_registry.h"

#include <thread>
#include <atomic>
#include <map>
#include <memory>

namespace francodb {

    class FrancoServer {
    private:
        // Default database components
        BufferPoolManager* bpm_;
        Catalog* catalog_;

        // System database for authentication (system.francodb)
        std::unique_ptr<DiskManager> system_disk_;
        std::unique_ptr<BufferPoolManager> system_bpm_;
        std::unique_ptr<Catalog> system_catalog_;
        std::unique_ptr<AuthManager> auth_manager_;

        // Multi-DB registry
        std::unique_ptr<DatabaseRegistry> registry_;
        std::atomic<bool> running_{false};
        uintptr_t listen_sock_{0};
        std::map<uintptr_t, std::thread> client_threads_;
        
        // Auto-save thread
        std::thread auto_save_thread_;
        void AutoSaveLoop();
        
        // Protocol detection
        ProtocolType DetectProtocol(const std::string& initial_data);

        // Ensure DB exists/loaded; returns Catalog* and BufferPoolManager*
        std::pair<Catalog*, BufferPoolManager*> GetOrCreateDb(const std::string &db_name);
        
    public:
        FrancoServer(BufferPoolManager* bpm, Catalog* catalog);
        ~FrancoServer();
        
        void Start(int port = net::DEFAULT_PORT);
        void Shutdown();
        bool IsRunning() const { return running_; }
        void RequestShutdown() { running_ = false; }
        
        // Get system components for crash handling
        AuthManager* GetAuthManager() { return auth_manager_.get(); }
        BufferPoolManager* GetSystemBpm() { return system_bpm_.get(); }
        Catalog* GetSystemCatalog() { return system_catalog_.get(); }
        
    private:
        void HandleClient(uintptr_t client_socket);
        std::unique_ptr<ConnectionHandler> CreateHandler(
            ProtocolType type, uintptr_t client_socket);
    };
}
