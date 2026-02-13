#include "network/packet.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCK -1
#endif

#include "network/chronos_server.h"
#include "network/protocol.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "storage/disk/disk_manager.h"
#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "common/config.h"
#include "common/chronos_net_config.h"
#include "network/database_registry.h"
#include "recovery/log_manager.h" // [FIX] Include LogManager
#include "recovery/checkpoint_manager.h"
#include "recovery/log_record.h"
#include "catalog/table_metadata.h"

#include <iostream>
#include <cstring>
#include <algorithm>
#include <string>
#include <filesystem>
#include <chrono>
#include <thread>
#include <future>

namespace chronosdb {
    
    // Accept IBufferManager for polymorphic buffer pool usage
    // Works with both BufferPoolManager and PartitionedBufferPoolManager
    ChronosServer::ChronosServer(IBufferManager *bpm, Catalog *catalog, LogManager *log_manager)
        : bpm_(bpm), catalog_(catalog), log_manager_(log_manager) {
        try {
            // Initialize Registry FIRST
            registry_ = std::make_unique<DatabaseRegistry>();
            
            // Register the default DB
            registry_->RegisterExternal("default", bpm_, catalog_);

            // Initialize Auth & System Resources
            InitializeSystemResources();

            // ================================================================
            // CHECKPOINT MANAGER - Operation-based checkpointing (Bug #6 fix)
            // ================================================================
            // Create a persistent checkpoint manager that tracks operations
            // and triggers checkpoints every N operations (default: 1000)
            checkpoint_mgr_ = std::make_unique<CheckpointManager>(bpm_, log_manager_);
            checkpoint_mgr_->SetCatalog(catalog_);
            checkpoint_mgr_->SetOperationThreshold(1000);  // Checkpoint every 1k operations
            
            // Connect LogManager to CheckpointManager for operation counting
            if (log_manager_) {
                log_manager_->SetCheckpointManager(checkpoint_mgr_.get());
            }
            
            // Start background checkpointing (30 second interval as backup)
            checkpoint_mgr_->StartBackgroundCheckpointing(30);
            std::cout << "[CheckpointManager] Initialized with master record: data/system/master_record" << std::endl;
            std::cout << "[CheckpointManager] Operation-based checkpoints every 1000 operations" << std::endl;

            // Web Admin HTTP Handler
            http_handler_ = std::make_unique<web::HttpHandler>(
                bpm_, catalog_, auth_manager_.get(), registry_.get(), log_manager_
            );
            // Look for React build in multiple candidate directories
            auto& cfg = ConfigManager::GetInstance();
            namespace fs = std::filesystem;
            fs::path exe_dir = fs::path(cfg.GetDataDirectory()).parent_path();
            bool found_web = false;
            for (const auto& candidate : {
                // Same directory as executable
                exe_dir / "web-admin" / "server" / "public",
                exe_dir / "web-admin" / "client" / "dist",
                // Parent of executable (project root when running from build/)
                exe_dir / ".." / "web-admin" / "server" / "public",
                exe_dir / ".." / "web-admin" / "client" / "dist",
                // Relative to CWD
                fs::path("web-admin") / "server" / "public",
                fs::path("web-admin") / "client" / "dist",
                fs::path("..") / "web-admin" / "server" / "public",
                fs::path("..") / "web-admin" / "client" / "dist",
            }) {
                if (fs::exists(candidate / "index.html")) {
                    http_handler_->SetWebRoot(fs::canonical(candidate).string());
                    std::cout << "[WEB] Serving web admin from: " << fs::canonical(candidate).string() << std::endl;
                    found_web = true;
                    break;
                }
            }
            if (!found_web) {
                std::cout << "[WEB] No React build found. Run 'cd web-admin/client && npm install && npm run build'" << std::endl;
                std::cout << "[WEB] Fallback page will be served at http://localhost:2501/" << std::endl;
            }
            std::cout << "[WEB] HTTP web admin interface enabled on same port" << std::endl;

            // Thread Pool
            unsigned int cores = std::thread::hardware_concurrency();
            int pool_size = (cores > 0) ? cores : 4;
            thread_pool_ = std::make_unique<ThreadPool>(pool_size);
            
        } catch (const std::exception &e) {
            std::cerr << "[CRITICAL] System Init Failed: " << e.what() << std::endl;
        }
    }

    ChronosServer::~ChronosServer() {
        std::cout << "[SHUTDOWN] Server destructor called..." << std::endl;
        
        // 1. Signal stop first
        running_.store(false);
        is_running_.store(false);
        
        // 2. Stop checkpoint manager background thread
        if (checkpoint_mgr_) {
            checkpoint_mgr_->StopBackgroundCheckpointing();
        }
        
        // 3. Wait for auto-save thread with timeout
        if (auto_save_thread_.joinable()) {
            std::cout << "[SHUTDOWN] Waiting for auto-save thread..." << std::endl;
            
            // Give it a reasonable time to finish current checkpoint
            auto future = std::async(std::launch::async, [this]() {
                if (auto_save_thread_.joinable()) {
                    auto_save_thread_.join();
                }
            });
            
            // Wait max 5 seconds for the auto-save thread
            if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
                std::cerr << "[SHUTDOWN] Warning: Auto-save thread did not finish in time" << std::endl;
                // Thread will be abandoned - this is better than deadlock
            } else {
                std::cout << "[SHUTDOWN] Auto-save thread finished cleanly" << std::endl;
            }
        }
        
        // 4. Now do the shutdown flush
        Shutdown();
        
#ifdef _WIN32
        WSACleanup();
#endif
        std::cout << "[SHUTDOWN] Server destructor complete" << std::endl;
    }

    void ChronosServer::Start(int port) {
        is_running_ = true;

        socket_t s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCK) {
            std::cerr << "[ERROR] Failed to create socket" << std::endl;
            throw std::runtime_error("Socket creation failed");
        }

        int opt = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *) &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
            std::cerr << "[ERROR] Server Bind Failed on port " << port << std::endl;
#ifdef _WIN32
            closesocket(s);
#else
            close(s);
#endif
            throw std::runtime_error("Bind failed");
        }

        listen(s, net::BACKLOG_QUEUE);
        listen_sock_.store((uintptr_t) s);
        running_.store(true);

        auto_save_thread_ = std::thread(&ChronosServer::AutoSaveLoop, this);

        std::cout << "[READY] ChronosDB Server listening on port " << port << " (Pool Active)..." << std::endl;

        while (running_.load() && is_running_.load()) {
            // Check if socket was closed by Stop() BEFORE using it
            uintptr_t current_sock = listen_sock_.load();
            if (current_sock == 0) {
                std::cout << "[SERVER] Socket closed by Stop(), exiting accept loop..." << std::endl;
                break;
            }
            
            // Use the current socket value, not the local 's' which might be stale
            socket_t active_sock = (socket_t)current_sock;
            
            sockaddr_in client_addr{};
            socklen_t len = sizeof(client_addr);

#ifdef _WIN32
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(active_sock, &readSet);
            timeval timeout;
            timeout.tv_sec = 0;  // Shorter timeout for faster shutdown response
            timeout.tv_usec = 500000; // 500ms
            int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);
            
            // Check running_ again after select returns (might have been signaled)
            if (!running_.load() || !is_running_.load() || listen_sock_.load() == 0) {
                std::cout << "[SERVER] Shutdown detected after select, exiting..." << std::endl;
                break;
            }
            
            // Handle select error (socket may have been closed)
            if (selectResult < 0) {
                int err = WSAGetLastError();
                if (err == WSAENOTSOCK || err == WSAEINTR || err == WSAEBADF) {
                    std::cout << "[SERVER] Socket error (closed), exiting..." << std::endl;
                    break;
                }
                // Other errors - just continue with next iteration
                continue;
            }
            
            if (selectResult > 0 && FD_ISSET(active_sock, &readSet)) {
#else
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(active_sock, &readSet);
            timeval timeout;
            timeout.tv_sec = 0;  // Shorter timeout for faster shutdown response
            timeout.tv_usec = 500000; // 500ms
            int selectResult = select(active_sock + 1, &readSet, nullptr, nullptr, &timeout);
            
            // Check running_ again after select returns (might have been signaled)
            if (!running_.load() || !is_running_.load() || listen_sock_.load() == 0) {
                std::cout << "[SERVER] Shutdown detected after select, exiting..." << std::endl;
                break;
            }
            
            // Handle select error (socket may have been closed)
            if (selectResult < 0) {
                if (errno == EBADF || errno == EINTR) {
                    std::cout << "[SERVER] Socket error (closed), exiting..." << std::endl;
                    break;
                }
                continue;
            }
            
            if (selectResult > 0 && FD_ISSET(active_sock, &readSet)) { 
#endif
                socket_t client_sock = accept(active_sock, (struct sockaddr *) &client_addr, &len);
                if (client_sock != INVALID_SOCK && running_.load()) {
                    uintptr_t client_id = (uintptr_t) client_sock;

                    // Push client to Thread Pool
                    thread_pool_->Enqueue([this, client_id] {
                        this->HandleClient(client_id);
                    });
                }
            } else if (selectResult < 0) {
                // Socket error - likely closed
                std::cout << "[SERVER] Select error, socket may be closed. Exiting..." << std::endl;
                break;
            }
        }
        
        std::cout << "[SERVER] Accept loop exited cleanly" << std::endl;

        // Only close socket if it wasn't already closed by Stop()
        uintptr_t sock_to_close = listen_sock_.exchange(0);
        if (sock_to_close != 0) {
#ifdef _WIN32
            closesocket((socket_t) sock_to_close);
#else
            close((socket_t) sock_to_close);
#endif
        }
    }


    void ChronosServer::Shutdown() {
        std::cout << "[SHUTDOWN] Flushing buffers..." << std::endl;
        if (auth_manager_) auth_manager_->SaveUsers();
        if (system_catalog_) system_catalog_->SaveCatalog();
        if (system_bpm_) system_bpm_->FlushAllPages();
        if (registry_) registry_->FlushAllDatabases();
        if (catalog_) catalog_->SaveCatalog();
        if (bpm_) bpm_->FlushAllPages();
        
        // [NOTE] LogManager flush is handled in main.cpp usually, or can be added here
        if (log_manager_) log_manager_->Flush(true);

        running_.store(false);
        is_running_.store(false);
        
        uintptr_t sock = listen_sock_.exchange(0);
        if (sock != 0) {
#ifdef _WIN32
            closesocket((socket_t) sock);
#else
            close((socket_t) sock);
#endif
        }
    }


    void ChronosServer::AutoSaveLoop() {
        while (running_.load()) {
            // Sleep in small increments to respond to shutdown quickly
            for (int i = 0; i < 300 && running_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            // Check again after sleep - might have been signaled to stop
            if (!running_.load()) {
                std::cout << "[AUTO-SAVE] Shutdown detected, exiting loop..." << std::endl;
                break;
            }
            
            std::cout << "[SERVER] Auto-Checkpoint Initiating..." << std::endl;
            
            // Try to acquire lock with timeout to avoid deadlock during shutdown
            {
                std::unique_lock<std::shared_mutex> lock(ExecutionEngine::global_lock_, std::defer_lock);
                
                // Try to lock with timeout
                auto start = std::chrono::steady_clock::now();
                bool acquired = false;
                while (!acquired && running_.load()) {
                    acquired = lock.try_lock();
                    if (!acquired) {
                        auto elapsed = std::chrono::steady_clock::now() - start;
                        if (elapsed > std::chrono::seconds(5)) {
                            std::cerr << "[AUTO-SAVE] Could not acquire lock, skipping checkpoint" << std::endl;
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                }
                
                if (!acquired || !running_.load()) {
                    continue;
                }
                
                // Flush pages first
                if (bpm_) bpm_->FlushAllPages();
                if (system_bpm_) system_bpm_->FlushAllPages();
                
                // Use the persistent checkpoint manager (not creating a new one)
                // This ensures operation counts and state are properly tracked
                if (!checkpoint_mgr_) {
                    checkpoint_mgr_ = std::make_unique<CheckpointManager>(bpm_, log_manager_);
                    checkpoint_mgr_->SetCatalog(catalog_);
                    checkpoint_mgr_->SetOperationThreshold(1000);
                }
                
                // Update catalog reference in case it changed
                checkpoint_mgr_->SetCatalog(catalog_);
                checkpoint_mgr_->BeginCheckpoint();
                
                // Get the checkpoint LSN that was just created
                LogRecord::lsn_t checkpoint_lsn = checkpoint_mgr_->GetLastCheckpointLSN();
                
                // CRITICAL: Update ALL tables in MAIN catalog with this checkpoint LSN
                // (BeginCheckpoint should do this, but we ensure it here)
                if (catalog_) {
                    auto all_tables = catalog_->GetAllTables();
                    for (auto* table : all_tables) {
                        if (table) {
                            table->SetCheckpointLSN(checkpoint_lsn);
                        }
                    }
                    // Save catalog AFTER checkpoint so checkpoint LSN is persisted
                    catalog_->SaveCatalog();
                }
                if (system_catalog_) system_catalog_->SaveCatalog();
                
                // Also flush and update checkpoint LSN for all OTHER loaded databases
                // CRITICAL FIX: Each database in the registry needs its tables updated
                if (registry_) {
                    registry_->ForEachDatabase([&](const std::string& db_name, DbEntry* entry) {
                        if (entry && entry->catalog) {
                            // Get the catalog for this database
                            Catalog* db_catalog = entry->catalog.get();
                            if (!db_catalog) {
                                // Check external catalog
                                db_catalog = registry_->ExternalCatalog(db_name);
                            }
                            
                            if (db_catalog) {
                                // Update checkpoint LSN for ALL tables in this database
                                auto all_tables = db_catalog->GetAllTables();
                                for (auto* table : all_tables) {
                                    if (table) {
                                        // Use the same checkpoint LSN - all databases share this checkpoint
                                        table->SetCheckpointLSN(checkpoint_lsn);
                                    }
                                }
                                // Save the catalog with updated checkpoint LSNs
                                db_catalog->SaveCatalog();
                            }
                            
                            if (entry->bpm) {
                                entry->bpm->FlushAllPages();
                            }
                        }
                    });
                }
            
            } // Lock releases here -> Transactions resume automatically.

            std::cout << "[SERVER] Auto-Checkpoint Complete." << std::endl;
        }
        
        std::cout << "[AUTO-SAVE] Thread exiting cleanly" << std::endl;
    }

    void ChronosServer::InitializeSystemResources() {
        auto &config = ConfigManager::GetInstance();
        std::string data_dir = config.GetDataDirectory();
        
        std::filesystem::path system_dir = std::filesystem::path(data_dir) / "system";
        std::filesystem::path system_db_path = system_dir / "system.chronosdb";

        std::filesystem::create_directories(system_dir);

        if (std::filesystem::exists(system_db_path) && std::filesystem::file_size(system_db_path) < 4096) {
            std::cout << "[RECOVERY] System DB is too small. Wiping." << std::endl;
            std::filesystem::remove(system_db_path);
        }

        system_disk_ = std::make_unique<DiskManager>(system_db_path.string());
        
        if (config.IsEncryptionEnabled()) {
            system_disk_->SetEncryptionKey(config.GetEncryptionKey());
        }
        
        system_bpm_ = std::make_unique<BufferPoolManager>(BUFFER_POOL_SIZE, system_disk_.get());
        system_catalog_ = std::make_unique<Catalog>(system_bpm_.get());

        // [FIX] Pass log_manager_ to AuthManager so system internal queries are logged/ACID compliant
        auth_manager_ = std::make_unique<AuthManager>(
            system_bpm_.get(), 
            system_catalog_.get(), 
            registry_.get(),
            log_manager_
        );
    }


    std::string ChronosServer::DispatchCommand(const std::string &sql, ClientConnectionHandler *handler) {
        std::string upper_sql = sql;
        std::transform(upper_sql.begin(), upper_sql.end(), upper_sql.begin(), ::toupper);

        // Handle STOP/SHUTDOWN command - requires SUPERADMIN
        if (upper_sql.find("STOP") == 0 || upper_sql.find("SHUTDOWN") == 0 ||
            upper_sql.find("WA2AF") == 0 || upper_sql.find("2AFOL") == 0) {
            
            if (!handler->IsAuthenticated()) {
                return "ERROR: Authentication required to stop server.";
            }
            
            // Check for SUPERADMIN role
            auto session = handler->GetSession();
            if (!session || session->role != UserRole::SUPERADMIN) {
                return "ERROR: Permission denied. Only SUPERADMIN can stop the server.";
            }
            
            std::cout << "[STOP] Server shutdown requested by user: " << session->current_user << std::endl;
            
            // Start graceful shutdown in a separate thread to allow response to be sent
            std::thread([this]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Allow response to be sent
                this->Stop();
            }).detach();
            
            return "SHUTDOWN INITIATED. Server will stop in 500ms.";
        }

        if (upper_sql.find("WHOAMI") != std::string::npos) {
            return "Current User: " + (handler->IsAuthenticated() ? handler->GetCurrentUser() : "Guest");
        }

        if (upper_sql.find("SHOW DATABASES") != std::string::npos || upper_sql.find("WARINI DATABASE") != std::string::npos) {
            if (!handler->IsAuthenticated()) {
                return "ERROR: Authentication required.";
            }

            std::string user = handler->GetCurrentUser();
            std::stringstream ss;
            ss << "--- AVAILABLE DATABASES ---\n";

            if (auth_manager_->HasDatabaseAccess(user, "chronosdb")) {
                ss << "default\n";
            }

            auto &config = ConfigManager::GetInstance();
            std::filesystem::path data_path(config.GetDataDirectory());

            if (std::filesystem::exists(data_path)) {
                for (const auto &entry: std::filesystem::directory_iterator(data_path)) {
                    if (entry.is_directory()) {
                        std::string db_name = entry.path().filename().string();
                        if (db_name == "system" || db_name == "default") continue;

                        if (auth_manager_->HasDatabaseAccess(user, db_name)) {
                            ss << db_name << "\n";
                        }
                    }
                }
            }
            return ss.str();
        }

        return handler->ProcessRequest(sql);
    }

    void ChronosServer::Stop() {
        std::cout << "[STOP] Initiating graceful shutdown..." << std::endl;
        
        // 1. Signal all loops to stop FIRST (non-blocking)
        running_.store(false);
        is_running_.store(false);

        // 2. Close the listening socket to unblock accept()
        uintptr_t sock = listen_sock_.exchange(0);
        if (sock != 0) {
#ifdef _WIN32
            // Shutdown first to unblock any blocking recv/send calls
            shutdown((socket_t) sock, SD_BOTH);
            closesocket((socket_t) sock);
#else
            shutdown((socket_t) sock, SHUT_RDWR);
            close((socket_t) sock);
#endif
        }
        
        std::cout << "[STOP] Socket closed, waiting for threads to finish..." << std::endl;
    }


    void ChronosServer::HandleHttpClient(uintptr_t client_socket) {
        socket_t sock = (socket_t) client_socket;

        // Handle HTTP request-response (one request per connection for simplicity)
        web::HttpRequest req;
        if (web::HttpHandler::ReadHttpRequest(client_socket, req)) {
            http_handler_->HandleRequest(client_socket, req);
        }

#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
    }

    void ChronosServer::HandleClient(uintptr_t client_socket) {
        socket_t sock = (socket_t) client_socket;

        // ── Peek at first bytes to detect HTTP vs ChronosDB protocol ──
        char peek_buf[4];
        int peek_n = recv(sock, peek_buf, sizeof(peek_buf), MSG_PEEK);
        if (peek_n >= 3 && web::HttpHandler::IsHttpRequest(peek_buf, peek_n)) {
            // Route to HTTP handler (web admin interface)
            HandleHttpClient(client_socket);
            return;
        }

        // [FIX] Pass log_manager_ to ExecutionEngine
        auto engine = std::make_unique<ExecutionEngine>(
            bpm_,
            catalog_,
            auth_manager_.get(),
            registry_.get(),
            log_manager_
        );

        auto handler = std::make_unique<ClientConnectionHandler>(engine.release(), auth_manager_.get());

        while (running_.load()) {
            PacketHeader header;
            int bytes_read = recv(sock, (char *) &header, sizeof(header), MSG_WAITALL);
            if (bytes_read <= 0) break;

            uint32_t payload_len = ntohl(header.length);
            if (payload_len > 1024 * 1024 * 10) break; // 10MB Limit

            std::vector<char> payload(payload_len);
            recv(sock, payload.data(), payload_len, MSG_WAITALL);

            std::string sql(payload.begin(), payload.end());

            switch (header.type) {
                case MsgType::CMD_JSON: handler->SetResponseFormat(ProtocolType::JSON); break;
                case MsgType::CMD_BINARY: handler->SetResponseFormat(ProtocolType::BINARY); break;
                default: handler->SetResponseFormat(ProtocolType::TEXT); break;
            }

            std::string response = DispatchCommand(sql, handler.get());

            uint32_t resp_len = htonl(response.size());
            int sent_header = send(sock, (char*)&resp_len, sizeof(resp_len), 0);
            if (sent_header <= 0) break;

            size_t total_sent = 0;
            while (total_sent < response.size()) {
                int sent = send(sock, response.c_str() + total_sent, response.size() - total_sent, 0);
                if (sent <= 0) break;
                total_sent += sent;
            }
        }

#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
    }
}