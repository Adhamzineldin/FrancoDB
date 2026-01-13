#pragma once

#include <cstring>
#include <iostream>
#include <vector>
#include "common/config.h"

namespace francodb {

    /**
     * Page Class
     * The generic container for a 4KB block of memory.
     */
    class Page {
        // BufferPoolManager needs access to private members
        friend class BufferPoolManager;

    public:
        Page() { ResetMemory(); }
        ~Page() = default;

        // --- Accessors ---
        inline char *GetData() { return data_; }
        inline page_id_t GetPageId() { return page_id_; }
        inline int GetPinCount() { return pin_count_; }
        inline bool IsDirty() { return is_dirty_; }
    
        // --- Mutators ---
        inline void SetDirty(bool dirty) { is_dirty_ = dirty; }

        // --- Buffer Pool Helpers (RESTORED) ---
    
        inline void IncrementPinCount() { pin_count_++; }
    
        inline void DecrementPinCount() { 
            if (pin_count_ > 0) pin_count_--; 
        }

        // Initialize the page metadata (Used when BPM assigns a new slot)
        inline void Init(page_id_t page_id) {
            ResetMemory();
            page_id_ = page_id;
            pin_count_ = 0;
            is_dirty_ = false;
        }

        inline void ResetMemory() {
            std::memset(data_, 0, PAGE_SIZE);
        }

    protected:
        static_assert(sizeof(page_id_t) == 4);
    
        // 1. The actual Data (4KB) - Written to Disk
        char data_[PAGE_SIZE]{};

        // 2. Metadata (RAM Only) - Managed by Buffer Pool
        page_id_t page_id_ = INVALID_PAGE_ID;
        int pin_count_ = 0;
        bool is_dirty_ = false;
    };

} // namespace francodb