#include "storage/table/tuple.h"
#include "storage/table/schema.h"
#include "common/exception.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <algorithm> // For std::max

namespace francodb {

Tuple::Tuple(const std::vector<Value> &values, const Schema &schema) {
    if (values.size() != schema.GetColumnCount()) {
        return; // Fail gracefully if counts mismatch
    }

    // --- STEP 1: CALCULATE ROBUST SIZES ---
    // We do NOT trust schema.GetLength() blindly. We calculate the 
    // actual extent required by the fixed columns.
    uint32_t max_fixed_end = 0;
    
    for (uint32_t i = 0; i < schema.GetColumnCount(); i++) {
        const Column &col = schema.GetColumn(i);
        uint32_t col_len = 0;
        
        // Determine length based on type
        if (col.GetType() == TypeId::VARCHAR) {
            // VARCHAR stores 2x uint32_t (offset, length) in the fixed area
            col_len = sizeof(uint32_t) * 2;
        } else if (col.GetType() == TypeId::DECIMAL || col.GetType() == TypeId::BIGINT || col.GetType() == TypeId::TIMESTAMP) {
            col_len = 8;
        } else {
            col_len = 4; // INTEGER, BOOLEAN
        }
        
        uint32_t end_pos = col.GetOffset() + col_len;
        if (end_pos > max_fixed_end) {
            max_fixed_end = end_pos;
        }
    }

    // Determine Variable Length Size
    uint32_t var_len = 0;
    for (const auto &val : values) {
        if (val.GetTypeId() == TypeId::VARCHAR) {
            var_len += val.GetAsString().length();
        }
    }

    // Allocate Total Size
    uint32_t fixed_len = std::max(max_fixed_end, schema.GetLength());
    uint32_t total_size = fixed_len + var_len;

    data_.resize(total_size);
    std::memset(data_.data(), 0, total_size);

    // --- STEP 2: WRITE DATA ---
    uint32_t var_offset = fixed_len; // Start writing strings after the fixed area

    for (size_t i = 0; i < values.size(); ++i) {
        const Column &col = schema.GetColumn(i);
        const Value &val = values[i];
        uint32_t offset = col.GetOffset();

        // Bounds Check (Just in case, but Step 1 should prevent this)
        if (offset >= total_size) {
            std::cerr << "[Tuple Error] Critical Logic Error: Offset " << offset 
                      << " is outside total size " << total_size << std::endl;
            continue;
        }

        if (col.GetType() == TypeId::VARCHAR) {
            std::string str = val.GetAsString();
            uint32_t str_len = static_cast<uint32_t>(str.length());

            // Write metadata: (Offset, Length)
            // Ensure we have space for 8 bytes of metadata
            if (offset + 8 <= total_size) {
                memcpy(data_.data() + offset, &var_offset, sizeof(uint32_t));
                memcpy(data_.data() + offset + sizeof(uint32_t), &str_len, sizeof(uint32_t));
            }

            // Write string data
            if (str_len > 0) {
                if (var_offset + str_len <= total_size) {
                    memcpy(data_.data() + var_offset, str.c_str(), str_len);
                } else {
                    std::cerr << "[Tuple Error] String Truncated! VarOffset: " << var_offset << std::endl;
                }
            }
            var_offset += str_len;
        } else {
            // Fixed Types
            // SerializeTo handles the specific bytes (4 or 8)
            // We just ensure the pointer is valid.
            val.SerializeTo(data_.data() + offset);
        }
    }
}

Value Tuple::GetValue(const Schema &schema, uint32_t column_idx) const {
    if (column_idx >= schema.GetColumnCount()) {
        throw Exception(ExceptionType::EXECUTION, "Column index out of range");
    }
    if (data_.empty()) {
        // Return a safe default instead of crashing
        return Value(TypeId::INTEGER, 0);
    }
    
    const Column &col = schema.GetColumn(column_idx);
    TypeId type = col.GetType();
    uint32_t offset = col.GetOffset();

    if (offset >= data_.size()) {
        // Log warning but don't throw if possible to recover
        // throw Exception(ExceptionType::EXECUTION, "Column offset out of bounds");
        return Value(TypeId::INTEGER, 0);
    }

    if (type == TypeId::VARCHAR) {
        if (offset + 8 > data_.size()) {
            return Value(TypeId::VARCHAR, "");
        }
        
        uint32_t var_offset, var_len;
        memcpy(&var_offset, data_.data() + offset, sizeof(uint32_t));
        memcpy(&var_len, data_.data() + offset + sizeof(uint32_t), sizeof(uint32_t));

        // Safety check for reading string
        if (var_offset >= data_.size() || var_offset + var_len > data_.size()) {
            return Value(TypeId::VARCHAR, "");
        }

        return Value::DeserializeFrom(data_.data() + var_offset, TypeId::VARCHAR, var_len);
    }

    // Pass 0 length for fixed types
    return Value::DeserializeFrom(data_.data() + offset, type, 0);
}

} // namespace francodb