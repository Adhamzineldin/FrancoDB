// Platform networking headers MUST come before any header that may include <windows.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCKET -1
#endif

#include "network/franco_server.h"
#include "common/result_formatter.h"
#include "parser/lexer.h"
#include "parser/parser.h"

#include <iostream>
#include <cstring>
#include <algorithm>

namespace francodb {
    FrancoServer::FrancoServer(BufferPoolManager *bpm, Catalog *catalog)
        : bpm_(bpm), catalog_(catalog) {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    }

    FrancoServer::~FrancoServer() {
        Shutdown();
    }

    void FrancoServer::Start(int port) {
        socket_t s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == (socket_t) INVALID_SOCKET) return;

        int opt = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *) &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
            std::cerr << "[ERROR] Server Bind Failed on port " << port << std::endl;
            return;
        }

        listen(s, net::BACKLOG_QUEUE);
        listen_sock_ = (uintptr_t) s;
        running_ = true;

        std::cout << "[READY] FrancoDB Server listening on port " << port << "..." << std::endl;

        while (running_) {
            sockaddr_in client_addr{};
#ifdef _WIN32
            int len = sizeof(client_addr);
#else
            socklen_t len = sizeof(client_addr);
#endif
            socket_t client_sock = accept((socket_t) listen_sock_, (struct sockaddr *) &client_addr, &len);

            if (client_sock != (socket_t) INVALID_SOCKET) {
                std::thread(&FrancoServer::HandleClient, this, (uintptr_t) client_sock).detach();
            }
        }
    }

    void FrancoServer::HandleClient(uintptr_t client_socket) {
        socket_t sock = (socket_t) client_socket;
        ExecutionEngine engine(bpm_, catalog_);
        char buffer[net::MAX_PACKET_SIZE];

        while (running_) {
            memset(buffer, 0, net::MAX_PACKET_SIZE);
            int bytes = recv(sock, buffer, net::MAX_PACKET_SIZE - 1, 0);
            if (bytes <= 0) break;

            std::string sql(buffer);
            // Basic cleanup
            sql.erase(std::remove(sql.begin(), sql.end(), '\n'), sql.end());
            sql.erase(std::remove(sql.begin(), sql.end(), '\r'), sql.end());

            if (sql == "exit" || sql == "quit") break;

            try {
                Lexer lexer(sql);
                Parser parser(std::move(lexer));
                auto stmt = parser.ParseQuery();

                if (stmt) {
                    ExecutionResult res = engine.Execute(stmt.get());
                    std::string response;

                    if (!res.success) {
                        response = "ERROR: " + res.message + "\n";
                    } else if (res.result_set) {
                        response = ResultFormatter::Format(res.result_set);
                    } else {
                        response = res.message + "\n";
                    }
                    send(sock, response.c_str(), response.size(), 0);
                }
            } catch (const std::exception &e) {
                std::string err = "SYSTEM ERROR: " + std::string(e.what()) + "\n";
                send(sock, err.c_str(), err.size(), 0);
            }
        }
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
    }

    void FrancoServer::Shutdown() {
        running_ = false;
        // Closing listen_sock_ would be done here...
    }
} // namespace francodb
