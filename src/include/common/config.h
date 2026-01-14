#pragma once

#include <cstdint>

namespace francodb {
    // --- Type Aliases ---
    // We use signed integers so we can check if(id < 0) for errors.
    using page_id_t = int32_t;
    using frame_id_t = int32_t;


    // --- Storage Layout ---
    // 4KB page size matches typical OS page size
    static constexpr uint32_t PAGE_SIZE = 4096;

    // --- Buffer Pool ---
    // Default number of pages the BufferPoolManager can hold in memory.
    // Used by the server main.
    static constexpr uint32_t BUFFER_POOL_SIZE = 100;

    /**
     * INVALID_PAGE_ID: Represents a null pointer for pages.
     */
    static constexpr page_id_t INVALID_PAGE_ID = -1;
} // namespace francodb
