#pragma once

#include <string>
#include <cstdint>

namespace francodb {

    /**
     * The Data Types supported by FrancoDB internally.
     * The Parser will map "RAKAM" -> INTEGER, "ESM" -> VARCHAR.
     */
    enum class TypeId {
        INVALID = 0,
        BOOLEAN,   // Bool (1 byte)
        INTEGER,   // Int32 (4 bytes)
        BIGINT,    // Int64 (8 bytes)
        DECIMAL,   // Double (8 bytes)
        VARCHAR,   // Variable Length String
        TIMESTAMP  // Date/Time
    };

    /**
     * Singleton class to handle type operations (Comparison, Size, etc.)
     * For this project, we'll keep it simple and just use the Enum mostly.
     */
    class Type {
    public:
        static uint32_t GetTypeSize(TypeId type_id) {
            switch (type_id) {
                case TypeId::BOOLEAN: return 1;
                case TypeId::INTEGER: return 4;
                case TypeId::BIGINT: return 8;
                case TypeId::DECIMAL: return 8;
                case TypeId::TIMESTAMP: return 8;
                case TypeId::VARCHAR: return 0; // Variable length
                default: return 0;
            }
        }
    
        static std::string TypeToString(TypeId type_id) {
            switch (type_id) {
                case TypeId::BOOLEAN: return "BOOLEAN";
                case TypeId::INTEGER: return "INTEGER";
                case TypeId::VARCHAR: return "VARCHAR";
                case TypeId::DECIMAL: return "DECIMAL";
                case TypeId::TIMESTAMP: return "TIMESTAMP";
                default: return "UNKNOWN";
            }
        }
    };

} // namespace francodb