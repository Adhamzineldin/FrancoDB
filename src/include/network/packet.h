#pragma once
#include <cstdint>

namespace francodb {

    // 1 Byte Message Type
    enum class MsgType : uint8_t {
        CMD_TEXT    = 'Q', // Standard SQL Query (Text response)
        CMD_JSON    = 'J', // JSON Query (JSON response)
        CMD_BINARY  = 'B', // Binary Query (Binary response)
        CMD_LOGIN   = 'L'  // Login Handshake
    };

    // 5 Byte Header
#pragma pack(push, 1) // Ensure no padding bytes
    struct PacketHeader {
        MsgType type;     // 1 byte
        uint32_t length;  // 4 bytes (size of the payload only)
    };
#pragma pack(pop)

}