#pragma once

#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <atomic> // Don't forget atomic!

#include "storage/disk/disk_manager.h"
#include "storage/page/page.h"
#include "buffer/replacer.h" // Use the generic interface

namespace francodb {

    class BufferPoolManager {
    public:
        BufferPoolManager(size_t pool_size, DiskManager *disk_manager);
        ~BufferPoolManager();

        Page *FetchPage(page_id_t page_id);
        bool UnpinPage(page_id_t page_id, bool is_dirty);
        bool FlushPage(page_id_t page_id);
        Page *NewPage(page_id_t *page_id);
        bool DeletePage(page_id_t page_id);
        void FlushAllPages();
        
        DiskManager *GetDiskManager() { return disk_manager_; }

        void Clear();

    private:
        bool FindFreeFrame(frame_id_t *out_frame_id);

        size_t pool_size_;
        Page *pages_;
        DiskManager *disk_manager_;
    
        // Changed from LRUReplacer* to Replacer* (Polymorphism!)
        Replacer *replacer_; 
    
        std::unordered_map<page_id_t, frame_id_t> page_table_;
        std::list<frame_id_t> free_list_;
        std::mutex latch_;
    
        // Next Page ID Tracker
        std::atomic<page_id_t> next_page_id_ = 0;
    };

} // namespace francodb