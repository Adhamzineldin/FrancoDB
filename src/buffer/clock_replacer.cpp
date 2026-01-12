#include "buffer/clock_replacer.h"

namespace francodb {

ClockReplacer::ClockReplacer(size_t num_pages) : capacity_(num_pages) {
    // Initialize vector with empty frames
    frames_.resize(capacity_);
}

bool ClockReplacer::Victim(frame_id_t *frame_id) {
    std::lock_guard<std::mutex> guard(mutex_);

    // Check if there are ANY frames to evict to avoid infinite loop
    bool any_valid = false;
    for (const auto &frame : frames_) {
        if (frame.is_valid) {
            any_valid = true;
            break;
        }
    }
    if (!any_valid) return false;

    // Spin the "Clock Hand"
    while (true) {
        // Wrap around (Circle behavior)
        if (clock_hand_ >= capacity_) {
            clock_hand_ = 0;
        }

        // Current candidate
        FrameInfo &frame = frames_[clock_hand_];

        if (frame.is_valid) {
            if (frame.ref_bit) {
                // Give Second Chance: Turn off bit, leave it alone, move hand
                frame.ref_bit = false;
            } else {
                // Found Victim: ref_bit is false
                *frame_id = static_cast<frame_id_t>(clock_hand_);
                
                // Remove from replacer (It's about to be evicted/pinned by manager)
                frame.is_valid = false; 
                frame.ref_bit = false;
                
                // Move hand forward for next time
                clock_hand_++;
                return true;
            }
        }

        clock_hand_++;
    }
}

void ClockReplacer::Pin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    
    // Frame is being used by a thread -> Remove from Clock
    if (static_cast<size_t>(frame_id) < capacity_) {
        frames_[frame_id].is_valid = false;
        frames_[frame_id].ref_bit = false;
    }
}

void ClockReplacer::Unpin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> guard(mutex_);

    // Thread is done -> Add to Clock
    if (static_cast<size_t>(frame_id) < capacity_) {
        if (!frames_[frame_id].is_valid) {
             frames_[frame_id].is_valid = true;
             // Give it a strong start (Second Chance) so it isn't evicted immediately
             frames_[frame_id].ref_bit = true; 
        }
    }
}

size_t ClockReplacer::Size() {
    std::lock_guard<std::mutex> guard(mutex_);
    size_t size = 0;
    for (const auto &frame : frames_) {
        if (frame.is_valid) size++;
    }
    return size;
}

} // namespace francodb