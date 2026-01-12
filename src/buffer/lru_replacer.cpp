#include "buffer/lru_replacer.h"

namespace francodb {

LRUReplacer::LRUReplacer(size_t num_pages) : capacity_(num_pages) {}

/**
 * Victim = "Kick someone out"
 * We look at the FRONT of the list (Least Recently Used).
 * If the list is empty, it means everyone is currently pinned (busy), so we can't evict anyone.
 */
bool LRUReplacer::Victim(frame_id_t *frame_id) {
    std::lock_guard<std::mutex> guard(mutex_);

    if (lru_list_.empty()) {
        return false;
    }

    // 1. Pick the guy at the front (He is the oldest/least used)
    frame_id_t victim_frame = lru_list_.front();

    // 2. Remove him from our records
    lru_list_.pop_front();
    lru_map_.erase(victim_frame);

    // 3. Tell the caller who the victim is
    *frame_id = victim_frame;
    return true;
}

/**
 * Pin = "This page is being used right now!"
 * When a thread pins a page, we must REMOVE it from the LRU Replacer.
 * Why? Because you cannot evict a page while someone is reading it!
 */
void LRUReplacer::Pin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> guard(mutex_);

    // If it's in the list, remove it. 
    // If it's not in the list (already pinned), do nothing.
    if (lru_map_.find(frame_id) != lru_map_.end()) {
        lru_list_.erase(lru_map_[frame_id]);
        lru_map_.erase(frame_id);
    }
}

/**
 * Unpin = "I am done with this page."
 * When a thread finishes, it unpins the page.
 * We add it to the BACK of the list (Most Recently Used).
 * It is now safe to evict again, but since it was just used, it will be the last one chosen.
 */
void LRUReplacer::Unpin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> guard(mutex_);

    // If already in LRU, remove old position
    auto it = lru_map_.find(frame_id);
    if (it != lru_map_.end()) {
        lru_list_.erase(it->second);
        lru_map_.erase(it);
    }
    
    // If we are over capacity (shouldn't happen if BPM logic is correct, but safe to check)
    if (lru_list_.size() >= capacity_) {
        return; 
    }

    // Add to the Back (MRU position)
    lru_list_.push_back(frame_id);
    
    // Update the map to point to the new last element
    // std::prev(lru_list_.end()) gives us the iterator to the element we just pushed
    lru_map_[frame_id] = std::prev(lru_list_.end());
}

size_t LRUReplacer::Size() {
    std::lock_guard<std::mutex> guard(mutex_);
    return lru_list_.size();
}

} // namespace francodb