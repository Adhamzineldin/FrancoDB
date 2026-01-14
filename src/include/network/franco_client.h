// network/franco_client.h
#pragma once

#include <string>
#include <memory>

#include "common/franco_net_config.h"
#include "network/protocol.h"

namespace francodb {

    class FrancoClient {
    private:
        uintptr_t sock_{0};
        bool is_connected_{false};
        std::unique_ptr<ProtocolSerializer> protocol_;
        ProtocolType protocol_type_;

    public:
        explicit FrancoClient(ProtocolType protocol = ProtocolType::TEXT);
        ~FrancoClient();

        bool Connect(const std::string &ip = "127.0.0.1", int port = net::DEFAULT_PORT);
        std::string Query(const std::string &sql);
        void Disconnect();
        bool IsConnected() const { return is_connected_; }

        // For binary protocol
        void SendBinary(const std::vector<uint8_t> &data);
        std::vector<uint8_t> ReceiveBinary();
    };
}