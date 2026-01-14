// network/connection_handler.h
#pragma once

#include <memory>

#include "network/protocol.h"
#include "execution/execution_engine.h"
#include "common/session_context.h"
#include "common/auth_manager.h"

namespace francodb {

    class ConnectionHandler {
    protected:
        std::unique_ptr<ProtocolSerializer> protocol_;
        ExecutionEngine *engine_;
        std::shared_ptr<SessionContext> session_;
        AuthManager *auth_manager_;

    public:
        ConnectionHandler(ProtocolType protocol_type, ExecutionEngine *engine, AuthManager *auth_manager);
        virtual ~ConnectionHandler() = default;

        virtual std::string ProcessRequest(const std::string &request) = 0;
        virtual ProtocolType GetProtocolType() const = 0;

        std::shared_ptr<SessionContext> GetSession() const { return session_; }
    };

    class ClientConnectionHandler : public ConnectionHandler {
    public:
        explicit ClientConnectionHandler(ExecutionEngine *engine, AuthManager *auth_manager);
        std::string ProcessRequest(const std::string &request) override;
        ProtocolType GetProtocolType() const override { return ProtocolType::TEXT; }
    };

    class ApiConnectionHandler : public ConnectionHandler {
    public:
        explicit ApiConnectionHandler(ExecutionEngine *engine, AuthManager *auth_manager);
        std::string ProcessRequest(const std::string &request) override;
        ProtocolType GetProtocolType() const override { return ProtocolType::JSON; }
    };

    class BinaryConnectionHandler : public ConnectionHandler {
    public:
        explicit BinaryConnectionHandler(ExecutionEngine *engine, AuthManager *auth_manager);
        std::string ProcessRequest(const std::string &request) override;
        ProtocolType GetProtocolType() const override { return ProtocolType::BINARY; }
    };
}