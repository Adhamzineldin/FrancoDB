#pragma once

#include <vector>
#include <string>
#include <cstring>
#include <iostream>
#include "common/type.h"
#include "common/exception.h"

namespace francodb {

class Value {
public:
    // Default Constructor
    Value() : type_id_(TypeId::INVALID), integer_(0) {}

    // Integer / Boolean / Timestamp Constructor
    Value(TypeId type, int32_t i) : type_id_(type), integer_(i) {}

    // Decimal Constructor (For KASR)
    Value(TypeId type, double d) : type_id_(type), decimal_(d) {}

    // String Constructor
    Value(TypeId type, const std::string &s) : type_id_(type) {
        string_val_ = s;
    }

    // --- RULE OF THREE (Fixes Compiler Error) ---
    
    // Copy Constructor
    Value(const Value &other) {
        CopyFrom(other);
    }

    // Assignment Operator
    Value &operator=(const Value &other) {
        if (this != &other) {
            CopyFrom(other);
        }
        return *this;
    }
    
    // Helper for copying
    void CopyFrom(const Value &other) {
        type_id_ = other.type_id_;
        if (type_id_ == TypeId::VARCHAR) {
            string_val_ = other.string_val_;
        } else if (type_id_ == TypeId::DECIMAL) {
            decimal_ = other.decimal_;
        } else {
            integer_ = other.integer_;
        }
    }

    // --- GETTERS (Fixes Parser Test) ---

    TypeId GetTypeId() const { return type_id_; }
    int32_t GetAsInteger() const { return integer_; }
    double GetAsDouble() const { return decimal_; }
    std::string GetAsString() const { return string_val_; }

    // --- SERIALIZATION (Fixes Tuple.cpp Error) ---

    // Write this value into a raw byte buffer (for Disk)
    void SerializeTo(char *dest) const {
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
                // For fixed-length char arrays in this simple engine
                // We just copy the string bytes.
                // Note: Tuple handles the length/offset management.
                std::memcpy(dest, string_val_.c_str(), string_val_.length());
                break;
            }
            default:
                break;
        }
    }

    // Read a value from a raw byte buffer (for Disk)
    static Value DeserializeFrom(const char *src, TypeId type) {
        switch (type) {
            case TypeId::INTEGER: {
                int32_t val;
                std::memcpy(&val, src, sizeof(int32_t));
                return Value(TypeId::INTEGER, val);
            }
            case TypeId::BOOLEAN: {
                int32_t val; // Bool is stored as int 0 or 1
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
                // In a real DB, we'd read the length prefix.
                // Here, Tuple passes us the pointer to the string data.
                // We assume null-termination or fixed length handling happens above.
                // For this simple implementation, let's treat it as a C-string.
                return Value(TypeId::VARCHAR, std::string(src));
            }
            default:
                return Value();
        }
    }

    friend std::ostream &operator<<(std::ostream &os, const Value &val) {
        if (val.type_id_ == TypeId::VARCHAR) os << val.string_val_;
        else if (val.type_id_ == TypeId::DECIMAL) os << val.decimal_;
        else os << val.integer_;
        return os;
    }

private:
    TypeId type_id_;
    union {
        int32_t integer_;
        double decimal_;
    };
    std::string string_val_;
};

} // namespace francodb