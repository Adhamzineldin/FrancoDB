// Platform networking headers MUST come before any header that may include <windows.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCK -1
#endif

#include "network/franco_client.h"

#include <cstring>
#include <iostream>

namespace francodb {

    FrancoClient::FrancoClient() {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    FrancoClient::~FrancoClient() {
        Disconnect();
#ifdef _WIN32
        WSACleanup();
#endif
    }

    bool FrancoClient::Connect(const std::string& ip, int port) {
        socket_t s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCK) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            return false;
        }

        sock_ = (uintptr_t)s;
        is_connected_ = true;
        return true;
    }

    std::string FrancoClient::Query(const std::string& sql) {
        if (!is_connected_) return "ERROR: Not connected to server.";

        // Send
        send((socket_t)sock_, sql.c_str(), sql.size(), 0);

        // Receive
        char buffer[net::MAX_PACKET_SIZE];
        memset(buffer, 0, net::MAX_PACKET_SIZE);
        int bytes_received = recv((socket_t)sock_, buffer, net::MAX_PACKET_SIZE - 1, 0);

        if (bytes_received <= 0) {
            is_connected_ = false;
            return "ERROR: Lost connection to server.";
        }

        return std::string(buffer);
    }

    void FrancoClient::Disconnect() {
        if (is_connected_) {
#ifdef _WIN32
            closesocket((socket_t)sock_);
#else
            close((socket_t)sock_);
#endif
            is_connected_ = false;
        }
    }

} // namespace francodb