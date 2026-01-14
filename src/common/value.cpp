#include "common/value.h"
#include <cstring>
#include <iostream>

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
        case TypeId::INTEGER:
        case TypeId::BOOLEAN:
        case TypeId::TIMESTAMP:
            std::memcpy(dest, &integer_, sizeof(int32_t));
            break;
        case TypeId::DECIMAL:
            std::memcpy(dest, &decimal_, sizeof(double));
            break;
        case TypeId::VARCHAR: {
            std::memcpy(dest, string_val_.c_str(), string_val_.length());
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
            int32_t val;
            std::memcpy(&val, src, sizeof(int32_t));
            return Value(TypeId::BOOLEAN, val);
        }
        case TypeId::TIMESTAMP: {
            int32_t val;
            std::memcpy(&val, src, sizeof(int32_t));
            return Value(TypeId::TIMESTAMP, val);
        }
        case TypeId::DECIMAL: {
            double val;
            std::memcpy(&val, src, sizeof(double));
            return Value(TypeId::DECIMAL, val);
        }
        case TypeId::VARCHAR: {
            return Value(TypeId::VARCHAR, std::string(src));
        }
        default:
            return Value();
    }
}

std::ostream &operator<<(std::ostream &os, const Value &val) {
    if (val.type_id_ == TypeId::VARCHAR) os << val.string_val_;
    else if (val.type_id_ == TypeId::DECIMAL) os << val.decimal_;
    else os << val.integer_;
    return os;
}

} // namespace francodb

