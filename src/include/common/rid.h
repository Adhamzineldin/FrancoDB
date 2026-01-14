#pragma once

#include <cstdint>
#include <sstream>
#include <functional>

#include "common/config.h"

namespace francodb {

    class RID {
    public:
        RID();
        RID(page_id_t page_id, uint32_t slot_num);
        page_id_t GetPageId() const { return page_id_; }
        uint32_t GetSlotId() const { return slot_num_; }
        void Set(page_id_t page_id, uint32_t slot_num);
        bool operator==(const RID &other) const;
        std::string ToString() const;

    private:
        page_id_t page_id_;
        uint32_t slot_num_; // Which row number inside the page?
    };

}

// Hash function specialization for RID (required for std::unordered_map)
namespace std {
    template<>
    struct hash<francodb::RID> {
        std::size_t operator()(const francodb::RID &rid) const noexcept {
            // Combine page_id and slot_num into a hash
            std::size_t h1 = std::hash<francodb::page_id_t>{}(rid.GetPageId());
            std::size_t h2 = std::hash<uint32_t>{}(rid.GetSlotId());
            return h1 ^ (h2 << 1);
        }
    };
} // namespace francodb