#include "common/type.h"

namespace francodb {

uint32_t Type::GetTypeSize(TypeId type_id) {
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

std::string Type::TypeToString(TypeId type_id) {
    switch (type_id) {
        case TypeId::BOOLEAN: return "BOOLEAN";
        case TypeId::INTEGER: return "INTEGER";
        case TypeId::VARCHAR: return "VARCHAR";
        case TypeId::DECIMAL: return "DECIMAL";
        case TypeId::TIMESTAMP: return "TIMESTAMP";
        default: return "UNKNOWN";
    }
}

} // namespace francodb

