#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <thread>
#include <utility>

// [FIX] Include the new ThreadPool
#include "common/thread_pool.h"

#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "execution/execution_engine.h"
#include "network/connection_handler.h"
#include "common/auth_manager.h"
#include "network/database_registry.h"
#include "network/protocol.h"
#include "parser/parser.h" // Ensure you have this for StatementType
#include "recovery/log_manager.h"

namespace francodb {

    class FrancoServer {
    public:
        FrancoServer(BufferPoolManager *bpm, Catalog *catalog, LogManager *log_manager);
        ~FrancoServer();

        void Start(int port);
        void Shutdown();
        void RequestShutdown() { running_ = false; }
        
        BufferPoolManager* GetSystemBpm() { return system_bpm_.get(); }
        Catalog* GetSystemCatalog() { return system_catalog_.get(); }
        AuthManager* GetAuthManager() { return auth_manager_.get(); }
        
        void Stop();

    private:
        void InitializeSystemResources();
        void HandleClient(uintptr_t client_socket);
        void AutoSaveLoop();
        
        // [FIX] New Dispatcher to route System vs Data commands
        std::string DispatchCommand(const std::string& sql, ClientConnectionHandler* handler);

        // Core Components
        BufferPoolManager* bpm_;
        Catalog* catalog_;
        LogManager *log_manager_;
        
        // System Components
        std::unique_ptr<DiskManager> system_disk_;
        std::unique_ptr<BufferPoolManager> system_bpm_;
        std::unique_ptr<Catalog> system_catalog_;
        std::unique_ptr<AuthManager> auth_manager_;
        std::unique_ptr<DatabaseRegistry> registry_;

        // [FIX] Thread Pool replaces the raw thread map
        std::unique_ptr<ThreadPool> thread_pool_;

        std::atomic<bool> running_{false};
        uintptr_t listen_sock_ = 0;
        
        // Removed: std::map<uintptr_t, std::thread> client_threads_;
        std::thread auto_save_thread_;
        std::atomic<bool> is_running_{false};
    };

}