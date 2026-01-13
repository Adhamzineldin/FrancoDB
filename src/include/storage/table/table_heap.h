#pragma once

#include "buffer/buffer_pool_manager.h"
#include "storage/table/table_page.h"
#include "storage/table/tuple.h"
#include "concurrency/transaction.h"

namespace francodb {

    /**
     * TableHeap represents a physical table on disk.
     * It is a doubly-linked list of TablePages.
     */
    class TableHeap {
    public:
        // Create a new table heap (or open existing)
        // If first_page_id is INVALID, we create a new table.
        TableHeap(BufferPoolManager *bpm, Transaction *txn = nullptr);

        // Insert a tuple -> Returns true if success
        bool InsertTuple(const Tuple &tuple, RID *rid, Transaction *txn);

        // Mark a tuple as deleted
        bool MarkDelete(const RID &rid, Transaction *txn);

        // Update a tuple (Delete Old + Insert New)
        bool UpdateTuple(const Tuple &tuple, const RID &rid, Transaction *txn);

        // Read a tuple
        bool GetTuple(const RID &rid, Tuple *tuple, Transaction *txn);

        // Get the ID of the first page (useful for scanning)
        page_id_t GetFirstPageId() const;

    private:
        BufferPoolManager *buffer_pool_manager_;
        page_id_t first_page_id_;
    };

} // namespace francodb