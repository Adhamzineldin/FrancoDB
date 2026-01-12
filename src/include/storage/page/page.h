#pragma once

#include <cstring>
#include <iostream>
#include <vector>
#include "common/config.h"

// --- WINDOWS COMPATIBILITY FIX ---
// The Windows API defines a macro called "GetFreeSpace" which conflicts 
// with our function name. We must undefine it to avoid syntax errors.
#ifdef GetFreeSpace
#undef GetFreeSpace
#endif
// ---------------------------------

namespace francodb {

/**
 * The Header sits at the top of every Page.
 * It tracks the "Free Space" in the middle (Slotted Page Architecture).
 */
struct PageHeader {
    uint32_t page_id = INVALID_PAGE_ID;
    uint32_t lsn = 0;             // Log Sequence Number (Recovery)
    uint16_t checksum = 0;        // CRC32 (Corruption detection)
    uint16_t flags = 0;           // 0=DATA, 1=INDEX
    uint16_t lower_offset = 0;    // Starts after the header, grows down
    uint16_t upper_offset = PAGE_SIZE; // Starts at end of page, grows up
    uint16_t slot_count = 0;      // Number of records
};

/**
 * A simple slot structure.
 * It points to where the actual data lives in the "bottom" of the page.
 */
struct Slot {
    uint16_t offset;
    uint16_t length;
};

/**
 * Represents a row of data.
 * For now, we treat data as a raw byte array.
 */
struct Tuple {
    std::vector<char> data;
    
    // Helper to create a tuple from a string
    static Tuple FromString(const std::string& str) {
        return Tuple{std::vector<char>(str.begin(), str.end())};
    }
};

/**
 * Page Class
 * Wraps a raw 4KB memory block and provides helper methods to act as a Slotted Page.
 */
class Page {
public:
    Page() { ResetMemory(); }
    ~Page() = default;

    // --- Accessors ---
    
    inline char *GetData() { return data_; }
    inline uint32_t GetPageId() { return GetHeader()->page_id; }
    inline uint32_t GetPinCount() { return pin_count_; }
    inline bool IsDirty() { return is_dirty_; }
    
    // --- Buffer Pool Helpers ---
    
    inline void SetPageId(uint32_t page_id) { GetHeader()->page_id = page_id; }
    inline void IncrementPinCount() { pin_count_++; }
    inline void DecrementPinCount() { if(pin_count_ > 0) pin_count_--; }
    inline void SetDirty(bool dirty) { is_dirty_ = dirty; }

    // --- Slotted Page Logic (The "Franco" Special) ---

    /**
     * Initialize a new page. Sets offsets to empty state.
     */
    void Init(uint32_t page_id) {
        ResetMemory();
        auto *header = GetHeader();
        header->page_id = page_id;
        header->lower_offset = sizeof(PageHeader); 
        header->upper_offset = PAGE_SIZE;
        header->slot_count = 0;
        
        // Reset metadata
        pin_count_ = 0;
        is_dirty_ = false;
    }

    /**
     * @return The remaining free space in the "hole" between slots and data.
     */
    uint16_t GetFreeSpace() const {
        auto *header = reinterpret_cast<const PageHeader *>(data_);
        // If lower > upper, we are corrupted or full beyond logic
        if (header->lower_offset > header->upper_offset) return 0;
        return header->upper_offset - header->lower_offset;
    }

    /**
     * Insert a tuple into the page.
     * @return true if it fits, false if page is full.
     */
    bool InsertTuple(const Tuple &tuple) {
        auto *header = GetHeader();
        uint16_t size = tuple.data.size();
        
        // We need space for the Data + the Slot (4 bytes: offset/len)
        if (GetFreeSpace() < size + sizeof(Slot)) {
            return false; // Not enough space
        }

        // 1. Calculate where to put the data (grow from bottom up)
        uint16_t data_start = header->upper_offset - size;
        
        // 2. Write the data
        std::memcpy(data_ + data_start, tuple.data.data(), size);
        
        // 3. Create the slot (grow from top down)
        Slot *slot_array = reinterpret_cast<Slot *>(data_ + sizeof(PageHeader));
        slot_array[header->slot_count] = Slot{data_start, size};

        // 4. Update Header
        header->slot_count++;
        header->upper_offset = data_start; // Move upper boundary down
        header->lower_offset += sizeof(Slot); // Move lower boundary up

        return true;
    }
    
    /**
     * Read a tuple from a slot index.
     */
    bool GetTuple(uint16_t slot_id, Tuple &out_tuple) {
        auto *header = GetHeader();
        if (slot_id >= header->slot_count) return false;

        Slot *slot_array = reinterpret_cast<Slot *>(data_ + sizeof(PageHeader));
        Slot slot = slot_array[slot_id];

        out_tuple.data.resize(slot.length);
        std::memcpy(out_tuple.data.data(), data_ + slot.offset, slot.length);
        
        return true;
    }

private:
    // Helper to interpret raw bytes as header
    inline PageHeader *GetHeader() {
        return reinterpret_cast<PageHeader *>(data_);
    }

    inline void ResetMemory() {
        std::memset(data_, 0, PAGE_SIZE);
    }

    // Actual Data Storage
    char data_[PAGE_SIZE]{};

    // Metadata (Not stored on disk, only in RAM for Buffer Pool)
    uint32_t pin_count_ = 0;
    bool is_dirty_ = false;
    // We will add the ReaderWriterLatch here in Phase 2
};

} // namespace francodb