#pragma once

#include "buffer/buffer_pool_manager.h"
#include "storage/table/table_page.h"
#include "storage/table/tuple.h"
#include "concurrency/transaction.h"
#include "common/exception.h" 

namespace francodb {

    /**
     * TableHeap represents a physical table on disk.
     * It is a doubly-linked list of TablePages.
     */
    class TableHeap {
    public:
        // FIX: Just DECLARE it here (removed the {} body)
        TableHeap(BufferPoolManager *bpm, page_id_t first_page_id);
        
        TableHeap(BufferPoolManager *bpm, Transaction *txn = nullptr);

        // Insert a tuple -> Returns true if success
        bool InsertTuple(const Tuple &tuple, RID *rid, Transaction *txn);

        // Mark a tuple as deleted
        bool MarkDelete(const RID &rid, Transaction *txn);
        
        // Unmark a tuple as deleted (for rollback)
        bool UnmarkDelete(const RID &rid, Transaction *txn);

        // Update a tuple (Delete Old + Insert New)
        bool UpdateTuple(const Tuple &tuple, const RID &rid, Transaction *txn);

        // Read a tuple
        bool GetTuple(const RID &rid, Tuple *tuple, Transaction *txn);

        // Get the ID of the first page (useful for scanning)
        page_id_t GetFirstPageId() const;
        
        
        
        
        class Iterator {
        public:
            Iterator(BufferPoolManager *bpm, page_id_t page_id, uint32_t slot_id, 
                     Transaction *txn, bool is_end = false);
            
            Tuple operator*();
            Iterator& operator++();
            bool operator!=(const Iterator &other) const;
            RID GetRID() const { return RID(current_page_id_, current_slot_); }

        private:
            void AdvanceToNextValidTuple();
            
            BufferPoolManager *bpm_;
            page_id_t current_page_id_;
            uint32_t current_slot_;
            Transaction *txn_;
            bool is_end_;
        };

        Iterator Begin(Transaction *txn = nullptr);
        Iterator End();

    private:
        BufferPoolManager *buffer_pool_manager_;
        page_id_t first_page_id_;
    };

} // namespace francodb