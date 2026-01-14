#include "network/connection_handler.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "common/result_formatter.h"
#include <sstream>
#include <algorithm>

namespace francodb {

    // Base ConnectionHandler
    ConnectionHandler::ConnectionHandler(ProtocolType protocol_type, ExecutionEngine *engine)
        : protocol_(std::unique_ptr<ProtocolSerializer>(CreateProtocol(protocol_type))),
          engine_(engine) {
    }

    // ClientConnectionHandler (TEXT protocol)
    ClientConnectionHandler::ClientConnectionHandler(ExecutionEngine *engine)
        : ConnectionHandler(ProtocolType::TEXT, engine) {
    }

    std::string ClientConnectionHandler::ProcessRequest(const std::string &request) {
        std::string sql = request;
        // Clean up input
        sql.erase(std::remove(sql.begin(), sql.end(), '\n'), sql.end());
        sql.erase(std::remove(sql.begin(), sql.end(), '\r'), sql.end());
        
        if (sql == "exit" || sql == "quit") {
            return "Goodbye!\n";
        }

        try {
            Lexer lexer(sql);
            Parser parser(std::move(lexer));
            auto stmt = parser.ParseQuery();

            if (!stmt) {
                return "ERROR: Failed to parse query\n";
            }

            ExecutionResult res = engine_->Execute(stmt.get());
            
            if (!res.success) {
                return "ERROR: " + res.message + "\n";
            } else if (res.result_set) {
                return ResultFormatter::Format(res.result_set);
            } else {
                return res.message + "\n";
            }
        } catch (const std::exception &e) {
            return "SYSTEM ERROR: " + std::string(e.what()) + "\n";
        }
    }

    // ApiConnectionHandler (JSON protocol)
    ApiConnectionHandler::ApiConnectionHandler(ExecutionEngine *engine)
        : ConnectionHandler(ProtocolType::JSON, engine) {
    }

    std::string ApiConnectionHandler::ProcessRequest(const std::string &request) {
        // Simple JSON request parsing - expects {"query": "SELECT ..."}
        std::string sql;
        
        // Extract SQL from JSON-like request
        size_t query_pos = request.find("\"query\"");
        if (query_pos != std::string::npos) {
            size_t colon = request.find(':', query_pos);
            size_t start = request.find('"', colon);
            size_t end = request.find('"', start + 1);
            if (start != std::string::npos && end != std::string::npos) {
                sql = request.substr(start + 1, end - start - 1);
            }
        } else {
            // Fallback: treat entire request as SQL
            sql = request;
        }

        try {
            Lexer lexer(sql);
            Parser parser(std::move(lexer));
            auto stmt = parser.ParseQuery();

            if (!stmt) {
                return protocol_->SerializeError("Failed to parse query");
            }

            ExecutionResult res = engine_->Execute(stmt.get());
            return protocol_->Serialize(res);
        } catch (const std::exception &e) {
            return protocol_->SerializeError(std::string(e.what()));
        }
    }

    // BinaryConnectionHandler (BINARY protocol)
    BinaryConnectionHandler::BinaryConnectionHandler(ExecutionEngine *engine)
        : ConnectionHandler(ProtocolType::BINARY, engine) {
    }

    std::string BinaryConnectionHandler::ProcessRequest(const std::string &request) {
        // For binary protocol, we'll treat it as text for now
        // In a real implementation, you'd parse binary packets
        std::string sql = request;
        
        // Skip binary header if present
        if (!sql.empty() && (sql[0] == 0x01 || sql[0] == 0x02)) {
            sql = sql.substr(1);
        }

        try {
            Lexer lexer(sql);
            Parser parser(std::move(lexer));
            auto stmt = parser.ParseQuery();

            if (!stmt) {
                return protocol_->SerializeError("Failed to parse query");
            }

            ExecutionResult res = engine_->Execute(stmt.get());
            return protocol_->Serialize(res);
        } catch (const std::exception &e) {
            return protocol_->SerializeError(std::string(e.what()));
        }
    }

} // namespace francodb
