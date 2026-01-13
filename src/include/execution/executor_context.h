#pragma once

#include "catalog/catalog.h"
#include "buffer/buffer_pool_manager.h"

namespace francodb {

    /**
     * ExecutorContext holds global state (Catalog, BPM) that all executors need.
     */
    class ExecutorContext {
    public:
        ExecutorContext(Catalog *catalog, BufferPoolManager *bpm)
            : catalog_(catalog), bpm_(bpm) {}

        Catalog *GetCatalog() { return catalog_; }
        BufferPoolManager *GetBufferPoolManager() { return bpm_; }

    private:
        Catalog *catalog_;
        BufferPoolManager *bpm_;
    };

} // namespace francodb