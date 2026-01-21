#pragma once

#include <cstring>
#include "storage/page/page.h"
#include "storage/table/tuple.h"
#include "common/rid.h"
#include "concurrency/transaction.h"

namespace francodb {
    static constexpr uint8_t TUPLE_DELETED = 0x01;

    /**
     * Slotted Page Layout:
     * -------------------------------------------------------------------------------
     * | Checksum (4) | Header | Slot[0] | Slot[1] | ... | ... | Tuple[1] | Tuple[0] |
     * -------------------------------------------------------------------------------
     *
     * Header:
     * - PrevPageId (4B)
     * - NextPageId (4B)
     * - FreeSpacePointer (4B): Offset to the *start* of the tuple area.
     * - TupleCount (4B): Number of slots.
     */
    class TablePage : public Page {
    public:
        void Init(page_id_t page_id, page_id_t prev_id, page_id_t next_id, Transaction *txn = nullptr);

        page_id_t GetTablePageId();

        page_id_t GetNextPageId();

        page_id_t GetPrevPageId();

        void SetNextPageId(page_id_t next_page_id);

        void SetPrevPageId(page_id_t prev_page_id);

        // --- MAIN OPERATIONS ---

        // Insert a tuple. Returns true if success.
        // Sets the RID inside the tuple object.
        bool InsertTuple(const Tuple &tuple, RID *rid, Transaction *txn);

        // Read a tuple given its RID.
        bool GetTuple(const RID &rid, Tuple *tuple, Transaction *txn);

        // Mark a tuple as deleted (we don't physically remove immediately)
        bool MarkDelete(const RID &rid, Transaction *txn);
        
        // Unmark a tuple as deleted (for rollback)
        bool UnmarkDelete(const RID &rid, Transaction *txn);

        // Returns remaining free space in bytes
        uint32_t GetFreeSpaceRemaining();
        
        uint32_t GetTupleCount();

    private:
        static constexpr size_t OFFSET_CHECKSUM   = 0;
        static constexpr size_t OFFSET_PREV_PAGE  = 4; // Was 0
        static constexpr size_t OFFSET_NEXT_PAGE  = 8; // Was 4
        static constexpr size_t OFFSET_FREE_SPACE = 12; // Was 8
        static constexpr size_t OFFSET_TUPLE_COUNT= 16; // Was 12
        static constexpr size_t SIZE_HEADER       = 20; // Was 16

        // Slot structure (4 bytes offset, 4 bytes size, 1 byte meta) = 9 bytes
        // Use explicit packed alignment to avoid padding issues
        struct __attribute__((packed)) Slot {
            uint32_t offset;
            uint32_t size;
            uint8_t meta;
        };
        static_assert(sizeof(Slot) == 9, "Slot must be exactly 9 bytes");

        // Private Helpers
       

        void SetTupleCount(uint32_t count);

        uint32_t GetFreeSpacePointer();

        void SetFreeSpacePointer(uint32_t ptr);
    };
} // namespace francodb
