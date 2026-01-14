#include <iostream>
#include <sstream>

#include "common/exception.h"
#include "storage/page/b_plus_tree_internal_page.h"
// FIX: Include the key definition so we can instantiate the template
#include "storage/index/index_key.h"

namespace francodb {

    /*****************************************************************************
     * HELPER METHODS AND UTILITIES
     *****************************************************************************/
    
    // Init: Setup the page headers
    template <typename KeyType, typename ValueType, typename KeyComparator>
    void BPlusTreeInternalPage<KeyType, ValueType, KeyComparator>::Init(page_id_t page_id, page_id_t parent_id, int max_size) {
        SetPageType(IndexPageType::INTERNAL_PAGE);
        SetPageId(page_id);
        SetParentPageId(parent_id);
        SetMaxSize(max_size);
        SetSize(0);
    }

    // KeyAt: Get key at index
    template <typename KeyType, typename ValueType, typename KeyComparator>
    KeyType BPlusTreeInternalPage<KeyType, ValueType, KeyComparator>::KeyAt(int index) const {
        return array_[index].first;
    }

    // SetKeyAt: Write key at index
    template <typename KeyType, typename ValueType, typename KeyComparator>
    void BPlusTreeInternalPage<KeyType, ValueType, KeyComparator>::SetKeyAt(int index, const KeyType &key) {
        array_[index].first = key;
    }

    // ValueAt: Get PageID at index
    template <typename KeyType, typename ValueType, typename KeyComparator>
    ValueType BPlusTreeInternalPage<KeyType, ValueType, KeyComparator>::ValueAt(int index) const {
        return array_[index].second;
    }

    // SetValueAt: Write PageID at index
    template <typename KeyType, typename ValueType, typename KeyComparator>
    void BPlusTreeInternalPage<KeyType, ValueType, KeyComparator>::SetValueAt(int index, const ValueType &value) {
        array_[index].second = value;
    }

    /*****************************************************************************
     * LOOKUP
     *****************************************************************************/
    
    // Lookup: Find which child PageID to follow for a given key
    template <typename KeyType, typename ValueType, typename KeyComparator>
    ValueType BPlusTreeInternalPage<KeyType, ValueType, KeyComparator>::Lookup(const KeyType &key, const KeyComparator &comparator) const {
        // Internal Node Structure:
        // [ (X, P0), (K1, P1), (K2, P2), ... ]
        // We want to find the last key K_i <= Key.
        // Or simpler: Find the first key strictly GREATER than our Key, then take the pointer BEFORE it.
        
        int size = GetSize();
        int max_size = GetMaxSize();
        
        // Bounds checking: validate size is reasonable
        // An internal node must have at least size 1 (one pointer)
        if (size < 1) {
            return static_cast<ValueType>(INVALID_PAGE_ID);
        }
        
        // Handle legacy indexes: if max_size is 0 (from old code), calculate a reasonable default
        // This allows old indexes loaded from disk to still work
        if (max_size <= 0) {
            // Calculate default max_size: (PAGE_SIZE - 24) / (sizeof(KeyType) + sizeof(ValueType))
            // For GenericKey<8> + page_id_t: (4096 - 24) / (8 + 4) = 4072 / 12 = 339
            constexpr int default_max_size = (4096 - 24) / (sizeof(KeyType) + sizeof(ValueType));
            max_size = default_max_size;
        }
        
        // Check if size exceeds max_size (but allow legacy pages with uninitialized max_size)
        if (size > max_size && max_size > 0) {
            return static_cast<ValueType>(INVALID_PAGE_ID);
        }
        
        // Special case: if size == 1, there's only one pointer (no keys to compare)
        // This can happen in a degenerate tree, return the only pointer
        if (size == 1) {
            return array_[0].second;
        }
        
        // Start from 1 because index 0 key is invalid/placeholder
        for (int i = 1; i < size; i++) {
            KeyType current_key = array_[i].first;
            
            // If current_key > search_key, then the search_key belongs to the PREVIOUS pointer.
            if (comparator(key, current_key) < 0) { // key < current_key
                return array_[i - 1].second;
            }
        }
        
        // If we went through the whole list and everything was smaller, it belongs to the last pointer.
        return array_[size - 1].second;
    }

    // --- EXPLICIT INSTANTIATION (Crucial for Linker) ---
    // We instantiate for GenericKey<8> and page_id_t (int) because Internal Pages point to PageIDs.
    template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;

} // namespace francodb