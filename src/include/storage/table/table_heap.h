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
            
            /**
             * Dereference operator - returns copy of current tuple
             * Consider using GetCurrentTuple() for reference access
             */
            Tuple operator*();
            
            /**
             * Get reference to current tuple (avoids copy)
             * Valid until Next() is called
             */
            const Tuple& GetCurrentTuple() const { return cached_tuple_; }
            
            /**
             * Extract tuple with move semantics (most efficient)
             * Iterator must be advanced after calling this
             */
            Tuple ExtractTuple() { return std::move(cached_tuple_); }
            
            Iterator& operator++();
            bool operator!=(const Iterator &other) const;
            RID GetRID() const { return RID(current_page_id_, current_slot_); }
            
            /**
             * Check if iterator has a valid cached tuple
             */
            bool HasCachedTuple() const { return has_cached_tuple_; }

        private:
            void AdvanceToNextValidTuple();
            void CacheTuple();  // Load current tuple into cache
            
            BufferPoolManager *bpm_;
            page_id_t current_page_id_;
            uint32_t current_slot_;
            Transaction *txn_;
            bool is_end_;
            
            // Cached tuple to avoid repeated reads
            Tuple cached_tuple_;
            bool has_cached_tuple_ = false;
        };

        Iterator Begin(Transaction *txn = nullptr);
        Iterator End();

    private:
        BufferPoolManager *buffer_pool_manager_;
        page_id_t first_page_id_;
    };

} // namespace francodb