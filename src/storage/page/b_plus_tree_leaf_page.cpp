#include <sstream>
#include "common/exception.h"
#include "common/rid.h"
#include "storage/page/b_plus_tree_leaf_page.h"
// FIX: Include key definition
#include "storage/index/index_key.h" 

namespace francodb {

    /*****************************************************************************
     * HELPER METHODS AND UTILITIES
     *****************************************************************************/
    
    // Init
    template <typename KeyType, typename ValueType, typename KeyComparator>
    void BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>::Init(page_id_t page_id, page_id_t parent_id, int max_size) {
        SetPageType(IndexPageType::LEAF_PAGE);
        SetPageId(page_id);
        SetParentPageId(parent_id);
        SetMaxSize(max_size);
        SetSize(0);
        next_page_id_ = INVALID_PAGE_ID;
    }

    // Get Next Page ID (for scanning)
    template <typename KeyType, typename ValueType, typename KeyComparator>
    page_id_t BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>::GetNextPageId() const {
        return next_page_id_;
    }

    // Set Next Page ID
    template <typename KeyType, typename ValueType, typename KeyComparator>
    void BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>::SetNextPageId(page_id_t next_page_id) {
        next_page_id_ = next_page_id;
    }

    // KeyAt
    template <typename KeyType, typename ValueType, typename KeyComparator>
    KeyType BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>::KeyAt(int index) const {
        return array_[index].first;
    }

    // SetKeyAt
    template <typename KeyType, typename ValueType, typename KeyComparator>
    void BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>::SetKeyAt(int index, const KeyType &key) {
        array_[index].first = key;
    }
    
    // KeyIndex: Find the index of a specific key
    template <typename KeyType, typename ValueType, typename KeyComparator>
    int BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>::KeyIndex(const KeyType &key, const KeyComparator &comparator) const {
        int size = GetSize();
        for (int i = 0; i < size; i++) {
            if (comparator(array_[i].first, key) == 0) {
                return i;
            }
        }
        return -1; // Not found
    }

    // ValueAt
    template <typename KeyType, typename ValueType, typename KeyComparator>
    ValueType BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>::ValueAt(int index) const {
        return array_[index].second;
    }

    // SetValueAt
    template <typename KeyType, typename ValueType, typename KeyComparator>
    void BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>::SetValueAt(int index, const ValueType &value) {
        array_[index].second = value;
    }

    /*****************************************************************************
     * LOOKUP
     *****************************************************************************/
    
    // Lookup: Find the value (RID) for a specific key
    template <typename KeyType, typename ValueType, typename KeyComparator>
    bool BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>::Lookup(const KeyType &key, ValueType &value, const KeyComparator &comparator) const {
        int size = GetSize();
        int max_size = GetMaxSize();
        
        // Bounds checking: validate size is reasonable
        if (size < 0) {
            return false; // Invalid size, cannot lookup
        }
        
        // Handle legacy indexes: if max_size is 0 (from old code), calculate a reasonable default
        // This allows old indexes loaded from disk to still work
        if (max_size <= 0) {
            // Calculate default max_size: (PAGE_SIZE - 28) / (sizeof(KeyType) + sizeof(ValueType))
            // For GenericKey<8> + RID: (4096 - 28) / (8 + 8) = 4068 / 16 = 254
            constexpr int default_max_size = (4096 - 28) / (sizeof(KeyType) + sizeof(ValueType));
            max_size = default_max_size;
        }
        
        // Check if size exceeds max_size (but allow legacy pages with uninitialized max_size)
        if (size > max_size && max_size > 0) {
            return false; // Size exceeds maximum, likely corruption
        }
        
        for (int i = 0; i < size; i++) {
            if (comparator(array_[i].first, key) == 0) { // Found match
                value = array_[i].second;
                return true;
            }
        }
        return false;
    }

    // --- EXPLICIT INSTANTIATION ---
    // Instantiate for GenericKey<8> and RID (Leaf pages store RIDs)
    template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>;

} // namespace francodb