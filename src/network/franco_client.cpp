// Platform networking headers MUST come before any header that may include <windows.h>
#include "network/packet.h"
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

    bool FrancoClient::Connect(const std::string& host, int port, const std::string &username, const std::string &password, const std::string &database) {
        // 1. Resolve Hostname (DNS)
        // This handles "localhost", "127.0.0.1", "google.com", IPv4, and IPv6 automatically.
        struct addrinfo hints{}, *result = nullptr;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;     // Allow IPv4 or IPv6
        hints.ai_socktype = SOCK_STREAM; // TCP

        std::string port_str = std::to_string(port);
        int gai_rc = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
        
        if (gai_rc != 0) {
            std::cerr << "[ERROR] Could not resolve host '" << host << "': " 
                      << gai_strerror(gai_rc) << std::endl;
            return false;
        }

        // 2. Iterate through results (The "Postgres Way")
        // If a domain has multiple IPs (or IPv6 + IPv4), we try them one by one.
        socket_t s = INVALID_SOCK;
        struct addrinfo *ptr = nullptr;
        
        for (ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
            // Create a socket for this specific result (IPv4 or IPv6)
            s = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
            if (s == INVALID_SOCK) {
                continue; // Try next address
            }

            // Attempt to connect
            if (connect(s, ptr->ai_addr, (int)ptr->ai_addrlen) != -1) {
                break; // Success! We found a working address.
            }

            // Connection failed, close this socket and try the next one
#ifdef _WIN32
            closesocket(s);
#else
            close(s);
#endif
            s = INVALID_SOCK;
        }

        freeaddrinfo(result);

        // 3. Check if we connected
        if (s == INVALID_SOCK) {
            std::cerr << "[ERROR] Could not connect to any address for host: " << host << std::endl;
            return false;
        }

        // 4. Store Socket
        sock_ = (uintptr_t)s;
        is_connected_ = true;

        // 5. Authenticate (LOGIN)
        // This mimics standard DB clients: Connect -> Handshake immediately.
        if (!username.empty()) {
            std::string login_cmd = "LOGIN " + username + " " + password + ";\n";
            std::string res = Query(login_cmd);
            
            if (res.find("LOGIN OK") == std::string::npos) {
                std::cerr << "[FATAL] Authentication failed: " << res << std::endl;
                Disconnect();
                return false;
            }
        }
        
        // 6. Select Database
        if (!database.empty()) {
            Query("USE " + database + ";\n");
        }
        
        return true;
    }

    bool FrancoClient::ConnectFromString(const std::string &connection_string) {
        if (connection_string.find("maayn://") != 0) {
            return false;
        }
        
        std::string rest = connection_string.substr(8);
        std::string username, password, host, database;
        int port = net::DEFAULT_PORT;
        
        // Parse User:Pass
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
        
        // Parse Database
        size_t slash_pos = rest.find('/');
        if (slash_pos != std::string::npos) {
            database = rest.substr(slash_pos + 1);
            rest = rest.substr(0, slash_pos);
        }
        
        // Parse Host:Port
        size_t colon_pos = rest.find(':');
        if (colon_pos != std::string::npos) {
            host = rest.substr(0, colon_pos);
            try {
                port = std::stoi(rest.substr(colon_pos + 1));
            } catch (...) { return false; }
        } else {
            host = rest;
        }
        
        if (host.empty()) return false;
        
        std::cout << "[INFO] Connecting to " << host << ":" << port << " as " << username << "..." << std::endl;
        
        return Connect(host, port, username, password, database);
    }

    std::string FrancoClient::Query(const std::string& sql) {
        if (!is_connected_) return "ERROR: Not connected.";

        MsgType type;
        switch (protocol_type_) {
            case ProtocolType::JSON:   type = MsgType::CMD_JSON; break;
            case ProtocolType::BINARY: type = MsgType::CMD_BINARY; break;
            default:                   type = MsgType::CMD_TEXT; break;
        }

        PacketHeader header;
        header.type = type;
        header.length = htonl(sql.size());

        if (send((socket_t)sock_, (char*)&header, sizeof(header), 0) < 0) {
            is_connected_ = false;
            return "ERROR: Connection Lost (Write)";
        }

        if (send((socket_t)sock_, sql.c_str(), sql.size(), 0) < 0) {
            is_connected_ = false;
            return "ERROR: Connection Lost (Write Payload)";
        }

        char buffer[net::MAX_PACKET_SIZE];
        int bytes = recv((socket_t)sock_, buffer, net::MAX_PACKET_SIZE, 0);
        
        if (bytes <= 0) {
            is_connected_ = false;
            return "ERROR: Server closed connection";
        }
        
        return std::string(buffer, bytes);
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