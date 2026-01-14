#include "network/protocol.h"
#include "common/result_formatter.h"
#include <sstream>
#include <iomanip>

namespace francodb {

    // TextProtocol implementation
    std::string TextProtocol::Serialize(const ExecutionResult &result) {
        if (!result.success) {
            return "ERROR: " + result.message + "\n";
        }
        
        if (result.result_set) {
            return ResultFormatter::Format(result.result_set);
        }
        
        return result.message + "\n";
    }

    std::string TextProtocol::SerializeError(const std::string &error) {
        return "ERROR: " + error + "\n";
    }

    // JsonProtocol implementation
    std::string JsonProtocol::Serialize(const ExecutionResult &result) {
        std::ostringstream json;
        json << "{\n";
        json << "  \"success\": " << (result.success ? "true" : "false") << ",\n";
        
        if (result.result_set) {
            json << "  \"data\": {\n";
            json << "    \"columns\": [";
            for (size_t i = 0; i < result.result_set->column_names.size(); ++i) {
                json << "\"" << result.result_set->column_names[i] << "\"";
                if (i < result.result_set->column_names.size() - 1) json << ", ";
            }
            json << "],\n";
            json << "    \"rows\": [\n";
            for (size_t i = 0; i < result.result_set->rows.size(); ++i) {
                json << "      [";
                for (size_t j = 0; j < result.result_set->rows[i].size(); ++j) {
                    json << "\"" << result.result_set->rows[i][j] << "\"";
                    if (j < result.result_set->rows[i].size() - 1) json << ", ";
                }
                json << "]";
                if (i < result.result_set->rows.size() - 1) json << ",\n";
            }
            json << "\n    ]\n";
            json << "  },\n";
            json << "  \"row_count\": " << result.result_set->rows.size() << "\n";
        } else {
            json << "  \"message\": \"" << result.message << "\"\n";
        }
        
        json << "}\n";
        return json.str();
    }

    std::string JsonProtocol::SerializeError(const std::string &error) {
        return "{\n  \"success\": false,\n  \"error\": \"" + error + "\"\n}\n";
    }

    // BinaryProtocol implementation (simplified - returns text for now)
    std::string BinaryProtocol::Serialize(const ExecutionResult &result) {
        // In a real binary protocol, you'd serialize to binary format
        // For now, we'll use text as a fallback
        if (!result.success) {
            return "ERROR: " + result.message + "\n";
        }
        
        if (result.result_set) {
            return ResultFormatter::Format(result.result_set);
        }
        
        return result.message + "\n";
    }

    std::string BinaryProtocol::SerializeError(const std::string &error) {
        return "ERROR: " + error + "\n";
    }

    // Factory function
    ProtocolSerializer* CreateProtocol(ProtocolType type) {
        switch (type) {
            case ProtocolType::TEXT:
                return new TextProtocol();
            case ProtocolType::JSON:
                return new JsonProtocol();
            case ProtocolType::BINARY:
                return new BinaryProtocol();
            default:
                return new TextProtocol();
        }
    }

} // namespace francodb
