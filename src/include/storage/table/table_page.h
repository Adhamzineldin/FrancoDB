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
     * ---------------------------------------------------------
     * | Header | Slot[0] | Slot[1] | ... | ... | Tuple[1] | Tuple[0] |
     * ---------------------------------------------------------
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
        // Helper to write/read header fields
        // Standard offsets for header fields
        static constexpr size_t OFFSET_PREV_PAGE = 0;
        static constexpr size_t OFFSET_NEXT_PAGE = 4;
        static constexpr size_t OFFSET_FREE_SPACE = 8;
        static constexpr size_t OFFSET_TUPLE_COUNT = 12;
        static constexpr size_t SIZE_HEADER = 16;

        // Slot structure (4 bytes offset, 4 bytes size)
        struct Slot {
            uint32_t offset;
            uint32_t size;
            uint8_t meta;
        };

        // Private Helpers
       

        void SetTupleCount(uint32_t count);

        uint32_t GetFreeSpacePointer();

        void SetFreeSpacePointer(uint32_t ptr);
    };
} // namespace francodb
