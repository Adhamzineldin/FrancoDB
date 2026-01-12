#pragma once

#include <mutex>
#include <list>
#include <unordered_map>
#include "common/config.h"
#include "buffer/replacer.h"
namespace francodb {

    /**
     * LRUReplacer tracks page usage.
     * It holds the "Frame IDs" of pages that are in the Buffer Pool but NOT currently pinned (in use).
     */
    class LRUReplacer : public Replacer{
    public:
        explicit LRUReplacer(size_t num_pages);
        ~LRUReplacer() override = default;

        /**
         * Remove the object that was accessed the least recently.
         * @param[out] frame_id The id of the frame that was victimized.
         * @return true if a victim was found, false if Replacer is empty.
         */
        bool Victim(frame_id_t *frame_id) override;

        /**
         * Pins a frame, meaning it is currently being used by a thread.
         * Pinned frames cannot be victimized.
         */
        void Pin(frame_id_t frame_id) override;

        /**
         * Unpins a frame, meaning the thread is done with it.
         * It effectively adds the frame to the LRU list, making it a candidate for eviction.
         */
        void Unpin(frame_id_t frame_id) override;

        /** @return the number of elements in the replacer that can be victimized */
        size_t Size() override;

    private:
        std::mutex mutex_;
        std::list<frame_id_t> lru_list_;
        std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> lru_map_;
        size_t capacity_;
    };

} // namespace francodb