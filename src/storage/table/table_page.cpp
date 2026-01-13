#include "storage/table/table_page.h"
#include "common/exception.h"
#include "concurrency/transaction.h"

namespace francodb {
    void TablePage::Init(page_id_t page_id, page_id_t prev_id, page_id_t next_id, Transaction *txn) {
        (void) txn;

        // FIX: Actually use page_id to initialize the base Page metadata
        // This assumes TablePage inherits from Page (which it does)
        // Note: TablePage::Init usually runs ON TOP of a Page that BufferPool already Init-ed.
        // So we don't strictly *need* to call Page::Init here, but using the variable silences the warning.
        // Let's just silence it or double-set it. Double-setting is safer.
        page_id_ = page_id;

        // Write Header Fields
        memcpy(GetData() + OFFSET_PREV_PAGE, &prev_id, sizeof(page_id_t));
        memcpy(GetData() + OFFSET_NEXT_PAGE, &next_id, sizeof(page_id_t));

        uint32_t free_ptr = PAGE_SIZE;
        SetFreeSpacePointer(free_ptr);
        SetTupleCount(0);
    }

    // --- Getters / Setters ---
    page_id_t TablePage::GetNextPageId() {
        return *reinterpret_cast<page_id_t *>(GetData() + OFFSET_NEXT_PAGE);
    }

    void TablePage::SetNextPageId(page_id_t next_page_id) {
        memcpy(GetData() + OFFSET_NEXT_PAGE, &next_page_id, sizeof(page_id_t));
    }

    page_id_t TablePage::GetPrevPageId() {
        return *reinterpret_cast<page_id_t *>(GetData() + OFFSET_PREV_PAGE);
    }

    void TablePage::SetPrevPageId(page_id_t prev_page_id) {
        memcpy(GetData() + OFFSET_PREV_PAGE, &prev_page_id, sizeof(page_id_t));
    }

    uint32_t TablePage::GetTupleCount() {
        return *reinterpret_cast<uint32_t *>(GetData() + OFFSET_TUPLE_COUNT);
    }

    void TablePage::SetTupleCount(uint32_t count) {
        memcpy(GetData() + OFFSET_TUPLE_COUNT, &count, sizeof(uint32_t));
    }

    uint32_t TablePage::GetFreeSpacePointer() {
        return *reinterpret_cast<uint32_t *>(GetData() + OFFSET_FREE_SPACE);
    }

    void TablePage::SetFreeSpacePointer(uint32_t ptr) {
        memcpy(GetData() + OFFSET_FREE_SPACE, &ptr, sizeof(uint32_t));
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

        // 7. Return RID
        rid->Set(GetPageId(), tuple_count); // Slot ID = Index
        return true;
    }

    // --- READ ---
    bool TablePage::GetTuple(const RID &rid, Tuple *tuple, Transaction *txn) {
        (void) txn;
        uint32_t slot_id = rid.GetSlotNum();
        if (slot_id >= GetTupleCount()) {
            return false;
        }

        // Read Slot
        uint32_t slot_offset = SIZE_HEADER + (slot_id * sizeof(Slot));
        Slot slot;
        memcpy(&slot, GetData() + slot_offset, sizeof(Slot));

        // Check if deleted
        if (slot.size == 0 || (slot.meta & TUPLE_DELETED)) {
            // <--- NEW CHECK
            return false;
        }
        // Read Data
        tuple->DeserializeFrom(GetData() + slot.offset, slot.size);
        tuple->SetRid(rid);
        return true;
    }


    bool TablePage::MarkDelete(const RID &rid, Transaction *txn) {
        (void) txn;
        uint32_t slot_id = rid.GetSlotNum();
        if (slot_id >= GetTupleCount()) {
            return false;
        }

        // 1. Calculate offset to the specific slot
        uint32_t slot_offset = SIZE_HEADER + (slot_id * sizeof(Slot));

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

        return true;
    }
} // namespace francodb
