#pragma once

#include <vector>
#include <string>
#include <cassert>
#include "storage/table/column.h"

namespace francodb {
    class Schema {
    public:
        Schema(const std::vector<Column> &columns);
        const std::vector<Column> &GetColumns() const { return columns_; }
        const Column &GetColumn(uint32_t col_idx) const { return columns_[col_idx]; }
        uint32_t GetColumnCount() const { return columns_.size(); }
        uint32_t GetLength() const { return length_; }
        int GetColumnIndex(const std::string &col_name) const;
        int32_t GetColIdx(const std::string &col_name) const;

    private:
        std::vector<Column> columns_;
        uint32_t length_; 
    };
}