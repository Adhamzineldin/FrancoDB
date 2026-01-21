#include "storage/table/table_page.h"
#include "common/exception.h"
#include "concurrency/transaction.h"

namespace francodb {
    void TablePage::Init(page_id_t page_id, page_id_t prev_id, page_id_t next_id, Transaction *txn) {
        (void) txn;

        page_id_ = page_id;

        // Get data pointer and verify it's valid
        uint8_t *data = reinterpret_cast<uint8_t*>(GetData());
        if (data == nullptr) {
            return; // Cannot initialize null page
        }

        // CRITICAL: Zero the entire header starting after checksum
        // Header layout: Checksum(4) | PrevPageId(4) | NextPageId(4) | FreeSpacePtr(4) | TupleCount(4)
        // We skip checksum and zero the rest
        size_t header_start = OFFSET_CHECKSUM + sizeof(page_id_t); // Start after checksum
        size_t header_size = SIZE_HEADER - header_start;  // Size of fields to zero
        
        // Validate bounds before memset
        if (header_start + header_size > SIZE_HEADER) {
            return; // Safety check
        }
        
        memset(data + header_start, 0, header_size);

        // Write Header Fields
        memcpy(data + OFFSET_PREV_PAGE, &prev_id, sizeof(page_id_t));
        memcpy(data + OFFSET_NEXT_PAGE, &next_id, sizeof(page_id_t));

        uint32_t free_ptr = PAGE_SIZE;
        SetFreeSpacePointer(free_ptr);
        SetTupleCount(0);
        
        // Mark page as dirty since we modified it - this ensures checksum gets recalculated on flush
        SetDirty(true);
    }

    // --- Getters / Setters ---
    page_id_t TablePage::GetNextPageId() {
        uint8_t *data = reinterpret_cast<uint8_t*>(GetData());
        if (data == nullptr) return INVALID_PAGE_ID;
        return *reinterpret_cast<page_id_t *>(data + OFFSET_NEXT_PAGE);
    }

    void TablePage::SetNextPageId(page_id_t next_page_id) {
        uint8_t *data = reinterpret_cast<uint8_t*>(GetData());
        if (data == nullptr) return;
        memcpy(data + OFFSET_NEXT_PAGE, &next_page_id, sizeof(page_id_t));
    }

    page_id_t TablePage::GetPrevPageId() {
        uint8_t *data = reinterpret_cast<uint8_t*>(GetData());
        if (data == nullptr) return INVALID_PAGE_ID;
        return *reinterpret_cast<page_id_t *>(data + OFFSET_PREV_PAGE);
    }

    void TablePage::SetPrevPageId(page_id_t prev_page_id) {
        uint8_t *data = reinterpret_cast<uint8_t*>(GetData());
        if (data == nullptr) return;
        memcpy(data + OFFSET_PREV_PAGE, &prev_page_id, sizeof(page_id_t));
    }

    uint32_t TablePage::GetTupleCount() {
        uint8_t *data = reinterpret_cast<uint8_t*>(GetData());
        if (data == nullptr) {
            return 0; // Safety: null page has 0 tuples
        }
        
        uint32_t count = *reinterpret_cast<uint32_t *>(data + OFFSET_TUPLE_COUNT);
        // Sanity check: tuple count should not be so large that slot array exceeds page size
        uint32_t max_possible_tuples = (PAGE_SIZE - SIZE_HEADER) / sizeof(Slot);
        if (count > max_possible_tuples) {
            return 0; // Corrupted - return 0 to prevent crashes
        }
        return count;
    }

    void TablePage::SetTupleCount(uint32_t count) {
        uint8_t *data = reinterpret_cast<uint8_t*>(GetData());
        if (data == nullptr) return;
        memcpy(data + OFFSET_TUPLE_COUNT, &count, sizeof(uint32_t));
    }

    uint32_t TablePage::GetFreeSpacePointer() {
        uint8_t *data = reinterpret_cast<uint8_t*>(GetData());
        if (data == nullptr) {
            return PAGE_SIZE; // Safety: null page reports full free space
        }
        
        uint32_t ptr = *reinterpret_cast<uint32_t *>(data + OFFSET_FREE_SPACE);
        // Sanity check: free space pointer should be within valid range
        if (ptr < SIZE_HEADER || ptr > PAGE_SIZE) {
            return PAGE_SIZE; // Corrupted - return safe default
        }
        return ptr;
    }

    void TablePage::SetFreeSpacePointer(uint32_t ptr) {
        uint8_t *data = reinterpret_cast<uint8_t*>(GetData());
        if (data == nullptr) return;
        memcpy(data + OFFSET_FREE_SPACE, &ptr, sizeof(uint32_t));
    }

    uint32_t TablePage::GetFreeSpaceRemaining() {
        // Free space is the gap between the end of the Slot Array and the Start of Tuple Data
        uint32_t header_end = SIZE_HEADER + (GetTupleCount() * sizeof(Slot));
        uint32_t data_start = GetFreeSpacePointer();

        if (header_end > data_start) return 0; // Should not happen
        return data_start - header_end;
    }

    // --- INSERT ---
    bool TablePage::InsertTuple(const Tuple &tuple, RID *rid, Transaction *txn) {
        (void) txn;

        // 1. Calculate space needed (Slot entry + Data)
        uint32_t size = tuple.GetLength();
        uint32_t space_needed = size + sizeof(Slot);

        // 2. Check if we have space
        if (GetFreeSpaceRemaining() < space_needed) {
            return false;
        }

        // 3. Update Free Space Pointer
        // Data is written backwards. New pointer = Old Pointer - Data Size
        uint32_t old_ptr = GetFreeSpacePointer();
        uint32_t new_ptr = old_ptr - size;
        SetFreeSpacePointer(new_ptr);

        // 4. Write Tuple Data
        memcpy(GetData() + new_ptr, tuple.GetData(), size);

        // 5. Create Slot Entry
        uint32_t tuple_count = GetTupleCount();
        Slot slot;
        slot.offset = new_ptr;
        slot.size = size;
        slot.meta = 0;

        // Write Slot to the array (which starts after header)
        uint32_t slot_offset = SIZE_HEADER + (tuple_count * sizeof(Slot));
        memcpy(GetData() + slot_offset, &slot, sizeof(Slot));

        // 6. Update Header
        SetTupleCount(tuple_count + 1);

        // 7. Mark page as dirty - this ensures checksum gets recalculated on flush
        SetDirty(true);

        // 8. Return RID
        rid->Set(GetPageId(), tuple_count); // Slot ID = Index
        return true;
    }

    // --- READ ---
    bool TablePage::GetTuple(const RID &rid, Tuple *tuple, Transaction *txn) {
        (void) txn;
        uint32_t slot_id = rid.GetSlotId();
        uint32_t tuple_count = GetTupleCount();
        
        if (slot_id >= tuple_count) {
            return false;
        }

        // Calculate slot offset and validate it's within page bounds
        uint32_t slot_offset = SIZE_HEADER + (slot_id * sizeof(Slot));
        
        // Prevent buffer overflow if tuple_count is corrupted
        if (slot_offset + sizeof(Slot) > PAGE_SIZE) {
            return false; // Slot array extends beyond page
        }

        // Read Slot
        Slot slot;
        memcpy(&slot, GetData() + slot_offset, sizeof(Slot));

        // Check if deleted
        if (slot.size == 0 || (slot.meta & TUPLE_DELETED)) {
            return false;
        }
        
        // Bounds checking: validate slot.offset and slot.size are within page bounds
        if (slot.offset < SIZE_HEADER || slot.offset >= PAGE_SIZE) {
            return false; // Invalid offset
        }
        if (slot.size == 0 || slot.size > PAGE_SIZE || slot.offset + slot.size > PAGE_SIZE) {
            return false; // Invalid size or would exceed page bounds
        }
        
        // Additional sanity check: slot data should not overlap with slot array
        uint32_t slot_array_end = SIZE_HEADER + (tuple_count * sizeof(Slot));
        if (slot.offset < slot_array_end) {
            return false; // Tuple data overlaps with slot array (corruption)
        }
        
        // Read Data
        tuple->DeserializeFrom(GetData() + slot.offset, slot.size);
        tuple->SetRid(rid);
        return true;
    }


    bool TablePage::MarkDelete(const RID &rid, Transaction *txn) {
        (void) txn;
        uint32_t slot_id = rid.GetSlotId();
        uint32_t tuple_count = GetTupleCount();
        
        if (slot_id >= tuple_count) {
            return false;
        }

        // 1. Calculate offset to the specific slot and validate
        uint32_t slot_offset = SIZE_HEADER + (slot_id * sizeof(Slot));
        
        // Prevent buffer overflow if tuple_count is corrupted
        if (slot_offset + sizeof(Slot) > PAGE_SIZE) {
            return false; // Slot array extends beyond page
        }

        // 2. Read the slot
        Slot slot;
        memcpy(&slot, GetData() + slot_offset, sizeof(Slot));

        // 3. Mark bit as deleted
        if (slot.meta & TUPLE_DELETED) {
            return false; // Already deleted
        }
        slot.meta |= TUPLE_DELETED;

        // 4. Write it back
        memcpy(GetData() + slot_offset, &slot, sizeof(Slot));

        // 5. Mark page as dirty - this ensures checksum gets recalculated on flush
        SetDirty(true);

        return true;
    }
    
    bool TablePage::UnmarkDelete(const RID &rid, Transaction *txn) {
        (void) txn;
        uint32_t slot_id = rid.GetSlotId();
        uint32_t tuple_count = GetTupleCount();
        
        if (slot_id >= tuple_count) {
            return false;
        }

        // 1. Calculate offset to the specific slot and validate
        uint32_t slot_offset = SIZE_HEADER + (slot_id * sizeof(Slot));
        
        // Prevent buffer overflow if tuple_count is corrupted
        if (slot_offset + sizeof(Slot) > PAGE_SIZE) {
            return false; // Slot array extends beyond page
        }

        // 2. Read the slot
        Slot slot;
        memcpy(&slot, GetData() + slot_offset, sizeof(Slot));

        // 3. Unmark bit (clear deleted flag)
        if (!(slot.meta & TUPLE_DELETED)) {
            return false; // Not deleted
        }
        slot.meta &= ~TUPLE_DELETED;

        // 4. Write it back
        memcpy(GetData() + slot_offset, &slot, sizeof(Slot));

        // 5. Mark page as dirty - this ensures checksum gets recalculated on flush
        SetDirty(true);

        return true;
    }
} // namespace francodb
