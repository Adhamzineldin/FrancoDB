#include "storage/table/schema.h"
#include "storage/table/column.h"
#include <iostream>
#include <vector>

namespace francodb {

Schema::Schema(const std::vector<Column> &columns) : columns_(columns) {
    uint32_t current_offset = 0;

    for (size_t i = 0; i < columns_.size(); ++i) {
        Column &col = columns_[i];
        
        // 1. Set the offset for this column
        // Ensure SetOffset exists in column.h, or allow Schema to access private via friend
        col.SetOffset(current_offset);

        // 2. [CRITICAL FIX] Calculate size manually. 
        // Do NOT trust col.GetLength() because the Parser sets it to 0 for Integers.
        uint32_t column_size = 0;
        switch (col.GetType()) {
            case TypeId::BOOLEAN:
            case TypeId::INTEGER:
                column_size = sizeof(int32_t); // Always 4 bytes
                break;
            case TypeId::BIGINT:
            case TypeId::TIMESTAMP:
            case TypeId::DECIMAL:
                column_size = sizeof(int64_t); // Always 8 bytes
                break;
            case TypeId::VARCHAR:
                // VARCHAR stores metadata (Offset + Length) in fixed area
                column_size = sizeof(uint32_t) * 2; // 8 bytes
                break;
            default:
                column_size = sizeof(int32_t); // Default safety
                break;
        }

        current_offset += column_size;
    }

    length_ = current_offset;
    
    // Debug print to confirm fix (Expect ~20 bytes for your test case, NOT 0)
    // std::cout << "[DEBUG] Schema calculated total length: " << length_ << std::endl;
}

int Schema::GetColumnIndex(const std::string &col_name) const {
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].GetName() == col_name) return static_cast<int>(i);
    }
    return -1;
}

int32_t Schema::GetColIdx(const std::string &col_name) const {
    for (uint32_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].GetName() == col_name) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

} // namespace francodb