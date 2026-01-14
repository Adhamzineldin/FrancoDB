#include "storage/table/schema.h"
#include "storage/table/column.h"

namespace francodb {

Schema::Schema(const std::vector<Column> &columns) : columns_(columns) {
    uint32_t current_offset = 0;
    for (auto &col : columns_) {
        col.SetOffset(current_offset);
        if (col.GetType() == TypeId::VARCHAR) {
            current_offset += sizeof(uint32_t) * 2; // (Offset + Length)
        } else {
            current_offset += col.GetLength();
        }
    }
    length_ = current_offset;
}

int Schema::GetColumnIndex(const std::string &col_name) const {
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].GetName() == col_name) return i;
    }
    return -1;
}

int32_t Schema::GetColIdx(const std::string &col_name) const {
    for (uint32_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].GetName() == col_name) {
            return i;
        }
    }
    return -1;
}

} // namespace francodb

