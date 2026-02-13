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

#include "storage/storage_interface.h"  // For IBufferManager
#include "catalog/catalog.h"
#include "execution/execution_engine.h"
#include "network/connection_handler.h"
#include "common/auth_manager.h"
#include "network/database_registry.h"
#include "network/protocol.h"
#include "parser/parser.h" // Ensure you have this for StatementType
#include "recovery/log_manager.h"
#include "recovery/checkpoint_manager.h"
#include "web/http_handler.h"

namespace chronosdb {

    class ChronosServer {
    public:
        // Accept IBufferManager for polymorphic buffer pool usage
        ChronosServer(IBufferManager *bpm, Catalog *catalog, LogManager *log_manager);
        ~ChronosServer();

        void Start(int port);
        void Shutdown();
        void RequestShutdown() { running_ = false; }
        
        IBufferManager* GetSystemBpm() { return system_bpm_.get(); }
        Catalog* GetSystemCatalog() { return system_catalog_.get(); }
        AuthManager* GetAuthManager() { return auth_manager_.get(); }
        CheckpointManager* GetCheckpointManager() { return checkpoint_mgr_.get(); }
        
        void Stop();

    private:
        void InitializeSystemResources();
        void HandleClient(uintptr_t client_socket);
        void HandleHttpClient(uintptr_t client_socket);
        void AutoSaveLoop();
        
        // [FIX] New Dispatcher to route System vs Data commands
        std::string DispatchCommand(const std::string& sql, ClientConnectionHandler* handler);

        // Core Components
        IBufferManager* bpm_;
        Catalog* catalog_;
        LogManager *log_manager_;
        
        // System Components
        std::unique_ptr<DiskManager> system_disk_;
        std::unique_ptr<BufferPoolManager> system_bpm_;
        std::unique_ptr<Catalog> system_catalog_;
        std::unique_ptr<AuthManager> auth_manager_;
        std::unique_ptr<DatabaseRegistry> registry_;
        
        // Checkpoint Manager - persistent for operation-based checkpointing
        std::unique_ptr<CheckpointManager> checkpoint_mgr_;

        // Web Admin HTTP Handler
        std::unique_ptr<web::HttpHandler> http_handler_;

        // [FIX] Thread Pool replaces the raw thread map
        std::unique_ptr<ThreadPool> thread_pool_;

        std::atomic<bool> running_{false};
        std::atomic<uintptr_t> listen_sock_{0};
        
        // Removed: std::map<uintptr_t, std::thread> client_threads_;
        std::thread auto_save_thread_;
        std::atomic<bool> is_running_{false};
    };

}