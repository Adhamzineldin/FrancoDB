#include "storage/table/table_heap.h"
#include "common/exception.h"

namespace francodb {
    // Constructor
    TableHeap::TableHeap(BufferPoolManager *bpm, Transaction *txn) : buffer_pool_manager_(bpm) {
        (void) txn;
        // 1. Allocate the first page of the table
        page_id_t first_page_id;
        Page *page = bpm->NewPage(&first_page_id);

        if (page == nullptr) {
            throw Exception(ExceptionType::OUT_OF_RANGE, "Out of memory: Could not create TableHeap.");
        }

        // 2. Initialize it as a TablePage
        auto *table_page = reinterpret_cast<TablePage *>(page->GetData());
        table_page->Init(first_page_id, INVALID_PAGE_ID, INVALID_PAGE_ID, txn);

        // 3. Save the ID
        first_page_id_ = first_page_id;

        // 4. Unpin
        bpm->UnpinPage(first_page_id, true);
    }

    page_id_t TableHeap::GetFirstPageId() const {
        return first_page_id_;
    }

    // --- INSERT ---
    bool TableHeap::InsertTuple(const Tuple &tuple, RID *rid, Transaction *txn) {
        // Start at the first page
        page_id_t current_page_id = first_page_id_;

        // Loop through the linked list to find a page with space
        while (current_page_id != INVALID_PAGE_ID) {
            Page *page = buffer_pool_manager_->FetchPage(current_page_id);
            if (page == nullptr) return false;

            auto *table_page = reinterpret_cast<TablePage *>(page->GetData());

            // Try to insert
            if (table_page->InsertTuple(tuple, rid, txn)) {
                // Success! Unpin and return.
                buffer_pool_manager_->UnpinPage(current_page_id, true);
                return true;
            }

            // Failed (Not enough space).
            // Check if there is a next page.
            page_id_t next_page_id = table_page->GetNextPageId();

            if (next_page_id == INVALID_PAGE_ID) {
                // We are at the end of the list, and it's full.
                // Create a NEW page.
                page_id_t new_page_id;
                Page *new_page_raw = buffer_pool_manager_->NewPage(&new_page_id);
                if (new_page_raw == nullptr) {
                    buffer_pool_manager_->UnpinPage(current_page_id, false);
                    return false;
                }

                auto *new_page = reinterpret_cast<TablePage *>(new_page_raw->GetData());

                // Init new page (Prev = Current, Next = Invalid)
                new_page->Init(new_page_id, current_page_id, INVALID_PAGE_ID, txn);

                // Link Current -> New
                table_page->SetNextPageId(new_page_id);

                // Insert the tuple into the new page
                new_page->InsertTuple(tuple, rid, txn);

                // Unpin both
                buffer_pool_manager_->UnpinPage(current_page_id, true); // We modified NextID
                buffer_pool_manager_->UnpinPage(new_page_id, true); // We inserted Data
                return true;
            }

            // Move to next page
            buffer_pool_manager_->UnpinPage(current_page_id, false);
            current_page_id = next_page_id;
        }

        return false;
    }

    // --- READ ---
    bool TableHeap::GetTuple(const RID &rid, Tuple *tuple, Transaction *txn) {
        page_id_t page_id = rid.GetPageId();
        Page *page = buffer_pool_manager_->FetchPage(page_id);
        if (page == nullptr) return false;

        auto *table_page = reinterpret_cast<TablePage *>(page->GetData());
        bool res = table_page->GetTuple(rid, tuple, txn);

        buffer_pool_manager_->UnpinPage(page_id, false);
        return res;
    }

    // --- DELETE ---
    bool TableHeap::MarkDelete(const RID &rid, Transaction *txn) {
        page_id_t page_id = rid.GetPageId();
        Page *page = buffer_pool_manager_->FetchPage(page_id);
        if (page == nullptr) return false;

        auto *table_page = reinterpret_cast<TablePage *>(page->GetData());

        // Call the actual page method
        bool result = table_page->MarkDelete(rid, txn);

        // Unpin (Dirty=true because we changed the meta bit)
        buffer_pool_manager_->UnpinPage(page_id, true);

        return result;
    }

    // --- UPDATE ---
    bool TableHeap::UpdateTuple(const Tuple &tuple, const RID &rid, Transaction *txn) {
        // 1. Find the page containing the old tuple
        Page *page = buffer_pool_manager_->FetchPage(rid.GetPageId());
        if (page == nullptr) return false;
        auto *table_page = reinterpret_cast<TablePage *>(page->GetData());

        // 2. Mark Old as Deleted
        // We do this directly here to save a FetchPage call compared to calling MarkDelete()
        bool is_deleted = table_page->MarkDelete(rid, txn);
        buffer_pool_manager_->UnpinPage(rid.GetPageId(), true); // Unpin dirty

        if (!is_deleted) {
            return false; // Could not delete (maybe already deleted)
        }

        // 3. Insert New Tuple (This will likely go to the end of the heap)
        RID new_rid;
        return InsertTuple(tuple, &new_rid, txn);
    }
} // namespace francodb
