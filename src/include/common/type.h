#pragma once

#include <string>
#include <cstdint>

namespace francodb {

    /**
     * The Data Types supported by FrancoDB internally.
     * The Parser will map "RAKAM" -> INTEGER, "GOMLA" -> VARCHAR.
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
        static uint32_t GetTypeSize(TypeId type_id);
        static std::string TypeToString(TypeId type_id);
    };

} // namespace francodb