#pragma once

#include "buffer/buffer_pool_manager.h"
#include "storage/page/page.h"

namespace francodb {

/**
 * PageGuard - RAII wrapper for buffer pool pages.
 * 
 * PROBLEM SOLVED:
 * - Automatic UnpinPage on destruction (even if exception thrown)
 * - Automatic latch release on destruction
 * - Prevents pin leaks that exhaust the buffer pool
 * 
 * USAGE:
 *   {
 *       PageGuard guard(bpm, page_id, true);  // WLock
 *       if (!guard.IsValid()) return false;
 *       
 *       auto* page = guard.As<TablePage>();
 *       page->InsertTuple(...);
 *       guard.SetDirty();
 *   }  // Auto-unpin and unlock here
 */
class PageGuard {
public:
    /**
     * Construct a PageGuard that fetches and optionally locks a page.
     * 
     * @param bpm Buffer pool manager
     * @param page_id Page to fetch
     * @param is_write If true, acquire WLock; if false, acquire RLock
     */
    PageGuard(BufferPoolManager* bpm, page_id_t page_id, bool is_write = false)
        : bpm_(bpm), 
          page_id_(page_id), 
          page_(nullptr),
          is_dirty_(false), 
          is_write_locked_(false),
          is_read_locked_(false),
          released_(false) {
        
        if (bpm_ == nullptr || page_id == INVALID_PAGE_ID) {
            return;
        }
        
        page_ = bpm_->FetchPage(page_id);
        if (page_ == nullptr) {
            return;
        }
        
        if (is_write) {
            page_->WLock();
            is_write_locked_ = true;
        } else {
            page_->RLock();
            is_read_locked_ = true;
        }
    }
    
    /**
     * Destructor - automatically releases latch and unpins page.
     */
    ~PageGuard() {
        Release();
    }
    
    // Non-copyable
    PageGuard(const PageGuard&) = delete;
    PageGuard& operator=(const PageGuard&) = delete;
    
    // Movable
    PageGuard(PageGuard&& other) noexcept
        : bpm_(other.bpm_),
          page_id_(other.page_id_),
          page_(other.page_),
          is_dirty_(other.is_dirty_),
          is_write_locked_(other.is_write_locked_),
          is_read_locked_(other.is_read_locked_),
          released_(other.released_) {
        other.released_ = true;  // Prevent double-release
        other.page_ = nullptr;
    }
    
    PageGuard& operator=(PageGuard&& other) noexcept {
        if (this != &other) {
            Release();  // Release current
            
            bpm_ = other.bpm_;
            page_id_ = other.page_id_;
            page_ = other.page_;
            is_dirty_ = other.is_dirty_;
            is_write_locked_ = other.is_write_locked_;
            is_read_locked_ = other.is_read_locked_;
            released_ = other.released_;
            
            other.released_ = true;
            other.page_ = nullptr;
        }
        return *this;
    }
    
    /**
     * Check if the page was successfully fetched.
     */
    bool IsValid() const { 
        return page_ != nullptr && !released_; 
    }
    
    /**
     * Get the underlying page pointer.
     */
    Page* GetPage() { 
        return page_; 
    }
    
    const Page* GetPage() const { 
        return page_; 
    }
    
    /**
     * Arrow operator for convenient member access.
     */
    Page* operator->() { 
        return page_; 
    }
    
    const Page* operator->() const { 
        return page_; 
    }
    
    /**
     * Dereference operator.
     */
    Page& operator*() { 
        return *page_; 
    }
    
    const Page& operator*() const { 
        return *page_; 
    }
    
    /**
     * Cast the page data to a specific type.
     * 
     * USAGE:
     *   auto* table_page = guard.As<TablePage>();
     */
    template<typename T>
    T* As() {
        return reinterpret_cast<T*>(page_->GetData());
    }
    
    template<typename T>
    const T* As() const {
        return reinterpret_cast<const T*>(page_->GetData());
    }
    
    /**
     * Mark the page as dirty (will be written to disk on eviction).
     */
    void SetDirty() { 
        is_dirty_ = true; 
    }
    
    /**
     * Get the page ID.
     */
    page_id_t GetPageId() const { 
        return page_id_; 
    }
    
    /**
     * Manually release the page early (before destructor).
     * After calling this, the guard is invalid.
     */
    void Release() {
        if (released_ || page_ == nullptr) {
            return;
        }
        
        // Release latch first
        if (is_write_locked_) {
            page_->WUnlock();
            is_write_locked_ = false;
        } else if (is_read_locked_) {
            page_->RUnlock();
            is_read_locked_ = false;
        }
        
        // Unpin page
        bpm_->UnpinPage(page_id_, is_dirty_);
        
        released_ = true;
        page_ = nullptr;
    }
    
    /**
     * Upgrade from read lock to write lock.
     * WARNING: This can deadlock if another thread also tries to upgrade!
     * Only use when you're certain no other thread is reading.
     * 
     * @return true if upgrade succeeded, false otherwise
     */
    bool UpgradeToWrite() {
        if (!IsValid() || !is_read_locked_ || is_write_locked_) {
            return false;
        }
        
        // Release read lock
        page_->RUnlock();
        is_read_locked_ = false;
        
        // Acquire write lock (may block)
        page_->WLock();
        is_write_locked_ = true;
        
        return true;
    }
    
    /**
     * Downgrade from write lock to read lock.
     * This allows other readers to proceed.
     */
    void DowngradeToRead() {
        if (!IsValid() || !is_write_locked_) {
            return;
        }
        
        // Release write lock
        page_->WUnlock();
        is_write_locked_ = false;
        
        // Acquire read lock
        page_->RLock();
        is_read_locked_ = true;
    }
    
private:
    BufferPoolManager* bpm_;
    page_id_t page_id_;
    Page* page_;
    bool is_dirty_;
    bool is_write_locked_;
    bool is_read_locked_;
    bool released_;
};

/**
 * WritePageGuard - Convenience alias for write-locked pages.
 */
class WritePageGuard : public PageGuard {
public:
    WritePageGuard(BufferPoolManager* bpm, page_id_t page_id)
        : PageGuard(bpm, page_id, true) {}
};

/**
 * ReadPageGuard - Convenience alias for read-locked pages.
 */
class ReadPageGuard : public PageGuard {
public:
    ReadPageGuard(BufferPoolManager* bpm, page_id_t page_id)
        : PageGuard(bpm, page_id, false) {}
};

} // namespace francodb

