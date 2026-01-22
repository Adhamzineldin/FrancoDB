#pragma once

#include <cstdint>

namespace francodb {
    // ========================================================================
    // TYPE ALIASES
    // ========================================================================
    
    // We use signed integers so we can check if(id < 0) for errors.
    using page_id_t = int32_t;
    using frame_id_t = int32_t;
    using txn_id_t = int32_t;
    using lsn_t = int32_t;
    using slot_id_t = uint32_t;

    // ========================================================================
    // STORAGE LAYOUT
    // ========================================================================
    
    // 4KB page size matches typical OS page size
    static constexpr uint32_t PAGE_SIZE = 4096;
    
    // Reserved page IDs
    static constexpr page_id_t METADATA_PAGE_ID = 0;    // Database metadata
    static constexpr page_id_t CATALOG_PAGE_ID = 1;     // System catalog
    static constexpr page_id_t BITMAP_PAGE_ID = 2;      // Free space bitmap
    static constexpr page_id_t FIRST_DATA_PAGE_ID = 3;  // First user data page
    
    // Invalid markers
    static constexpr page_id_t INVALID_PAGE_ID = -1;
    static constexpr txn_id_t INVALID_TXN_ID = -1;
    static constexpr lsn_t INVALID_LSN = -1;
    static constexpr slot_id_t INVALID_SLOT_ID = UINT32_MAX;

    // ========================================================================
    // BUFFER POOL
    // ========================================================================
    
    // Default number of pages the BufferPoolManager can hold in memory.
    static constexpr uint32_t BUFFER_POOL_SIZE = 100;
    
    // Number of buffer pool partitions for reduced contention
    static constexpr size_t BUFFER_POOL_PARTITIONS = 16;
    
    // Eviction batch size (for background eviction)
    static constexpr size_t EVICTION_BATCH_SIZE = 8;

    // ========================================================================
    // TABLE PAGE LAYOUT
    // ========================================================================
    
    // Table page header:
    // [page_id (4)] [prev_page (4)] [next_page (4)] [free_space_ptr (4)]
    // [tuple_count (4)] [checksum (4)] = 24 bytes
    static constexpr size_t TABLE_PAGE_HEADER_SIZE = 24;
    
    // Slot entry: [offset (4)] [size (4)] = 8 bytes
    static constexpr size_t TABLE_PAGE_SLOT_SIZE = 8;
    
    // Maximum tuple size (page size - header - one slot)
    static constexpr size_t MAX_TUPLE_SIZE = PAGE_SIZE - TABLE_PAGE_HEADER_SIZE - TABLE_PAGE_SLOT_SIZE;

    // ========================================================================
    // INDEX (B+ TREE)
    // ========================================================================
    
    // Default key size for index keys
    static constexpr size_t DEFAULT_KEY_SIZE = 8;
    
    // Maximum key size supported
    static constexpr size_t MAX_KEY_SIZE = 256;
    
    // B+ tree node fanout (affects tree height and I/O)
    static constexpr size_t BTREE_MAX_FANOUT = 128;
    static constexpr size_t BTREE_MIN_FANOUT = BTREE_MAX_FANOUT / 2;

    // ========================================================================
    // LOGGING & RECOVERY
    // ========================================================================
    
    // Log buffer size (64KB default)
    static constexpr size_t LOG_BUFFER_SIZE = 64 * 1024;
    
    // Checkpoint interval (in number of log records)
    static constexpr size_t CHECKPOINT_INTERVAL = 1000;
    
    // Checkpoint interval (in milliseconds)
    static constexpr uint64_t CHECKPOINT_INTERVAL_MS = 60000;  // 1 minute
    
    // Maximum log record size
    static constexpr size_t MAX_LOG_RECORD_SIZE = PAGE_SIZE;

    // ========================================================================
    // CONCURRENCY
    // ========================================================================
    
    // Maximum concurrent transactions
    static constexpr size_t MAX_TRANSACTIONS = 1024;
    
    // Lock table size (hash buckets)
    static constexpr size_t LOCK_TABLE_SIZE = 1024;
    
    // Deadlock detection interval (milliseconds)
    static constexpr uint64_t DEADLOCK_DETECTION_INTERVAL_MS = 1000;

    // ========================================================================
    // NETWORK
    // ========================================================================
    
    // Default server port
    static constexpr uint16_t DEFAULT_PORT = 2501;
    
    // Maximum client connections
    static constexpr size_t MAX_CONNECTIONS = 100;
    
    // Connection timeout (seconds)
    static constexpr uint32_t CONNECTION_TIMEOUT_SEC = 30;
    
    // Maximum query size (1MB)
    static constexpr size_t MAX_QUERY_SIZE = 1024 * 1024;

} // namespace francodb


