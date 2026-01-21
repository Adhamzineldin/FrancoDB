#include "common/value.h"
#include <cstring>
#include <iostream>
#include <cstdint>

namespace francodb {

Value::Value() : type_id_(TypeId::INVALID), integer_(0) {}

Value::Value(TypeId type, int32_t i) : type_id_(type), integer_(i) {}

Value::Value(TypeId type, double d) : type_id_(type), decimal_(d) {}

Value::Value(TypeId type, const std::string &s) : type_id_(type) {
    string_val_ = s;
}

Value::Value(const Value &other) {
    CopyFrom(other);
}

Value &Value::operator=(const Value &other) {
    if (this != &other) {
        CopyFrom(other);
    }
    return *this;
}

void Value::CopyFrom(const Value &other) {
    type_id_ = other.type_id_;
    if (type_id_ == TypeId::VARCHAR) {
        string_val_ = other.string_val_;
    } else if (type_id_ == TypeId::DECIMAL) {
        decimal_ = other.decimal_;
    } else {
        integer_ = other.integer_;
    }
}

void Value::SerializeTo(char *dest) const {
    switch (type_id_) {
        case TypeId::INTEGER: {
            std::memcpy(dest, &integer_, sizeof(int32_t));
            break;
        }
        case TypeId::BOOLEAN: {
            uint8_t b = static_cast<uint8_t>(integer_ != 0);
            std::memcpy(dest, &b, sizeof(uint8_t));
            break;
        }
        case TypeId::BIGINT: {
            int64_t val = static_cast<int64_t>(integer_);
            std::memcpy(dest, &val, sizeof(int64_t));
            break;
        }
        case TypeId::TIMESTAMP: {
            int64_t val = static_cast<int64_t>(integer_);
            std::memcpy(dest, &val, sizeof(int64_t));
            break;
        }
        case TypeId::DECIMAL: {
            std::memcpy(dest, &decimal_, sizeof(double));
            break;
        }
        case TypeId::VARCHAR: {
            // VARCHAR handled by Tuple (offset/length + raw bytes). Avoid writing here.
            // No-op for standalone Value serialization of VARCHAR.
            break;
        }
        default:
            break;
    }
}

Value Value::DeserializeFrom(const char *src, TypeId type) {
    switch (type) {
        case TypeId::INTEGER: {
            int32_t val;
            std::memcpy(&val, src, sizeof(int32_t));
            return Value(TypeId::INTEGER, val);
        }
        case TypeId::BOOLEAN: {
            uint8_t b;
            std::memcpy(&b, src, sizeof(uint8_t));
            return Value(TypeId::BOOLEAN, static_cast<int32_t>(b));
        }
        case TypeId::BIGINT: {
            int64_t val;
            std::memcpy(&val, src, sizeof(int64_t));
            // Store into integer_ for simplicity (may truncate if > int32 range)
            return Value(TypeId::BIGINT, static_cast<int32_t>(val));
        }
        case TypeId::TIMESTAMP: {
            int64_t val;
            std::memcpy(&val, src, sizeof(int64_t));
            return Value(TypeId::TIMESTAMP, static_cast<int32_t>(val));
        }
        case TypeId::DECIMAL: {
            double val;
            std::memcpy(&val, src, sizeof(double));
            return Value(TypeId::DECIMAL, val);
        }
        case TypeId::VARCHAR: {
            // For tuple data, use Tuple::GetValue which knows length.
            // Fallback: treat src as null-terminated.
            return Value(TypeId::VARCHAR, std::string(src));
        }
        default:
            return Value();
    }
}

std::ostream &operator<<(std::ostream &os, const Value &val) {
    if (val.type_id_ == TypeId::VARCHAR) {
        os << val.string_val_;
    } else if (val.type_id_ == TypeId::DECIMAL) {
        os << val.decimal_;
    } else if (val.type_id_ == TypeId::INTEGER || val.type_id_ == TypeId::BIGINT || 
               val.type_id_ == TypeId::TIMESTAMP || val.type_id_ == TypeId::BOOLEAN) {
        os << val.integer_;
    } else {
        os << "<INVALID>";
    }
    return os;
}

} // namespace francodb

