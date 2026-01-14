#include "storage/table/table_heap.h"
#include "common/exception.h"

namespace francodb {

    // Constructor 1: Open existing
    TableHeap::TableHeap(BufferPoolManager *bpm, page_id_t first_page_id) 
        : buffer_pool_manager_(bpm), first_page_id_(first_page_id) {
    }
    
    // Constructor 2: Create New
    TableHeap::TableHeap(BufferPoolManager *bpm, Transaction *txn) : buffer_pool_manager_(bpm) {
        (void)txn;
        page_id_t new_page_id;
        Page *page = bpm->NewPage(&new_page_id);
        if (page == nullptr) throw Exception(ExceptionType::OUT_OF_RANGE, "Out of memory");
        
        page->WLock();
        auto *table_page = reinterpret_cast<TablePage *>(page->GetData());
        table_page->Init(new_page_id, INVALID_PAGE_ID, INVALID_PAGE_ID, txn);
        first_page_id_ = new_page_id;
        page->WUnlock();
        
        bpm->UnpinPage(new_page_id, true);
    }

    // --- INSERT (Thread-Safe) ---
    bool TableHeap::InsertTuple(const Tuple &tuple, RID *rid, Transaction *txn) {
        if (first_page_id_ == INVALID_PAGE_ID) return false;

        Page *page = buffer_pool_manager_->FetchPage(first_page_id_);
        if (page == nullptr) return false;

        page->WLock(); 
        auto *table_page = reinterpret_cast<TablePage *>(page->GetData());

        while (true) {
            if (table_page->InsertTuple(tuple, rid, txn)) {
                page->WUnlock(); 
                buffer_pool_manager_->UnpinPage(table_page->GetPageId(), true);
                return true;
            }

            page_id_t next_page_id = table_page->GetNextPageId();
            
            if (next_page_id == INVALID_PAGE_ID) {
                // Create NEW page
                page_id_t new_page_id;
                Page *new_page_raw = buffer_pool_manager_->NewPage(&new_page_id);
                if (new_page_raw == nullptr) {
                    page->WUnlock();
                    buffer_pool_manager_->UnpinPage(table_page->GetPageId(), false);
                    return false;
                }
                
                new_page_raw->WLock();
                auto *new_page = reinterpret_cast<TablePage *>(new_page_raw->GetData());
                new_page->Init(new_page_id, table_page->GetPageId(), INVALID_PAGE_ID, txn);
                
                table_page->SetNextPageId(new_page_id);
                new_page->InsertTuple(tuple, rid, txn);
                
                new_page_raw->WUnlock();
                buffer_pool_manager_->UnpinPage(new_page_id, true);
                
                page->WUnlock();
                buffer_pool_manager_->UnpinPage(table_page->GetPageId(), true);
                return true;
            }

            // Crabbing
            page->WUnlock(); 
            buffer_pool_manager_->UnpinPage(table_page->GetPageId(), false);
            
            page = buffer_pool_manager_->FetchPage(next_page_id);
            if (page == nullptr) return false;
            
            page->WLock(); 
            table_page = reinterpret_cast<TablePage *>(page->GetData());
        }
    }

    bool TableHeap::GetTuple(const RID &rid, Tuple *tuple, Transaction *txn) {
        Page *page = buffer_pool_manager_->FetchPage(rid.GetPageId());
        if (page == nullptr) return false;

        page->RLock(); 
        auto *table_page = reinterpret_cast<TablePage *>(page->GetData());
        bool res = table_page->GetTuple(rid, tuple, txn);
        page->RUnlock(); 

        buffer_pool_manager_->UnpinPage(rid.GetPageId(), false);
        return res;
    }

    bool TableHeap::MarkDelete(const RID &rid, Transaction *txn) {
        Page *page = buffer_pool_manager_->FetchPage(rid.GetPageId());
        if (page == nullptr) return false;
        
        page->WLock();
        auto *table_page = reinterpret_cast<TablePage *>(page->GetData());
        bool result = table_page->MarkDelete(rid, txn);
        page->WUnlock();
        
        buffer_pool_manager_->UnpinPage(rid.GetPageId(), true);
        return result;
    }
    
    bool TableHeap::UnmarkDelete(const RID &rid, Transaction *txn) {
        Page *page = buffer_pool_manager_->FetchPage(rid.GetPageId());
        if (page == nullptr) return false;
        
        page->WLock();
        auto *table_page = reinterpret_cast<TablePage *>(page->GetData());
        bool result = table_page->UnmarkDelete(rid, txn);
        page->WUnlock();
        
        buffer_pool_manager_->UnpinPage(rid.GetPageId(), true);
        return result;
    }

    bool TableHeap::UpdateTuple(const Tuple &tuple, const RID &rid, Transaction *txn) {
        Page *page = buffer_pool_manager_->FetchPage(rid.GetPageId());
        if (page == nullptr) return false;
        
        page->WLock();
        auto *table_page = reinterpret_cast<TablePage *>(page->GetData());
        bool is_deleted = table_page->MarkDelete(rid, txn);
        page->WUnlock();
        
        buffer_pool_manager_->UnpinPage(rid.GetPageId(), true);
        
        if (!is_deleted) return false;
        
        RID new_rid;
        return InsertTuple(tuple, &new_rid, txn);
    }
    
    page_id_t TableHeap::GetFirstPageId() const { return first_page_id_; }

} // namespace francodb