#pragma once

#include <cstring>
#include <iostream>
#include <vector>
#include "common/config.h"
#include "common/rwlatch.h" // <--- 1. Include the Latch Definition

namespace francodb {

    /**
     * Page Class
     * The generic container for a 4KB block of memory.
     * NOW THREAD-SAFE!
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

        // --- Buffer Pool Helpers ---
        inline void IncrementPinCount() { pin_count_++; }
        inline void DecrementPinCount() { 
            if (pin_count_ > 0) pin_count_--; 
        }

        inline void Init(page_id_t page_id) {
            ResetMemory();
            page_id_ = page_id;
            pin_count_ = 0;
            is_dirty_ = false;
        }

        inline void ResetMemory() {
            // Only zero out after the checksum (first 4 bytes)
            // But for page 0 (meta), zero the whole page
            // Also, for INVALID_PAGE_ID, zero the whole page (newly allocated pages)
            if (page_id_ == 0 || page_id_ == INVALID_PAGE_ID) {
                std::memset(data_, 0, PAGE_SIZE);
            } else {
                // Always zero the checksum field as well for new pages
                std::memset(data_, 0, 4);
                std::memset(data_ + 4, 0, PAGE_SIZE - 4);
            }
        }

        // --- NEW: LATCHING METHODS ---
        // These allow threads to lock THIS specific page
        
        /** Acquire the page for READING (Multiple threads allowed) */
        inline void RLock() { rwlatch_.RLock(); }
        
        /** Release the reading lock */
        inline void RUnlock() { rwlatch_.RUnlock(); }
        
        /** Acquire the page for WRITING (Exclusive access) */
        inline void WLock() { rwlatch_.WLock(); }
        
        /** Release the writing lock */
        inline void WUnlock() { rwlatch_.WUnlock(); }

    protected:
        static_assert(sizeof(page_id_t) == 4);
    
        // 1. The actual Data (4KB) - Written to Disk
        char data_[PAGE_SIZE]{};

        // 2. Metadata (RAM Only)
        page_id_t page_id_ = INVALID_PAGE_ID;
        int pin_count_ = 0;
        bool is_dirty_ = false;
        
        // 3. The Latch (RAM Only)
        // This protects the content of 'data_' from race conditions.
        ReaderWriterLatch rwlatch_; 
    };

} // namespace francodb