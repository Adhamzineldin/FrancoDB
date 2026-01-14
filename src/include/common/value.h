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
    std::string GetAsString() const { return string_val_; }
    void SerializeTo(char *dest) const;
    static Value DeserializeFrom(const char *src, TypeId type);
    friend std::ostream &operator<<(std::ostream &os, const Value &val);

private:
    TypeId type_id_;
    union {
        int32_t integer_;
        double decimal_;
    };
    std::string string_val_;
};

} // namespace francodb