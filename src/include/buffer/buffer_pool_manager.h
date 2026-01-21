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

    // Forward declaration
    class LogManager;

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
        
        /**
         * Set the log manager for WAL protocol enforcement.
         * When set, FlushPage will ensure the log is flushed
         * up to the page's LSN before writing data.
         */
        void SetLogManager(LogManager* log_manager) { log_manager_ = log_manager; }

        void Clear();

    private:
        bool FindFreeFrame(frame_id_t *out_frame_id);

        size_t pool_size_;
        Page *pages_;
        DiskManager *disk_manager_;
        LogManager *log_manager_ = nullptr;  // Optional - for WAL protocol
    
        // Changed from LRUReplacer* to Replacer* (Polymorphism!)
        Replacer *replacer_; 
    
        std::unordered_map<page_id_t, frame_id_t> page_table_;
        std::list<frame_id_t> free_list_;
        std::mutex latch_;
    
        // Next Page ID Tracker
        std::atomic<page_id_t> next_page_id_ = 0;
    };

} // namespace francodb