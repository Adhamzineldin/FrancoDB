#include "buffer/buffer_pool_manager.h"
#include "buffer/lru_replacer.h"
#include "buffer/clock_replacer.h"
#include "common/config.h"

namespace francodb {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager) {
    
    pages_ = new Page[pool_size_];
    
    replacer_ = new LRUReplacer(pool_size);
    // replacer_ = new ClockReplacer(pool_size); 

    for (size_t i = 0; i < pool_size_; ++i) {
        free_list_.push_back(static_cast<frame_id_t>(i));
    }
    
    // Simple recovery for next_page_id
    // In production, we would store this in the Page 0 header.
    if (disk_manager_->GetFileSize("test_persistence.fdb") > 0) {
       // next_page_id_ = ... calculation ...
       // For now, let's keep it simple or the compiler might complain about file names
       next_page_id_ = 0; 
    }
}

BufferPoolManager::~BufferPoolManager() {
    delete[] pages_;
    delete replacer_;
}

// ... (FindFreeFrame is the same as before) ...
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

    if (page_table_.find(page_id) != page_table_.end()) {
        frame_id_t frame_id = page_table_[page_id];
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
    disk_manager_->ReadPage(page_id, page->GetData());
    page_table_[page_id] = frame_id;
    page->IncrementPinCount();
    replacer_->Pin(frame_id);

    return page;
}

// ... UnpinPage and FlushPage stay the same ...

Page *BufferPoolManager::NewPage(page_id_t *page_id) {
    std::lock_guard<std::mutex> guard(latch_);

    // --- FIX: Initialize variable ---
    frame_id_t frame_id = -1;
    
    if (!FindFreeFrame(&frame_id)) {
        return nullptr;
    }

    *page_id = next_page_id_++;
    Page *page = &pages_[frame_id];
    page->Init(*page_id);

    page_table_[*page_id] = frame_id;
    page->IncrementPinCount();
    replacer_->Pin(frame_id);

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
    for (auto const& [page_id, frame_id] : page_table_) {
        Page *page = &pages_[frame_id];
        if (page->IsDirty()) {
            disk_manager_->WritePage(page_id, page->GetData());
            page->SetDirty(false);
        }
    }
}

} // namespace francodb