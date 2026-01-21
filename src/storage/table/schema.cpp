#include "storage/table/schema.h"
#include "storage/table/column.h"
#include <iostream>

namespace francodb {

    Schema::Schema(const std::vector<Column> &columns) : columns_(columns) {
        uint32_t current_offset = 0;
    
        // std::cout << "[SCHEMA] Constructing schema with " << columns_.size() << " columns" << std::endl;
    
        for (size_t i = 0; i < columns_.size(); ++i) {
            columns_[i].SetOffset(current_offset);
        
            // std::cout << "[SCHEMA] Column " << i << " (" << columns_[i].GetName() << "): "
            //           << "offset=" << current_offset 
            //           << " type=" << static_cast<int>(columns_[i].GetType()) << std::endl;
        
            if (columns_[i].GetType() == TypeId::VARCHAR) {
                current_offset += sizeof(uint32_t) * 2; // (Offset + Length)
            } else {
                current_offset += columns_[i].GetLength();
            }
        }
    
        length_ = current_offset;
        // std::cout << "[SCHEMA] Total fixed length: " << length_ << std::endl;
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