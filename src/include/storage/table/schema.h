#pragma once

#include <vector>
#include <string>
#include <cassert>
#include "storage/table/column.h"

namespace francodb {
    class Schema {
    public:
        Schema(const std::vector<Column> &columns) : columns_(columns) {
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

        const std::vector<Column> &GetColumns() const { return columns_; }
        const Column &GetColumn(uint32_t col_idx) const { return columns_[col_idx]; }
        uint32_t GetColumnCount() const { return columns_.size(); }
        uint32_t GetLength() const { return length_; }

        int GetColumnIndex(const std::string &col_name) const {
            for (size_t i = 0; i < columns_.size(); ++i) {
                if (columns_[i].GetName() == col_name) return i;
            }
            return -1;
        }
        
        int32_t GetColIdx(const std::string &col_name) const {
            for (uint32_t i = 0; i < columns_.size(); ++i) {
                if (columns_[i].GetName() == col_name) {
                    return i;
                }
            }
            return -1;
        }

    private:
        std::vector<Column> columns_;
        uint32_t length_; 
    };
}