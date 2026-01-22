#pragma once

#include <array>
#include <mutex>
#include <unordered_map>
#include <list>
#include <atomic>
#include <functional>
#include "common/config.h"
#include "storage/page/page.h"
#include "buffer/replacer.h"
#include "storage/disk/disk_manager.h"

namespace francodb {

// Forward declaration
class LogManager;

/**
 * PartitionedBufferPoolManager - High-concurrency buffer pool with partitioned latching
 * 
 * PROBLEM SOLVED:
 * - Single mutex in BufferPoolManager causes contention under high concurrency
 * - All page fetches serialize on one lock
 * 
 * SOLUTION:
 * - Partition pages across N independent buffer pools
 * - Each partition has its own latch
 * - page_id % N determines which partition handles a page
 * - Reduces lock contention by factor of N
 * 
 * Based on PostgreSQL's buffer partition design.
 */
class PartitionedBufferPoolManager {
public:
    /**
     * Constructor
     * 
     * @param pool_size Total number of pages across all partitions
     * @param disk_manager Disk manager for I/O
     * @param num_partitions Number of partitions (default: 16)
     */
    PartitionedBufferPoolManager(size_t pool_size, DiskManager* disk_manager,
                                  size_t num_partitions = BUFFER_POOL_PARTITIONS)
        : total_pool_size_(pool_size),
          num_partitions_(num_partitions),
          disk_manager_(disk_manager),
          log_manager_(nullptr) {
        
        // Calculate pages per partition
        size_t pages_per_partition = pool_size / num_partitions;
        if (pages_per_partition < 1) {
            pages_per_partition = 1;
        }
        
        // Initialize partitions
        partitions_.resize(num_partitions);
        for (size_t i = 0; i < num_partitions; i++) {
            partitions_[i].pages = new Page[pages_per_partition];
            partitions_[i].pool_size = pages_per_partition;
            
            // Initialize free list
            for (size_t j = 0; j < pages_per_partition; j++) {
                partitions_[i].free_list.push_back(static_cast<frame_id_t>(j));
            }
            
            // Create replacer (LRU for each partition)
            partitions_[i].replacer = CreateReplacer(pages_per_partition);
        }
        
        // Initialize next page ID
        next_page_id_ = disk_manager->GetNumPages();
        if (next_page_id_ == 0) {
            next_page_id_ = 1;
        }
    }
    
    ~PartitionedBufferPoolManager() {
        FlushAllPages();
        
        for (auto& partition : partitions_) {
            delete[] partition.pages;
            delete partition.replacer;
        }
    }
    
    // ========================================================================
    // PAGE OPERATIONS
    // ========================================================================
    
    /**
     * Fetch a page from the buffer pool.
     * Routes to appropriate partition based on page_id.
     */
    Page* FetchPage(page_id_t page_id) {
        if (page_id == INVALID_PAGE_ID || page_id < 0) {
            return nullptr;
        }
        
        size_t partition_idx = GetPartitionIndex(page_id);
        Partition& partition = partitions_[partition_idx];
        
        std::lock_guard<std::mutex> lock(partition.latch);
        
        // Check if page is already in this partition's buffer
        auto it = partition.page_table.find(page_id);
        if (it != partition.page_table.end()) {
            frame_id_t frame_id = it->second;
            Page* page = &partition.pages[frame_id];
            page->IncrementPinCount();
            partition.replacer->Pin(frame_id);
            return page;
        }
        
        // Need to load page from disk
        frame_id_t frame_id = -1;
        if (!FindFreeFrame(partition, &frame_id)) {
            return nullptr;  // No frames available
        }
        
        Page* page = &partition.pages[frame_id];
        page->Init(page_id);
        
        try {
            disk_manager_->ReadPage(page_id, page->GetData());
        } catch (...) {
            page->Init(INVALID_PAGE_ID);
            partition.free_list.push_back(frame_id);
            return nullptr;
        }
        
        partition.page_table[page_id] = frame_id;
        page->IncrementPinCount();
        partition.replacer->Pin(frame_id);
        
        return page;
    }
    
    /**
     * Create a new page.
     */
    Page* NewPage(page_id_t* page_id) {
        // Allocate new page ID atomically
        *page_id = next_page_id_.fetch_add(1);
        
        size_t partition_idx = GetPartitionIndex(*page_id);
        Partition& partition = partitions_[partition_idx];
        
        std::lock_guard<std::mutex> lock(partition.latch);
        
        frame_id_t frame_id = -1;
        if (!FindFreeFrame(partition, &frame_id)) {
            return nullptr;
        }
        
        Page* page = &partition.pages[frame_id];
        page->Init(*page_id);
        
        partition.page_table[*page_id] = frame_id;
        page->IncrementPinCount();
        
        return page;
    }
    
    /**
     * Unpin a page.
     */
    bool UnpinPage(page_id_t page_id, bool is_dirty) {
        if (page_id == INVALID_PAGE_ID) {
            return false;
        }
        
        size_t partition_idx = GetPartitionIndex(page_id);
        Partition& partition = partitions_[partition_idx];
        
        std::lock_guard<std::mutex> lock(partition.latch);
        
        auto it = partition.page_table.find(page_id);
        if (it == partition.page_table.end()) {
            return false;
        }
        
        frame_id_t frame_id = it->second;
        Page* page = &partition.pages[frame_id];
        
        if (is_dirty) {
            page->SetDirty(true);
        }
        
        if (page->GetPinCount() <= 0) {
            return false;
        }
        
        page->DecrementPinCount();
        if (page->GetPinCount() == 0) {
            partition.replacer->Unpin(frame_id);
        }
        
        return true;
    }
    
    /**
     * Flush a page to disk.
     */
    bool FlushPage(page_id_t page_id) {
        if (page_id == INVALID_PAGE_ID || page_id == 0) {
            return false;
        }
        
        size_t partition_idx = GetPartitionIndex(page_id);
        Partition& partition = partitions_[partition_idx];
        
        std::lock_guard<std::mutex> lock(partition.latch);
        
        auto it = partition.page_table.find(page_id);
        if (it == partition.page_table.end()) {
            return false;
        }
        
        frame_id_t frame_id = it->second;
        Page* page = &partition.pages[frame_id];
        
        // WAL protocol: flush log before data
        if (log_manager_ != nullptr) {
            lsn_t page_lsn = page->GetPageLSN();
            if (page_lsn != INVALID_LSN) {
                log_manager_->FlushToLSN(page_lsn);
            }
        }
        
        disk_manager_->WritePage(page_id, page->GetData());
        page->SetDirty(false);
        
        return true;
    }
    
    /**
     * Delete a page from the buffer pool.
     */
    bool DeletePage(page_id_t page_id) {
        if (page_id == INVALID_PAGE_ID) {
            return true;
        }
        
        size_t partition_idx = GetPartitionIndex(page_id);
        Partition& partition = partitions_[partition_idx];
        
        std::lock_guard<std::mutex> lock(partition.latch);
        
        auto it = partition.page_table.find(page_id);
        if (it == partition.page_table.end()) {
            return true;
        }
        
        frame_id_t frame_id = it->second;
        Page* page = &partition.pages[frame_id];
        
        if (page->GetPinCount() > 0) {
            return false;  // Page still in use
        }
        
        partition.page_table.erase(it);
        page->Init(INVALID_PAGE_ID);
        partition.free_list.push_back(frame_id);
        
        return true;
    }
    
    /**
     * Flush all dirty pages to disk.
     */
    void FlushAllPages() {
        // Flush log first
        if (log_manager_ != nullptr) {
            log_manager_->Flush(true);
        }
        
        // Flush all partitions
        for (auto& partition : partitions_) {
            std::lock_guard<std::mutex> lock(partition.latch);
            
            for (const auto& [page_id, frame_id] : partition.page_table) {
                if (page_id == 0) continue;
                
                Page* page = &partition.pages[frame_id];
                if (page->IsDirty()) {
                    disk_manager_->WritePage(page_id, page->GetData());
                    page->SetDirty(false);
                }
            }
        }
    }
    
    // ========================================================================
    // CONFIGURATION
    // ========================================================================
    
    void SetLogManager(LogManager* log_manager) {
        log_manager_ = log_manager;
    }
    
    DiskManager* GetDiskManager() { return disk_manager_; }
    
    size_t GetTotalPoolSize() const { return total_pool_size_; }
    size_t GetNumPartitions() const { return num_partitions_; }
    
    /**
     * Get statistics for monitoring.
     */
    struct Stats {
        size_t total_pages;
        size_t used_pages;
        size_t dirty_pages;
        size_t pinned_pages;
    };
    
    Stats GetStats() const {
        Stats stats = {0, 0, 0, 0};
        
        for (const auto& partition : partitions_) {
            std::lock_guard<std::mutex> lock(partition.latch);
            
            stats.total_pages += partition.pool_size;
            stats.used_pages += partition.page_table.size();
            
            for (const auto& [_, frame_id] : partition.page_table) {
                const Page* page = &partition.pages[frame_id];
                if (page->IsDirty()) stats.dirty_pages++;
                if (page->GetPinCount() > 0) stats.pinned_pages++;
            }
        }
        
        return stats;
    }

private:
    /**
     * Partition structure - independent buffer pool segment
     */
    struct Partition {
        mutable std::mutex latch;
        Page* pages = nullptr;
        size_t pool_size = 0;
        Replacer* replacer = nullptr;
        std::unordered_map<page_id_t, frame_id_t> page_table;
        std::list<frame_id_t> free_list;
    };
    
    /**
     * Get partition index for a page ID.
     */
    size_t GetPartitionIndex(page_id_t page_id) const {
        return static_cast<size_t>(page_id) % num_partitions_;
    }
    
    /**
     * Find a free frame in a partition.
     */
    bool FindFreeFrame(Partition& partition, frame_id_t* out_frame_id) {
        if (!partition.free_list.empty()) {
            *out_frame_id = partition.free_list.front();
            partition.free_list.pop_front();
            return true;
        }
        
        if (partition.replacer->Victim(out_frame_id)) {
            Page* victim = &partition.pages[*out_frame_id];
            
            if (victim->IsDirty()) {
                // WAL protocol
                if (log_manager_ != nullptr) {
                    lsn_t page_lsn = victim->GetPageLSN();
                    if (page_lsn != INVALID_LSN) {
                        log_manager_->FlushToLSN(page_lsn);
                    }
                }
                disk_manager_->WritePage(victim->GetPageId(), victim->GetData());
            }
            
            partition.page_table.erase(victim->GetPageId());
            return true;
        }
        
        return false;
    }
    
    /**
     * Create a replacer for a partition.
     */
    Replacer* CreateReplacer(size_t capacity);  // Implemented in .cpp
    
    // ========================================================================
    // DATA MEMBERS
    // ========================================================================
    
    size_t total_pool_size_;
    size_t num_partitions_;
    DiskManager* disk_manager_;
    LogManager* log_manager_;
    
    std::vector<Partition> partitions_;
    std::atomic<page_id_t> next_page_id_;
};

} // namespace francodb

