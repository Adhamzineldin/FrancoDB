#pragma once

#include <cstdint>
#include <sstream>

#include "common/config.h"

namespace francodb {

    class RID {
    public:
        // Default constructor (Invalid RID)
        RID() : page_id_(INVALID_PAGE_ID), slot_num_(-1) {}

        // Constructor with values
        RID(page_id_t page_id, uint32_t slot_num) 
            : page_id_(page_id), slot_num_(slot_num) {}

        page_id_t GetPageId() const { return page_id_; }
        uint32_t GetSlotId() const { return slot_num_; }

        void Set(page_id_t page_id, uint32_t slot_num) {
            page_id_ = page_id;
            slot_num_ = slot_num;
        }

        // Overload equality operator so we can compare RIDs
        bool operator==(const RID &other) const {
            return page_id_ == other.page_id_ && slot_num_ == other.slot_num_;
        }
    
        // For debugging
        std::string ToString() const {
            std::stringstream os;
            os << "RID(" << page_id_ << ", " << slot_num_ << ")";
            return os.str();
        }

    private:
        page_id_t page_id_;
        uint32_t slot_num_; // Which row number inside the page?
    };

} // namespace francodb