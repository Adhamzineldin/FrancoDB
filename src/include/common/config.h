#pragma once

#include <cstdint>

namespace francodb {
    // --- Type Aliases ---
    // We use signed integers so we can check if(id < 0) for errors.
    using page_id_t = int32_t;
    using frame_id_t = int32_t;


    /**
     * PAGE_SIZE: The size of a database page.
     * 4KB is standard because it matches the default OS page size and disk sector size.
     * This minimizes "torn writes" (where half a page is written before a crash).
     */
    static constexpr uint32_t PAGE_SIZE = 4096;

    /**
     * INVALID_PAGE_ID: Represents a null pointer for pages.
     */
    static constexpr uint32_t INVALID_PAGE_ID = UINT32_MAX;
} // namespace francodb
