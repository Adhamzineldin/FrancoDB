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
#include <netdb.h>
#include <cerrno>
typedef int socket_t;
#define INVALID_SOCK -1
#endif

#include "network/franco_client.h"
#include "network/protocol.h"
#include "common/franco_net_config.h"

#include <cstring>
#include <iostream>
#include <sstream>

namespace francodb {

    FrancoClient::FrancoClient(ProtocolType protocol)
        : protocol_(std::unique_ptr<ProtocolSerializer>(CreateProtocol(protocol))),
          protocol_type_(protocol) {
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

    bool FrancoClient::Connect(const std::string& ip, int port, const std::string &username, const std::string &password, const std::string &database) {
        socket_t s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCK) {
#ifdef _WIN32
            int err = WSAGetLastError();
#else
            int err = errno;
#endif
            std::cerr << "[DEBUG] socket() failed with error: " << err << std::endl;
            return false;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        // Use system DNS / name resolution (getaddrinfo) so we support hostnames
        struct addrinfo hints{};
        struct addrinfo *result = nullptr;
        hints.ai_family = AF_INET;      // IPv4
        hints.ai_socktype = SOCK_STREAM;

        int gai_rc = getaddrinfo(ip.c_str(), nullptr, &hints, &result);
        if (gai_rc != 0 || result == nullptr) {
#ifdef _WIN32
            closesocket(s);
#else
            close(s);
#endif
            std::cerr << "[DEBUG] getaddrinfo() failed for host: " << ip
                      << " (rc=" << gai_rc << ")" << std::endl;
            return false;
        }

        // Take the first resolved address
        auto *resolved = reinterpret_cast<sockaddr_in *>(result->ai_addr);
        addr.sin_addr = resolved->sin_addr;
        freeaddrinfo(result);

        if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            closesocket(s);
#else
            int err = errno;
            close(s);
#endif
            std::cerr << "[DEBUG] connect() failed to " << ip << ":" << port << " with error: " << err << std::endl;
            return false;
        }

        sock_ = (uintptr_t)s;
        is_connected_ = true;

        // Require auth to establish a usable connection (send LOGIN immediately)
        if (!username.empty()) {
            std::string login_cmd = "LOGIN " + username + " " + password + ";\n";
            std::string res = Query(login_cmd);
            // server replies "LOGIN OK ..." on success
            if (res.find("LOGIN OK") == std::string::npos) {
                std::cerr << "[DEBUG] LOGIN failed. Server response: " << res << std::endl;
                Disconnect();
                return false;
            }
        }
        
        // Auto-switch to database if provided
        if (!database.empty()) {
            std::string use_cmd = "USE " + database + ";\n";
            Query(use_cmd); // Don't fail if DB doesn't exist, just try
        }
        
        return true;
    }

    bool FrancoClient::ConnectFromString(const std::string &connection_string) {
        // Parse: maayn://user:pass@host:port/dbname
        // Format: protocol://[user[:pass]@]host[:port][/database]
        // Examples:
        //   maayn://maayn:root@localhost:2501/mydb
        //   maayn://maayn:root@localhost/mydb
        //   maayn://maayn:root@localhost
        //   maayn://localhost
        
        if (connection_string.find("maayn://") != 0) {
            return false;
        }
        
        std::string rest = connection_string.substr(8); // Skip "maayn://"
        std::string username, password, host, database;
        int port = net::DEFAULT_PORT;
        
        // Extract user:pass@ if present
        size_t at_pos = rest.find('@');
        if (at_pos != std::string::npos) {
            std::string auth = rest.substr(0, at_pos);
            rest = rest.substr(at_pos + 1);
            
            size_t colon_pos = auth.find(':');
            if (colon_pos != std::string::npos) {
                username = auth.substr(0, colon_pos);
                password = auth.substr(colon_pos + 1);
            } else {
                username = auth;
                password = "";
            }
        } else {
            username = net::DEFAULT_ADMIN_USERNAME;
            password = net::DEFAULT_ADMIN_PASSWORD;
        }
        
        // Extract database if present (after /)
        size_t slash_pos = rest.find('/');
        if (slash_pos != std::string::npos) {
            database = rest.substr(slash_pos + 1);
            rest = rest.substr(0, slash_pos);
        }
        
        // Extract port if present (after :)
        size_t colon_pos = rest.find(':');
        if (colon_pos != std::string::npos) {
            host = rest.substr(0, colon_pos);
            std::string port_str = rest.substr(colon_pos + 1);
            try {
                port = std::stoi(port_str);
            } catch (...) {
                return false; // Invalid port
            }
        } else {
            host = rest;
        }
        
        if (host.empty()) {
            std::cerr << "[DEBUG] Empty host after parsing connection string" << std::endl;
            return false;
        }
        
        std::cerr << "[DEBUG] Parsed connection string: host=" << host << ", port=" << port 
                  << ", user=" << username << ", db=" << (database.empty() ? "(none)" : database) << std::endl;
        
        return Connect(host, port, username, password, database);
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