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
static bool IsFileCorrupt(const std::string& path) {
    if (!std::filesystem::exists(path)) return false; 
    return std::filesystem::file_size(path) == 0;     
}

namespace francodb {
    FrancoServer::FrancoServer(BufferPoolManager* bpm, Catalog* catalog)
        : bpm_(bpm), catalog_(catalog) {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

        auto& config = ConfigManager::GetInstance();
        std::string data_dir = config.GetDataDirectory();
        
        std::filesystem::path system_dir = std::filesystem::path(data_dir) / "system";
        std::filesystem::create_directories(system_dir);
        std::filesystem::path system_db_path = system_dir / "francodb.db";

        // FIX: Check for System DB Corruption
        if (IsFileCorrupt(system_db_path.string())) {
            std::cout << "[WARN] System DB corrupt. Resetting..." << std::endl;
            std::filesystem::remove(system_db_path);
        }

        system_disk_ = std::make_unique<DiskManager>(system_db_path.string());
        
        if (config.IsEncryptionEnabled() && !config.GetEncryptionKey().empty()) {
            system_disk_->SetEncryptionKey(config.GetEncryptionKey());
        }
        
        system_bpm_ = std::make_unique<BufferPoolManager>(BUFFER_POOL_SIZE, system_disk_.get());
        system_catalog_ = std::make_unique<Catalog>(system_bpm_.get());
        
        try {
            system_catalog_->LoadCatalog();
        } catch (...) {
            std::cout << "[INFO] Creating new System Catalog." << std::endl;
        }
        
        auth_manager_ = std::make_unique<AuthManager>(system_bpm_.get(), system_catalog_.get());
        
        system_catalog_->SaveCatalog();
        system_bpm_->FlushAllPages();

        registry_ = std::make_unique<DatabaseRegistry>();
        registry_->RegisterExternal("default", bpm_, catalog_);
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
        socket_t s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCK) {
            std::cerr << "[ERROR] Failed to create socket" << std::endl;
            throw std::runtime_error("Socket creation failed");
        }

        int opt = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[ERROR] Server Bind Failed on port " << port << std::endl;
#ifdef _WIN32
            closesocket(s);
#else
            close(s);
#endif
            throw std::runtime_error("Bind failed");
        }

        listen(s, net::BACKLOG_QUEUE);
        listen_sock_ = (uintptr_t)s;
        running_ = true;
        
        auto_save_thread_ = std::thread(&FrancoServer::AutoSaveLoop, this);

        std::cout << "[READY] FrancoDB Server listening on port " << port << "..." << std::endl;

        while (running_) {
            sockaddr_in client_addr{};
            int len = sizeof(client_addr);
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
            socklen_t slen = sizeof(client_addr);
             fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(s, &readSet);
            timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            int selectResult = select(s + 1, &readSet, nullptr, nullptr, &timeout);
            if (selectResult > 0 && FD_ISSET(s, &readSet)) {
#endif
                socket_t client_sock = accept(s, (struct sockaddr*)&client_addr, &len);
                if (client_sock != INVALID_SOCK && running_) {
                    uintptr_t client_id = (uintptr_t)client_sock;
                    client_threads_[client_id] = std::thread(&FrancoServer::HandleClient, this, client_id);
                    client_threads_[client_id].detach();
                }
            }
        }

#ifdef _WIN32
        closesocket((socket_t)listen_sock_);
#else
        close((socket_t)listen_sock_);
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
        if (listen_sock_ != 0) {
#ifdef _WIN32
            closesocket((socket_t)listen_sock_);
#else
            close((socket_t)listen_sock_);
#endif
            listen_sock_ = 0;
        }
    }

    void FrancoServer::AutoSaveLoop() {
        while (running_) {
            for (int i = 0; i < 300 && running_; ++i) { // 30s
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

    void FrancoServer::HandleClient(uintptr_t client_socket) {
        socket_t sock = (socket_t)client_socket;
        
        // Create Handler ONCE to persist session
        auto engine = std::make_unique<ExecutionEngine>(bpm_, catalog_);
        auto handler = std::make_unique<ClientConnectionHandler>(engine.release(), auth_manager_.get());

        while (running_) {
            // 1. READ HEADER (5 Bytes)
            PacketHeader header;
            int bytes_read = recv(sock, (char*)&header, sizeof(header), MSG_WAITALL);
            
            if (bytes_read <= 0) break; 

            uint32_t payload_len = ntohl(header.length);
            if (payload_len > net::MAX_PACKET_SIZE) break;
            
            std::vector<char> payload(payload_len);
            recv(sock, payload.data(), payload_len, MSG_WAITALL);
            
            std::string sql(payload.begin(), payload.end());

            // 2. PROCESS (Using Packet Type to determine output format if needed)
            // For now, we just pass SQL to the existing handler
            std::string response = handler->ProcessRequest(sql);

            // 3. SEND RESPONSE (For now, raw text)
            send(sock, response.c_str(), response.size(), 0);
        }
        
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
    }
}