#pragma once

#include "common/config.h"

namespace francodb {

    /**
     * Abstract Base Class for replacement policies (LRU, Clock, FIFO).
     */
    class Replacer {
    public:
        virtual ~Replacer() = default;

        virtual bool Victim(frame_id_t *frame_id) = 0;
        virtual void Pin(frame_id_t frame_id) = 0;
        virtual void Unpin(frame_id_t frame_id) = 0;
        virtual size_t Size() = 0;
    };

} // namespace francodb