// network/protocol.h
#pragma once
#include <memory>
#include <string>
#include "execution/execution_result.h"

namespace francodb {
    enum class ProtocolType {
        TEXT,    // Plain text for CLI/human
        JSON,    // JSON for web APIs
        BINARY   // Binary for high-performance clients
    };

    class ProtocolSerializer {
    public:
        virtual ~ProtocolSerializer() = default;
        virtual std::string Serialize(const ExecutionResult& result) = 0;
        virtual std::string SerializeError(const std::string& error) = 0;
    };

    class TextProtocol : public ProtocolSerializer {
    public:
        std::string Serialize(const ExecutionResult& result) override;
        std::string SerializeError(const std::string& error) override;
    };

    class JsonProtocol : public ProtocolSerializer {
    public:
        std::string Serialize(const ExecutionResult& result) override;
        std::string SerializeError(const std::string& error) override;
    };

    class BinaryProtocol : public ProtocolSerializer {
    public:
        std::string Serialize(const ExecutionResult& result) override;
        std::string SerializeError(const std::string& error) override;
    };

    ProtocolSerializer* CreateProtocol(ProtocolType type);
}