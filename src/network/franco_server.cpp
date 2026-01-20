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

#include "network/franco_server.h"
#include "network/protocol.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "storage/disk/disk_manager.h"
#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "common/config.h"
#include "common/franco_net_config.h"
#include "network/database_registry.h"

#include <iostream>
#include <cstring>
#include <algorithm>
#include <string>
#include <filesystem>
#include <chrono>
#include <thread>

// Helper to check system db corruption
// static bool IsFileCorrupt(const std::string& path) {
//     if (!std::filesystem::exists(path)) return false; 
//     return std::filesystem::file_size(path) == 0;     
// }

namespace francodb {
    FrancoServer::FrancoServer(BufferPoolManager *bpm, Catalog *catalog)
        : bpm_(bpm), catalog_(catalog) {
        try {
            // [FIX 1] Initialize Registry FIRST, before System Resources
            // AuthManager (inside InitializeSystemResources) now depends on registry_
            registry_ = std::make_unique<DatabaseRegistry>();
            
            // Register the default DB immediately
            registry_->RegisterExternal("default", bpm_, catalog_);

            // [FIX 2] Now call this, so AuthManager can receive the valid registry_ pointer
            InitializeSystemResources();

            // Thread Pool
            unsigned int cores = std::thread::hardware_concurrency();
            int pool_size = (cores > 0) ? cores : 4;
            thread_pool_ = std::make_unique<ThreadPool>(pool_size);
            
        } catch (const std::exception &e) {
            std::cerr << "[CRITICAL] System DB Corrupt. Self-healing..." << std::endl;
        }
    }

    FrancoServer::~FrancoServer() {
        running_ = false;
        if (auto_save_thread_.joinable()) {
            auto_save_thread_.join();
        }
        Shutdown();
#ifdef _WIN32
        WSACleanup();
#endif
    }

    void FrancoServer::Start(int port) {
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
        listen_sock_ = (uintptr_t) s;
        running_ = true;

        auto_save_thread_ = std::thread(&FrancoServer::AutoSaveLoop, this);

        std::cout << "[READY] FrancoDB Server listening on port " << port << " (Pool Active)..." << std::endl;

        while (running_ && is_running_) {
            sockaddr_in client_addr{};
            int len = sizeof(client_addr);

            // Note: ThreadPool handles tasks fast, so blocking accept is usually okay.
            // Keeping select() logic for clean shutdown support.
#ifdef _WIN32
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(s, &readSet);
            timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);
            if (selectResult > 0 && FD_ISSET(s, &readSet)) {
#else
                fd_set readSet;
                FD_ZERO(&readSet);
                FD_SET(s, &readSet);
                timeval timeout;
                timeout.tv_sec = 1;
                timeout.tv_usec = 0;
                int selectResult = select(s + 1, &readSet, nullptr, nullptr, &timeout);
                if (selectResult > 0 && FD_ISSET(s, &readSet)) { 
#endif
                socket_t client_sock = accept(s, (struct sockaddr *) &client_addr, &len);
                if (client_sock != INVALID_SOCK && running_) {
                    uintptr_t client_id = (uintptr_t) client_sock;

                    // Push client to Thread Pool
                    thread_pool_->Enqueue([this, client_id] {
                        this->HandleClient(client_id);
                    });
                }
            }
        }

#ifdef _WIN32
        closesocket((socket_t) listen_sock_);
#else
        close((socket_t) listen_sock_);
#endif
    }


    void FrancoServer::Shutdown() {
        std::cout << "[SHUTDOWN] Flushing buffers..." << std::endl;
        if (auth_manager_) auth_manager_->SaveUsers();
        if (system_catalog_) system_catalog_->SaveCatalog();
        if (system_bpm_) system_bpm_->FlushAllPages();
        if (registry_) registry_->FlushAllDatabases();
        if (catalog_) catalog_->SaveCatalog();
        if (bpm_) bpm_->FlushAllPages();

        running_ = false;
        is_running_ = false;
        if (listen_sock_ != 0) {
#ifdef _WIN32
            closesocket((socket_t) listen_sock_);
#else
            close((socket_t) listen_sock_);
#endif
            listen_sock_ = 0;
        }
    }


    void FrancoServer::AutoSaveLoop() {
        while (running_) {
            for (int i = 0; i < 300 && running_; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (running_) {
                if (bpm_) bpm_->FlushAllPages();
                if (catalog_) catalog_->SaveCatalog();
                if (system_bpm_) system_bpm_->FlushAllPages();
                if (system_catalog_) system_catalog_->SaveCatalog();
            }
        }
    }

    void FrancoServer::InitializeSystemResources() {
        auto &config = ConfigManager::GetInstance();
        std::string data_dir = config.GetDataDirectory();
        
        // 1. Define paths (this was missing in the snippet)
        std::filesystem::path system_dir = std::filesystem::path(data_dir) / "system";
        std::filesystem::path system_db_path = system_dir / "system.francodb";

        // 2. Create directories and check for corruption
        std::filesystem::create_directories(system_dir);

        if (std::filesystem::exists(system_db_path) && std::filesystem::file_size(system_db_path) < 4096) {
            std::cout << "[RECOVERY] System DB is too small. Wiping." << std::endl;
            std::filesystem::remove(system_db_path);
        }

        // 3. Initialize System Components
        system_disk_ = std::make_unique<DiskManager>(system_db_path.string());
        
        if (config.IsEncryptionEnabled()) {
            system_disk_->SetEncryptionKey(config.GetEncryptionKey());
        }
        
        system_bpm_ = std::make_unique<BufferPoolManager>(BUFFER_POOL_SIZE, system_disk_.get());
        system_catalog_ = std::make_unique<Catalog>(system_bpm_.get());

        // [FIX] Now we pass registry_.get() here
        auth_manager_ = std::make_unique<AuthManager>(
            system_bpm_.get(), 
            system_catalog_.get(), 
            registry_.get()
        );
    }


    std::string FrancoServer::DispatchCommand(const std::string &sql, ClientConnectionHandler *handler) {
        std::string upper_sql = sql;
        std::transform(upper_sql.begin(), upper_sql.end(), upper_sql.begin(), ::toupper);

        // 1. WHOAMI
        if (upper_sql.find("WHOAMI") != std::string::npos) {
            return "Current User: " + (handler->IsAuthenticated() ? handler->GetCurrentUser() : "Guest");
        }

        // 2. SHOW DATABASES (SECURE VERSION)
        if (upper_sql.find("SHOW DATABASES") != std::string::npos || upper_sql.find("WARINI DATABASE") !=
            std::string::npos) {
            if (!handler->IsAuthenticated()) {
                return "ERROR: Authentication required.";
            }

            std::string user = handler->GetCurrentUser();
            std::stringstream ss;
            ss << "--- AVAILABLE DATABASES ---\n";

            // Always check if user can see 'default'
            if (auth_manager_->HasDatabaseAccess(user, "francodb")) {
                ss << "default\n";
            }

            // Scan Data Directory for other DBs
            auto &config = ConfigManager::GetInstance();
            std::filesystem::path data_path(config.GetDataDirectory());

            if (std::filesystem::exists(data_path)) {
                for (const auto &entry: std::filesystem::directory_iterator(data_path)) {
                    if (entry.is_directory()) {
                        std::string db_name = entry.path().filename().string();
                        // Skip system folder (internal use only)
                        if (db_name == "system" || db_name == "default") continue;

                        // ASK AUTH MANAGER: Does this user have access?
                        if (auth_manager_->HasDatabaseAccess(user, db_name)) {
                            ss << db_name << "\n";
                        }
                    }
                }
            }
            return ss.str();
        }

        // 3. CREATE USER (Delegate to Handler/Auth logic if parser supports it, 
        //    otherwise rely on ProcessRequest to handle standard SQL statements)

        // 4. Default: Send to Volcano Engine / Connection Handler
        return handler->ProcessRequest(sql);
    }

    // Add this implementation
    void FrancoServer::Stop() {
        running_ = false;
        is_running_ = false;

        // This forces accept() to throw/return, unblocking the main thread
        if (listen_sock_ != 0) {
#ifdef _WIN32
            shutdown((socket_t) listen_sock_, SD_BOTH);
            closesocket((socket_t) listen_sock_);
#else
            shutdown((socket_t) listen_sock_, SHUT_RDWR);
            close((socket_t) listen_sock_);
#endif
            listen_sock_ = 0;
        }
    }


    // src/network/franco_server.cpp

    void FrancoServer::HandleClient(uintptr_t client_socket) {
        socket_t sock = (socket_t) client_socket;
        
        auto engine = std::make_unique<ExecutionEngine>(
            bpm_, 
            catalog_, 
            auth_manager_.get(), 
            registry_.get()
        );
        
        auto handler = std::make_unique<ClientConnectionHandler>(engine.release(), auth_manager_.get());

        while (running_) {
            PacketHeader header;
            int bytes_read = recv(sock, (char *) &header, sizeof(header), MSG_WAITALL);

            if (bytes_read <= 0) break;

            uint32_t payload_len = ntohl(header.length);
            // Safety check against DOS attacks (Max 10MB packet)
            if (payload_len > 1024 * 1024 * 10) break;

            std::vector<char> payload(payload_len);
            recv(sock, payload.data(), payload_len, MSG_WAITALL);

            std::string sql(payload.begin(), payload.end());

            switch (header.type) {
                case MsgType::CMD_JSON: handler->SetResponseFormat(ProtocolType::JSON);
                    break;
                case MsgType::CMD_BINARY: handler->SetResponseFormat(ProtocolType::BINARY);
                    break;
                default: handler->SetResponseFormat(ProtocolType::TEXT);
                    break;
            }

            // [FIX] Use DispatchCommand instead of direct ProcessRequest
            std::string response = DispatchCommand(sql, handler.get());

            send(sock, response.c_str(), response.size(), 0);
        }

#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
    }
}
