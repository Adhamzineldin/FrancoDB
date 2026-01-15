#include "network/protocol.h"
#include "common/result_formatter.h"
#include <sstream>
#include <vector>
#include <cstring>
#include <cstdint>

namespace francodb {

    // ==========================================
    // HELPERS FOR BINARY PACKING (Big Endian)
    // ==========================================
    
    // Write 4-byte Integer
    void WriteInt32(std::string& buf, int32_t val) {
        uint32_t uval = static_cast<uint32_t>(val);
        buf.push_back((uval >> 24) & 0xFF);
        buf.push_back((uval >> 16) & 0xFF);
        buf.push_back((uval >> 8) & 0xFF);
        buf.push_back(uval & 0xFF);
    }

    // Write String: [Length (4 bytes)] + [Chars]
    void WriteString(std::string& buf, const std::string& str) {
        WriteInt32(buf, str.length());
        buf.append(str);
    }

    // Protocol Constants (Must match Python Client)
    const uint8_t RESP_MSG   = 0x01; // Simple Message
    const uint8_t RESP_TABLE = 0x02; // Table Data
    const uint8_t RESP_ERROR = 0xFF; // Error

    // ==========================================
    // EXISTING TEXT & JSON PROTOCOLS
    // ==========================================

    std::string TextProtocol::Serialize(const ExecutionResult &result) {
        if (!result.success) return "ERROR: " + result.message + "\n";
        if (result.result_set) return ResultFormatter::Format(result.result_set);
        return result.message + "\n";
    }

    std::string TextProtocol::SerializeError(const std::string &error) {
        return "ERROR: " + error + "\n";
    }

    std::string JsonProtocol::Serialize(const ExecutionResult &result) {
        std::ostringstream json;
        json << "{\n  \"success\": " << (result.success ? "true" : "false") << ",\n";
        if (result.result_set) {
            json << "  \"data\": {\n    \"columns\": [";
            for (size_t i = 0; i < result.result_set->column_names.size(); ++i) {
                json << "\"" << result.result_set->column_names[i] << "\"";
                if (i < result.result_set->column_names.size() - 1) json << ", ";
            }
            json << "],\n    \"rows\": [\n";
            for (size_t i = 0; i < result.result_set->rows.size(); ++i) {
                json << "      [";
                for (size_t j = 0; j < result.result_set->rows[i].size(); ++j) {
                    json << "\"" << result.result_set->rows[i][j] << "\"";
                    if (j < result.result_set->rows[i].size() - 1) json << ", ";
                }
                json << "]";
                if (i < result.result_set->rows.size() - 1) json << ",\n";
            }
            json << "\n    ]\n  },\n";
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

    // ==========================================
    // NEW BINARY PROTOCOL IMPLEMENTATION
    // ==========================================

    std::string BinaryProtocol::Serialize(const ExecutionResult &result) {
        // 1. Handle Logic Failures
        if (!result.success) {
            return SerializeError(result.message);
        }

        std::string buffer;

        // 2. Handle Result Set (SELECT)
        if (result.result_set) {
            buffer.push_back(RESP_TABLE); // Header Byte: 0x02

            const auto& rs = *result.result_set;

            // A. Metadata
            WriteInt32(buffer, rs.column_names.size()); // Num Columns
            WriteInt32(buffer, rs.rows.size());         // Num Rows

            // B. Column Definitions
            // Note: Since ExecutionResult stores strings, we default type to STRING (2)
            // In a full implementation, you'd check rs.column_types
            for (const auto& name : rs.column_names) {
                buffer.push_back(2); // Type: String
                WriteString(buffer, name);
            }

            // C. Row Data
            for (const auto& row : rs.rows) {
                for (const auto& cell : row) {
                    // Since we marked cols as String, we write String
                    WriteString(buffer, cell);
                }
            }
        } 
        // 3. Handle Simple Messages (INSERT/UPDATE/CREATE)
        else {
            buffer.push_back(RESP_MSG); // Header Byte: 0x01
            WriteString(buffer, result.message);
        }

        return buffer;
    }

    std::string BinaryProtocol::SerializeError(const std::string &error) {
        std::string buffer;
        buffer.push_back(RESP_ERROR); // Header Byte: 0xFF
        WriteString(buffer, error);
        return buffer;
    }

    ProtocolSerializer* CreateProtocol(ProtocolType type) {
        switch (type) {
            case ProtocolType::TEXT: return new TextProtocol();
            case ProtocolType::JSON: return new JsonProtocol();
            case ProtocolType::BINARY: return new BinaryProtocol();
            default: return new TextProtocol();
        }
    }

} // namespace francodb