#pragma once

#include <vector>
#include <string>
#include <cstring>
#include <iostream>
#include <sstream>

#include "common/type.h"
#include "common/exception.h"

namespace francodb {
    class Value {
    public:
        Value();

        Value(TypeId type, int32_t i);

        Value(TypeId type, double d);

        Value(TypeId type, const std::string &s);

        Value(const Value &other);

        Value &operator=(const Value &other);

        void CopyFrom(const Value &other);

        TypeId GetTypeId() const { return type_id_; }
        int32_t GetAsInteger() const { return integer_; }
        double GetAsDouble() const { return decimal_; }

        std::string GetAsString() const {
            // Return human-readable for BOOLEAN
            if (type_id_ == TypeId::BOOLEAN) {
                return integer_ != 0 ? std::string("true") : std::string("false");
            }
            return string_val_;
        }

        void SerializeTo(char *dest) const;

        static Value DeserializeFrom(const char *src, TypeId type, uint32_t length = 0);

        friend std::ostream &operator<<(std::ostream &os, const Value &val);
        bool CompareEquals(const Value &other) const;
        
        std::string ToString() const {
            std::ostringstream oss;
            // Assuming you already have operator<< overloaded for Value
            // If not, you might need to implement a switch(type_id_) here.
            oss << (*this); 
            return oss.str();
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
