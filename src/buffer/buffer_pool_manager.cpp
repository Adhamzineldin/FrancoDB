#include "buffer/buffer_pool_manager.h"
#include "buffer/lru_replacer.h"
#include "buffer/clock_replacer.h"
#include "common/config.h"
#include "storage/page/free_page_manager.h"

namespace francodb {
    BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager)
        : pool_size_(pool_size), disk_manager_(disk_manager) {
        pages_ = new Page[pool_size_];

        // Default to LRU, but easy to switch
        replacer_ = new LRUReplacer(pool_size);
        // replacer_ = new ClockReplacer(pool_size); 

        for (size_t i = 0; i < pool_size_; ++i) {
            free_list_.push_back(static_cast<frame_id_t>(i));
        }

        int num_pages = disk_manager_->GetNumPages();

        next_page_id_ = num_pages;

        if (next_page_id_ == 0) {
            next_page_id_ = 1;
        }
    }

    BufferPoolManager::~BufferPoolManager() {
        // Force write everything to disk before destroying RAM
        FlushAllPages();

        delete[] pages_;
        delete replacer_;
    }
    
    bool BufferPoolManager::FindFreeFrame(frame_id_t *out_frame_id) {
        if (!free_list_.empty()) {
            *out_frame_id = free_list_.front();
            free_list_.pop_front();
            return true;
        }
        if (replacer_->Victim(out_frame_id)) {
            Page *victim_page = &pages_[*out_frame_id];
            if (victim_page->IsDirty()) {
                disk_manager_->WritePage(victim_page->GetPageId(), victim_page->GetData());
            }
            page_table_.erase(victim_page->GetPageId());
            return true;
        }
        return false;
    }

    Page *BufferPoolManager::FetchPage(page_id_t page_id) {
        std::lock_guard<std::mutex> guard(latch_);

        // Validate page_id before using it
        if (page_id == INVALID_PAGE_ID || page_id < 0) {
            return nullptr;
        }

        if (page_table_.find(page_id) != page_table_.end()) {
            frame_id_t frame_id = page_table_[page_id];
            if (frame_id < 0 || frame_id >= static_cast<frame_id_t>(pool_size_)) {
                return nullptr; // Invalid frame_id
            }
            Page *page = &pages_[frame_id];
            page->IncrementPinCount();
            replacer_->Pin(frame_id);
            return page;
        }

        // --- FIX: Initialize variable to avoid warnings ---
        frame_id_t frame_id = -1;

        if (!FindFreeFrame(&frame_id)) {
            return nullptr;
        }

        Page *page = &pages_[frame_id];
        page->Init(page_id);
        
        // Validate that ReadPage succeeds (it might fail for invalid page IDs)
        try {
            disk_manager_->ReadPage(page_id, page->GetData());
        } catch (...) {
            // If ReadPage fails, clean up and return nullptr
            page->Init(INVALID_PAGE_ID);
            free_list_.push_back(frame_id);
            return nullptr;
        }
        
        page_table_[page_id] = frame_id;
        page->IncrementPinCount();
        replacer_->Pin(frame_id);

        return page;
    }

    
    Page *BufferPoolManager::NewPage(page_id_t *page_id) {
        std::lock_guard<std::mutex> guard(latch_);

        // 1. Find a free frame in the buffer pool
        frame_id_t frame_id = -1;
        if (!free_list_.empty()) {
            frame_id = free_list_.front();
            free_list_.pop_front();
        } else if (replacer_->Victim(&frame_id)) {
            Page *old_page = &pages_[frame_id];
            if (old_page->IsDirty()) {
                disk_manager_->WritePage(old_page->GetPageId(), old_page->GetData());
            }
            page_table_.erase(old_page->GetPageId());
        }

        if (frame_id == -1) return nullptr;

        // 2. S-CLASS ALLOCATION LOGIC
        // Instead of just incrementing, we consult the Bitmap (Page 2)
    
        // We need to read Page 2 to see if there are any 0s (free pages)
        char bitmap_data[PAGE_SIZE];
        disk_manager_->ReadPage(2, bitmap_data);
    
        // AllocatePage will return a recycled ID or the end-of-file ID
        *page_id = FreePageManager::AllocatePage(bitmap_data, disk_manager_->GetNumPages());
    
        // Write the updated bitmap back to disk immediately 
        // (Or better: keep Page 2 pinned in the buffer pool, but this is safer for now)
        disk_manager_->WritePage(2, bitmap_data);

        // 3. Initialize the page
        Page *page = &pages_[frame_id];
        page->Init(*page_id);
    
        page_table_[*page_id] = frame_id;
        page->IncrementPinCount();

        return page;
    }

    // ... (Copy DeletePage, UnpinPage, FlushAllPages, FlushPage from previous implementation) ...
    // (I didn't repeat them to save space, but make sure they are in your file!)

    bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
        std::lock_guard<std::mutex> guard(latch_);
        if (page_table_.find(page_id) == page_table_.end()) return false;
        frame_id_t frame_id = page_table_[page_id];
        Page *page = &pages_[frame_id];
        if (is_dirty) page->SetDirty(true);
        if (page->GetPinCount() <= 0) return false;
        page->DecrementPinCount();
        if (page->GetPinCount() == 0) replacer_->Unpin(frame_id);
        return true;
    }

    bool BufferPoolManager::FlushPage(page_id_t page_id) {
        std::lock_guard<std::mutex> guard(latch_);
        if (page_id == 0) {
            // Never flush or write page 0 (magic header). Only DiskManager writes page 0 during DB creation.
            return false;
        }
        if (page_table_.find(page_id) == page_table_.end()) return false;
        frame_id_t frame_id = page_table_[page_id];
        Page *page = &pages_[frame_id];
        disk_manager_->WritePage(page_id, page->GetData());
        page->SetDirty(false);
        return true;
    }

    bool BufferPoolManager::DeletePage(page_id_t page_id) {
        std::lock_guard<std::mutex> guard(latch_);
        if (page_table_.find(page_id) == page_table_.end()) return true;
        frame_id_t frame_id = page_table_[page_id];
        Page *page = &pages_[frame_id];
        if (page->GetPinCount() > 0) return false;
        page_table_.erase(page_id);
        page->Init(INVALID_PAGE_ID);
        free_list_.push_back(frame_id);
        return true;
    }

    void BufferPoolManager::FlushAllPages() {
        std::lock_guard<std::mutex> guard(latch_);
        for (auto const &[page_id, frame_id]: page_table_) {
            if (page_id == 0) continue; // Never flush or write page 0
            Page *page = &pages_[frame_id];
            if (page->IsDirty()) {
                disk_manager_->WritePage(page_id, page->GetData());
                page->SetDirty(false);
            }
        }
    }
} // namespace francodb
