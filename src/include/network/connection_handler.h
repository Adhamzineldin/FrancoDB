// network/connection_handler.h
#pragma once

#include <memory>

#include "network/protocol.h"
#include "execution/execution_engine.h"

namespace francodb {

    class ConnectionHandler {
    protected:
        std::unique_ptr<ProtocolSerializer> protocol_;
        ExecutionEngine *engine_;

    public:
        ConnectionHandler(ProtocolType protocol_type, ExecutionEngine *engine);
        virtual ~ConnectionHandler() = default;

        virtual std::string ProcessRequest(const std::string &request) = 0;
        virtual ProtocolType GetProtocolType() const = 0;
    };

    class ClientConnectionHandler : public ConnectionHandler {
    public:
        explicit ClientConnectionHandler(ExecutionEngine *engine);
        std::string ProcessRequest(const std::string &request) override;
        ProtocolType GetProtocolType() const override { return ProtocolType::TEXT; }
    };

    class ApiConnectionHandler : public ConnectionHandler {
    public:
        explicit ApiConnectionHandler(ExecutionEngine *engine);
        std::string ProcessRequest(const std::string &request) override;
        ProtocolType GetProtocolType() const override { return ProtocolType::JSON; }
    };

    class BinaryConnectionHandler : public ConnectionHandler {
    public:
        explicit BinaryConnectionHandler(ExecutionEngine *engine);
        std::string ProcessRequest(const std::string &request) override;
        ProtocolType GetProtocolType() const override { return ProtocolType::BINARY; }
    };
}