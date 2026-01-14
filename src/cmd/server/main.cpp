#include <iostream>
#include <thread>
#include <vector>
#include <cstring>
#include <algorithm>

// --- CROSS PLATFORM SOCKETS ---
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    // Linker is handled by CMake now
    typedef SOCKET socket_t;
    #define CLOSE_SOCKET closesocket
    #define IS_VALID(s) (s != INVALID_SOCKET)
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    typedef int socket_t;
    #define CLOSE_SOCKET close
    #define IS_VALID(s) (s >= 0)
    #define INVALID_SOCKET -1
#endif

#include "storage/disk/disk_manager.h"
#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "execution/execution_engine.h"
#include "parser/parser.h"

using namespace francodb;

// GLOBAL DB INSTANCE
DiskManager *g_disk_manager;
BufferPoolManager *g_bpm;
Catalog *g_catalog;

// --- CLIENT HANDLER (Worker Thread) ---
void HandleClient(socket_t client_socket) {
    std::cout << "[SERVER] New Client Connected!" << std::endl;
    
    // Each client gets its own Execution Engine (Session)
    ExecutionEngine engine(g_bpm, g_catalog);

    char buffer[4096];
    
    while (true) {
        // 1. Clear Buffer
        memset(buffer, 0, 4096);
        
        // 2. Read from Socket
        int bytes_read = recv(client_socket, buffer, 4096, 0);
        if (bytes_read <= 0) {
            std::cout << "[SERVER] Client Disconnected." << std::endl;
            break; 
        }

        // 3. Process SQL
        std::string sql(buffer);
        // Remove newlines usually sent by telnet
        sql.erase(std::remove(sql.begin(), sql.end(), '\n'), sql.end());
        sql.erase(std::remove(sql.begin(), sql.end(), '\r'), sql.end());

        if (sql == "exit" || sql == "QUIT") break;

        std::cout << "[SQL] " << sql << std::endl;
        
        try {
            // Parse & Execute
            Lexer lexer(sql);
            Parser parser(std::move(lexer));
            auto stmt = parser.ParseQuery();
            
            if (stmt) {
                // Execute logic
                engine.Execute(stmt.get());
                
                std::string msg = "Command Executed (Check Server Console)\n";
                send(client_socket, msg.c_str(), msg.size(), 0);
            }
        } catch (const std::exception &e) {
            std::string error_msg = "ERROR: " + std::string(e.what()) + "\n";
            send(client_socket, error_msg.c_str(), error_msg.size(), 0);
        }
    }

    CLOSE_SOCKET(client_socket);
}

// --- MAIN SERVER LOOP ---
int main() {
    // 1. Initialize Windows Sockets (WSA)
    #ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed." << std::endl;
            return 1;
        }
    #endif

    // 2. Initialize Database Core
    std::cout << "[INIT] Starting FrancoDB Storage Engine..." << std::endl;
    g_disk_manager = new DiskManager("francodb.db");
    g_bpm = new BufferPoolManager(100, g_disk_manager);
    g_catalog = new Catalog(g_bpm);

    // 3. Setup Listening Socket
    socket_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (!IS_VALID(server_fd)) {
        std::cerr << "Socket creation failed" << std::endl;
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on 0.0.0.0 (All IPs)
    int port = 2501;
    address.sin_port = htons(port);       // PORT 2501

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed. Port " << port <<  " might be in use." << std::endl;
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        std::cerr << "Listen failed" << std::endl;
        return 1;
    }

    std::cout << "[READY] FrancoDB Server listening on Port "<< port << "..." << std::endl;

    // 4. Accept Loop
    while (true) {
        sockaddr_in client_addr{};
        
        // We define 'len' correctly based on OS.
        #ifdef _WIN32
        int len = sizeof(client_addr);
        #else
        socklen_t len = sizeof(client_addr);
        #endif

        socket_t client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &len);
        
        if (IS_VALID(client_socket)) {
            // Spawn a thread for this client
            std::thread t(HandleClient, client_socket);
            t.detach(); // Let it run independently
        }
    }

    // Cleanup
    #ifdef _WIN32
        WSACleanup();
    #endif
    return 0;
}