#include "storage/table/tuple.h"
#include "storage/table/schema.h"
#include "common/exception.h"
#include <cassert>
#include <cstring>

namespace francodb {

Tuple::Tuple(const std::vector<Value> &values, const Schema &schema) {
    assert(values.size() == schema.GetColumnCount());

    // 1. Calculate Total Size
    uint32_t tuple_size = schema.GetLength();  // Fixed part
    for (const auto &val : values) {
        if (val.GetTypeId() == TypeId::VARCHAR) {
            tuple_size += val.GetAsString().length();
        }
    }

    // 2. Allocate Memory
    data_.resize(tuple_size);
    std::fill(data_.begin(), data_.end(), 0);

    // 3. Write Data
    uint32_t var_offset = schema.GetLength();  // Start of variable data

    for (size_t i = 0; i < values.size(); ++i) {
        const Column &col = schema.GetColumn(i);
        const Value &val = values[i];
        uint32_t offset = col.GetOffset();

        if (col.GetType() == TypeId::VARCHAR) {
            // VARCHAR: Write (offset, length) pair in fixed area
            std::string str = val.GetAsString();
            uint32_t str_len = static_cast<uint32_t>(str.length());

            // Write offset and length
            memcpy(data_.data() + offset, &var_offset, sizeof(uint32_t));
            memcpy(data_.data() + offset + sizeof(uint32_t), &str_len, sizeof(uint32_t));

            // Write actual string data
            if (str_len > 0) {
                memcpy(data_.data() + var_offset, str.c_str(), str_len);
            }
            var_offset += str_len;
        } else {
            // Fixed-size types: write directly
            val.SerializeTo(data_.data() + offset);
        }
    }
}

Value Tuple::GetValue(const Schema &schema, uint32_t column_idx) const {
    if (column_idx >= schema.GetColumnCount()) {
        throw Exception(ExceptionType::EXECUTION, "Column index out of range");
    }
    if (data_.empty()) {
        throw Exception(ExceptionType::EXECUTION, "Tuple data is empty");
    }
    
    const Column &col = schema.GetColumn(column_idx);
    TypeId type = col.GetType();
    uint32_t offset = col.GetOffset();

    if (offset >= data_.size()) {
        throw Exception(ExceptionType::EXECUTION, 
            "Column offset " + std::to_string(offset) + 
            " >= data size " + std::to_string(data_.size()));
    }

    if (type == TypeId::VARCHAR) {
        // Read (offset, length) from fixed area
        if (offset + 8 > data_.size()) {
            throw Exception(ExceptionType::EXECUTION, "VARCHAR metadata out of range");
        }
        
        uint32_t var_offset, var_len;
        memcpy(&var_offset, data_.data() + offset, sizeof(uint32_t));
        memcpy(&var_len, data_.data() + offset + sizeof(uint32_t), sizeof(uint32_t));

        // Validate
        if (var_offset >= data_.size() || var_offset + var_len > data_.size()) {
            throw Exception(ExceptionType::EXECUTION, 
                "VARCHAR data out of range: offset=" + std::to_string(var_offset) +
                " len=" + std::to_string(var_len) + 
                " data_size=" + std::to_string(data_.size()));
        }

        std::string val_str(data_.data() + var_offset, var_len);
        return Value(TypeId::VARCHAR, val_str);
    }

    // Fixed types
    return Value::DeserializeFrom(data_.data() + offset, type);
}

} // namespace francodb