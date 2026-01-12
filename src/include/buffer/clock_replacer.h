#pragma once

#include <vector>
#include <mutex>
#include "buffer/replacer.h"

namespace francodb {

    /**
     * ClockReplacer implements the Clock (Second Chance) algorithm.
     * It is generally more efficient than LRU for large buffer pools because it avoids 
     * strict list locking overhead.
     */
    class ClockReplacer : public Replacer {
    public:
        explicit ClockReplacer(size_t num_pages);
        ~ClockReplacer() override = default;

        bool Victim(frame_id_t *frame_id) override;
        void Pin(frame_id_t frame_id) override;
        void Unpin(frame_id_t frame_id) override;
        size_t Size() override;

    private:
        // How many frames this replacer manages
        size_t capacity_;
    
        // The "Clock Hand" (points to the next candidate for eviction)
        size_t clock_hand_ = 0;
    
        // Status of each frame in the replacer
        // We need to track:
        // 1. Is it in the replacer? (Not pinned)
        // 2. The Reference Bit (Has it been accessed?)
        struct FrameInfo {
            bool is_valid = false;  // Is it currently in the Clock (i.e., unpinned)?
            bool ref_bit = false;   // The "Second Chance" bit
        };
    
        std::vector<FrameInfo> frames_;
        std::mutex mutex_;
    };

} // namespace francodb