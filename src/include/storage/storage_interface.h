#pragma once

#include <memory>
#include "common/config.h"
#include "storage/tuple.h"
#include "common/rid.h"

namespace francodb {

// Forward declarations
class Transaction;
class Schema;

/**
 * ITableStorage - Abstract interface for table storage engines
 * 
 * PROBLEM SOLVED:
 * - Decouples executors from concrete storage implementations
 * - Allows swapping storage engines (heap, column store, LSM tree)
 * - Enables testing with mock storage
 * 
 * DEPENDENCY INVERSION PRINCIPLE:
 * High-level modules (Executors) depend on abstractions (ITableStorage),
 * not concrete implementations (TableHeap).
 */
class ITableStorage {
public:
    virtual ~ITableStorage() = default;
    
    // ========================================================================
    // TUPLE OPERATIONS
    // ========================================================================
    
    /**
     * Insert a tuple into the table.
     * 
     * @param tuple The tuple to insert
     * @param rid Output: the RID where the tuple was inserted
     * @param txn Transaction context (may be nullptr)
     * @return true if successful
     */
    virtual bool InsertTuple(const Tuple& tuple, RID* rid, Transaction* txn) = 0;
    
    /**
     * Get a tuple by RID.
     * 
     * @param rid The RID of the tuple
     * @param tuple Output: the tuple data
     * @param txn Transaction context (may be nullptr)
     * @return true if tuple exists and is visible
     */
    virtual bool GetTuple(const RID& rid, Tuple* tuple, Transaction* txn) = 0;
    
    /**
     * Mark a tuple as deleted.
     * 
     * @param rid The RID of the tuple to delete
     * @param txn Transaction context (may be nullptr)
     * @return true if successful
     */
    virtual bool MarkDelete(const RID& rid, Transaction* txn) = 0;
    
    /**
     * Unmark a deleted tuple (for rollback).
     * 
     * @param rid The RID of the tuple to undelete
     * @param txn Transaction context (may be nullptr)
     * @return true if successful
     */
    virtual bool UnmarkDelete(const RID& rid, Transaction* txn) = 0;
    
    /**
     * Update a tuple (delete old + insert new).
     * 
     * @param tuple The new tuple data
     * @param rid The RID of the tuple to update
     * @param txn Transaction context (may be nullptr)
     * @return true if successful
     */
    virtual bool UpdateTuple(const Tuple& tuple, const RID& rid, Transaction* txn) = 0;
    
    // ========================================================================
    // ITERATION
    // ========================================================================
    
    /**
     * Iterator interface for table scanning
     */
    class Iterator {
    public:
        virtual ~Iterator() = default;
        virtual bool IsEnd() const = 0;
        virtual void Next() = 0;
        virtual Tuple GetTuple() const = 0;
        virtual RID GetRID() const = 0;
    };
    
    /**
     * Create an iterator for scanning the table.
     * 
     * @param txn Transaction context (may be nullptr)
     * @return Unique pointer to iterator
     */
    virtual std::unique_ptr<Iterator> CreateIterator(Transaction* txn) = 0;
    
    // ========================================================================
    // METADATA
    // ========================================================================
    
    /**
     * Get the first page ID of the table.
     */
    virtual page_id_t GetFirstPageId() const = 0;
};

/**
 * IBufferManager - Abstract interface for buffer pool management
 * 
 * Allows swapping buffer pool implementations (LRU, Clock, LRU-K, etc.)
 */
class IBufferManager {
public:
    virtual ~IBufferManager() = default;
    
    /**
     * Fetch a page from the buffer pool.
     * 
     * @param page_id The page to fetch
     * @return Pointer to the page, or nullptr if not found
     */
    virtual class Page* FetchPage(page_id_t page_id) = 0;
    
    /**
     * Create a new page.
     * 
     * @param page_id Output: the ID of the new page
     * @return Pointer to the new page, or nullptr on failure
     */
    virtual class Page* NewPage(page_id_t* page_id) = 0;
    
    /**
     * Unpin a page (mark as no longer in use).
     * 
     * @param page_id The page to unpin
     * @param is_dirty Whether the page was modified
     * @return true if successful
     */
    virtual bool UnpinPage(page_id_t page_id, bool is_dirty) = 0;
    
    /**
     * Flush a page to disk.
     * 
     * @param page_id The page to flush
     * @return true if successful
     */
    virtual bool FlushPage(page_id_t page_id) = 0;
    
    /**
     * Delete a page from the buffer pool.
     * 
     * @param page_id The page to delete
     * @return true if successful
     */
    virtual bool DeletePage(page_id_t page_id) = 0;
    
    /**
     * Flush all dirty pages to disk.
     */
    virtual void FlushAllPages() = 0;
};

/**
 * IDiskManager - Abstract interface for disk I/O
 * 
 * Allows swapping disk implementations (file, memory, network, etc.)
 */
class IDiskManager {
public:
    virtual ~IDiskManager() = default;
    
    /**
     * Read a page from disk.
     * 
     * @param page_id The page to read
     * @param page_data Output buffer (must be PAGE_SIZE bytes)
     */
    virtual void ReadPage(page_id_t page_id, char* page_data) = 0;
    
    /**
     * Write a page to disk.
     * 
     * @param page_id The page to write
     * @param page_data Data to write (must be PAGE_SIZE bytes)
     */
    virtual void WritePage(page_id_t page_id, const char* page_data) = 0;
    
    /**
     * Get the number of pages on disk.
     */
    virtual int GetNumPages() const = 0;
};

} // namespace francodb

