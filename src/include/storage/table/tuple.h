#pragma once

#include <string>
#include <vector>
#include <cstring>
#include <sstream>

#include "common/rid.h"
#include "common/config.h"

namespace francodb {

    class Tuple {
    public:
        // Empty Constructor
        Tuple() = default;

        // Constructor with data
        Tuple(std::vector<char> data) : data_(std::move(data)) {}

        // Getters
        RID GetRid() const { return rid_; }
        void SetRid(RID rid) { rid_ = rid; }
    
        // Access raw data
        char *GetData() { return data_.data(); }
        const char *GetData() const { return data_.data(); }
    
        uint32_t GetLength() const { return data_.size(); }

        // Serialization (Copy data to a buffer)
        void SerializeTo(char *storage) const {
            memcpy(storage, data_.data(), data_.size());
        }

        // Deserialization (Read data from a buffer)
        void DeserializeFrom(const char *storage, uint32_t size) {
            data_.resize(size);
            memcpy(data_.data(), storage, size);
        }
    
        // Debug helper
        std::string ToString() const {
            std::stringstream ss;
            ss << "Tuple(RID=" << rid_.GetPageId() << ":" << rid_.GetSlotNum() 
               << ", Size=" << data_.size() << ")";
            return ss.str();
        }

    private:
        RID rid_;
        std::vector<char> data_;
    };

} // namespace francodb