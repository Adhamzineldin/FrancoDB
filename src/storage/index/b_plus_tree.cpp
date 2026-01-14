#include <string>
#include <vector>
#include <algorithm>
#include "common/exception.h"
#include "common/rid.h"
#include "storage/index/b_plus_tree.h"
#include "storage/index/index_key.h"

namespace francodb {

    // --- CONSTRUCTOR ---
    template<typename KeyType, typename ValueType, typename KeyComparator>
    BPlusTree<KeyType, ValueType, KeyComparator>::BPlusTree(
        std::string name, BufferPoolManager *buffer_pool_manager, const KeyComparator &comparator,
        int leaf_max_size, int internal_max_size)
        : index_name_(std::move(name)),
          root_page_id_(INVALID_PAGE_ID),
          buffer_pool_manager_(buffer_pool_manager),
          comparator_(comparator),
          leaf_max_size_(leaf_max_size),
          internal_max_size_(internal_max_size) {
    }

    // --- IS EMPTY ---
    template<typename KeyType, typename ValueType, typename KeyComparator>
    bool BPlusTree<KeyType, ValueType, KeyComparator>::IsEmpty() const {
        return root_page_id_ == INVALID_PAGE_ID;
    }

    /*****************************************************************************
     * SEARCH (Safe Mode: Global Read Lock)
     *****************************************************************************/
    template<typename KeyType, typename ValueType, typename KeyComparator>
    bool BPlusTree<KeyType, ValueType, KeyComparator>::GetValue(const KeyType &key,
                                                                std::vector<ValueType> *result,
                                                                Transaction *transaction) {
        (void)transaction;
        root_latch_.RLock(); // Global READ Lock
        
        try {
            if (IsEmpty()) {
                root_latch_.RUnlock();
                return false;
            }

            // Validate root_page_id_ before using it
            if (root_page_id_ == INVALID_PAGE_ID || root_page_id_ < 0) {
                root_latch_.RUnlock();
                return false;
            }

            Page *page = buffer_pool_manager_->FetchPage(root_page_id_);
            if (!page || !page->GetData()) { 
                root_latch_.RUnlock(); 
                return false; 
            }
            
            auto *node = reinterpret_cast<BPlusTreePage *>(page->GetData());
            if (!node) {
                buffer_pool_manager_->UnpinPage(root_page_id_, false);
                root_latch_.RUnlock();
                return false;
            }
            
            // Validate page type before accessing
            if (node->GetPageType() != IndexPageType::LEAF_PAGE && 
                node->GetPageType() != IndexPageType::INTERNAL_PAGE) {
                buffer_pool_manager_->UnpinPage(root_page_id_, false);
                root_latch_.RUnlock();
                return false; // Invalid page type
            }
            
            while (!node->IsLeafPage()) {
                auto *internal = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(node);
                if (!internal) {
                    buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
                    root_latch_.RUnlock();
                    return false;
                }
                
                page_id_t child_id = internal->Lookup(key, comparator_);
                
                // Validate child_id before fetching
                if (child_id == INVALID_PAGE_ID || child_id < 0) {
                    buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
                    root_latch_.RUnlock();
                    return false;
                }
                
                // Unpin parent BEFORE fetching child to prevent buffer clogging
                buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
                
                page = buffer_pool_manager_->FetchPage(child_id);
                if (!page || !page->GetData()) { 
                    root_latch_.RUnlock(); 
                    return false; 
                }
                node = reinterpret_cast<BPlusTreePage *>(page->GetData());
                if (!node) {
                    buffer_pool_manager_->UnpinPage(child_id, false);
                    root_latch_.RUnlock();
                    return false;
                }
            }

            auto *leaf = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *>(page->GetData());
            if (!leaf) {
                buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
                root_latch_.RUnlock();
                return false;
            }
            
            ValueType val;
            bool found = leaf->Lookup(key, val, comparator_);
            
            buffer_pool_manager_->UnpinPage(leaf->GetPageId(), false);
            root_latch_.RUnlock();

            if (found) result->push_back(val);
            return found;
        } catch (...) {
            // If any exception occurs (including access violations), unlock and return false
            root_latch_.RUnlock();
            return false;
        }
    }

    /*****************************************************************************
     * INSERTION (Safe Mode: Global Write Lock)
     *****************************************************************************/
    template<typename KeyType, typename ValueType, typename KeyComparator>
    bool BPlusTree<KeyType, ValueType, KeyComparator>::Insert(const KeyType &key, const ValueType &value,
                                                              Transaction *transaction) {
        root_latch_.WLock(); // Global WRITE Lock
        try {
            if (IsEmpty()) {
                StartNewTree(key, value);
                root_latch_.WUnlock();
                return true;
            }
            bool res = InsertIntoLeafPessimistic(key, value, transaction);
            root_latch_.WUnlock();
            return res;
        } catch (...) {
            root_latch_.WUnlock();
            throw; 
        }
    }

    /*****************************************************************************
     * REMOVAL (Safe Mode: Global Write Lock + Lazy Delete)
     *****************************************************************************/
    template<typename KeyType, typename ValueType, typename KeyComparator>
    void BPlusTree<KeyType, ValueType, KeyComparator>::Remove(const KeyType &key, Transaction *transaction) {
        (void)transaction;
        root_latch_.WLock(); // Global WRITE Lock

        try {
            if (IsEmpty()) {
                root_latch_.WUnlock();
                return;
            }

            // Validate root_page_id_ before using it
            if (root_page_id_ == INVALID_PAGE_ID || root_page_id_ < 0) {
                root_latch_.WUnlock();
                return;
            }

            Page *page = buffer_pool_manager_->FetchPage(root_page_id_);
            if (!page) { 
                root_latch_.WUnlock(); 
                return; 
            }

            auto *node = reinterpret_cast<BPlusTreePage *>(page->GetData());
            
            // Traverse to leaf (Unpin parent immediately)
            while (!node->IsLeafPage()) {
                auto *internal = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(node);
                page_id_t child_id = internal->Lookup(key, comparator_);
                
                // Validate child_id before fetching
                if (child_id == INVALID_PAGE_ID || child_id < 0) {
                    buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
                    root_latch_.WUnlock();
                    return;
                }
                
                buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
                
                page = buffer_pool_manager_->FetchPage(child_id);
                if (!page) { 
                    root_latch_.WUnlock(); 
                    return; 
                }
                node = reinterpret_cast<BPlusTreePage *>(page->GetData());
            }

            // We are at the Leaf. Safe to modify because we hold the Global Write Lock.
            auto *leaf = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *>(page->GetData());
            
            // 1. Find Key Index
            int size = leaf->GetSize();
            if (size <= 0) {
                // Invalid leaf size, skip removal
                buffer_pool_manager_->UnpinPage(leaf->GetPageId(), false);
                root_latch_.WUnlock();
                return;
            }
            
            int index = -1;
            for(int i=0; i<size; i++) {
                if (comparator_(key, leaf->KeyAt(i)) == 0) {
                    index = i;
                    break;
                }
            }

            // 2. Lazy Delete (Shift Left)
            if (index != -1) {
                // CRITICAL: Bounds check to prevent out-of-bounds access
                int max_size = leaf->GetMaxSize();
                if (index < 0 || index >= size || size > max_size) {
                    buffer_pool_manager_->UnpinPage(leaf->GetPageId(), false);
                    root_latch_.WUnlock();
                    return; // Invalid index or size, skip deletion
                }
                
                // Shift elements left to overwrite the deleted key
                for(int i=index; i<size-1; i++) {
                    // Bounds check: i+1 must be < size (which is <= max_size)
                    if (i+1 >= max_size) {
                        buffer_pool_manager_->UnpinPage(leaf->GetPageId(), false);
                        root_latch_.WUnlock();
                        return; // Out of bounds, skip deletion
                    }
                    leaf->SetKeyAt(i, leaf->KeyAt(i+1));
                    leaf->SetValueAt(i, leaf->ValueAt(i+1));
                }
                leaf->SetSize(size - 1);
                buffer_pool_manager_->UnpinPage(leaf->GetPageId(), true); // Mark Dirty
            } else {
                buffer_pool_manager_->UnpinPage(leaf->GetPageId(), false); // Not found, not dirty
            }

            root_latch_.WUnlock();
        } catch (...) {
            // If any exception occurs, make sure to unlock
            root_latch_.WUnlock();
            // Don't rethrow - just fail silently to prevent crashes
        }
    }

    // --- HELPER: Generic Insert ---
    template <typename N, typename K, typename V, typename C>
    void InsertGeneric(N *node, const K &key, const V &value, const C &cmp) {
        int size = node->GetSize();
        int max_size = node->GetMaxSize();
        
        // CRITICAL: Bounds check to prevent out-of-bounds array access
        if (size < 0 || size >= max_size || max_size <= 0) {
            throw Exception(ExceptionType::OUT_OF_RANGE, "Invalid size for insert");
        }
        
        int index = size;
        for (int i = 0; i < size; i++) {
            if (cmp(key, node->KeyAt(i)) < 0) {
                index = i; break;
            }
        }
        
        // CRITICAL: Ensure we don't write beyond array bounds
        // The array has max_size elements (indices 0 to max_size-1)
        // We're inserting, so size will become size+1, which must be <= max_size
        // The loop writes to indices [index+1, size], which must all be < max_size
        if (size >= max_size) {
            throw Exception(ExceptionType::OUT_OF_RANGE, "Page is full, cannot insert");
        }
        
        for (int i = size; i > index; i--) {
            // Bounds check: i must be < max_size (since size < max_size, and i <= size)
            if (i >= max_size) {
                throw Exception(ExceptionType::OUT_OF_RANGE, "Array index out of bounds during insert");
            }
            node->SetKeyAt(i, node->KeyAt(i - 1));
            node->SetValueAt(i, node->ValueAt(i - 1));
        }
        node->SetKeyAt(index, key);
        node->SetValueAt(index, value);
        node->SetSize(size + 1);
    }

    // --- HELPER: Pessimistic Insert Logic ---
    template <typename KeyType, typename ValueType, typename KeyComparator>
    bool BPlusTree<KeyType, ValueType, KeyComparator>::InsertIntoLeafPessimistic(
        const KeyType &key, const ValueType &value, Transaction *txn) {
        (void)txn;

        Page *leaf_page = buffer_pool_manager_->FetchPage(root_page_id_);
        if (!leaf_page) throw Exception(ExceptionType::OUT_OF_RANGE, "Cannot fetch root");

        auto *node = reinterpret_cast<BPlusTreePage *>(leaf_page->GetData());
        
        // Traverse to Leaf
        while (!node->IsLeafPage()) {
            auto *internal = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(node);
            page_id_t child_id = internal->Lookup(key, comparator_);
            
            // Validate child_id before fetching
            if (child_id == INVALID_PAGE_ID || child_id < 0) {
                buffer_pool_manager_->UnpinPage(leaf_page->GetPageId(), false);
                throw Exception(ExceptionType::OUT_OF_RANGE, "Invalid child page ID from Lookup");
            }
            
            buffer_pool_manager_->UnpinPage(leaf_page->GetPageId(), false);
            
            leaf_page = buffer_pool_manager_->FetchPage(child_id);
            if (!leaf_page) throw Exception(ExceptionType::OUT_OF_RANGE, "Cannot fetch child");
            
            node = reinterpret_cast<BPlusTreePage *>(leaf_page->GetData());
        }

        auto *leaf = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *>(leaf_page->GetData());
        
        ValueType v;
        if (leaf->Lookup(key, v, comparator_)) {
            buffer_pool_manager_->UnpinPage(leaf->GetPageId(), false);
            return false;
        }

        if (leaf->GetSize() < leaf->GetMaxSize()) {
            InsertGeneric(leaf, key, value, comparator_);
            buffer_pool_manager_->UnpinPage(leaf->GetPageId(), true);
            return true;
        }

        return SplitInsert(leaf, leaf_page, key, value);
    }

    // --- HELPER: Split Insert ---
    template <typename KeyType, typename ValueType, typename KeyComparator>
    bool BPlusTree<KeyType, ValueType, KeyComparator>::SplitInsert(
        BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *leaf,
        Page *leaf_page,
        const KeyType &key, 
        const ValueType &value) {
        (void) leaf_page; 
        
        page_id_t leaf_id = leaf->GetPageId();
        page_id_t parent_id = leaf->GetParentPageId();
        
        page_id_t new_leaf_id;
        Page *new_leaf_page = buffer_pool_manager_->NewPage(&new_leaf_id);
        if (!new_leaf_page) {
            buffer_pool_manager_->UnpinPage(leaf_id, false);
            throw Exception(ExceptionType::OUT_OF_RANGE, "Cannot allocate new leaf");
        }
        
        auto *new_leaf = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *>(new_leaf_page->GetData());
        new_leaf->Init(new_leaf_id, parent_id, leaf->GetMaxSize());

        std::vector<std::pair<KeyType, ValueType>> entries;
        for (int i = 0; i < leaf->GetSize(); i++) {
            entries.push_back({leaf->KeyAt(i), leaf->ValueAt(i)});
        }
        
        auto it = std::lower_bound(entries.begin(), entries.end(), key,
            [&](const auto &p, const auto &k) { return comparator_(p.first, k) < 0; });
        entries.insert(it, {key, value});

        int mid = entries.size() / 2;
        leaf->SetSize(mid);
        for (int i = 0; i < mid; i++) {
            leaf->SetKeyAt(i, entries[i].first);
            leaf->SetValueAt(i, entries[i].second);
        }
        
        new_leaf->SetSize(entries.size() - mid);
        for (size_t i = mid; i < entries.size(); i++) {
            new_leaf->SetKeyAt(i - mid, entries[i].first);
            new_leaf->SetValueAt(i - mid, entries[i].second);
        }
        
        new_leaf->SetNextPageId(leaf->GetNextPageId());
        leaf->SetNextPageId(new_leaf_id);
        
        KeyType split_key = new_leaf->KeyAt(0);
        
        buffer_pool_manager_->UnpinPage(leaf_id, true);
        buffer_pool_manager_->UnpinPage(new_leaf_id, true);
        
        if (parent_id == INVALID_PAGE_ID) {
            page_id_t new_root_id;
            Page *new_root_page = buffer_pool_manager_->NewPage(&new_root_id);
            if (!new_root_page) throw Exception(ExceptionType::OUT_OF_RANGE, "Cannot create new root");
            
            auto *new_root = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(new_root_page->GetData());
            new_root->Init(new_root_id, INVALID_PAGE_ID, internal_max_size_);
            new_root->SetValueAt(0, leaf_id);
            new_root->SetKeyAt(1, split_key);
            new_root->SetValueAt(1, new_leaf_id);
            new_root->SetSize(2);
            
            Page *left = buffer_pool_manager_->FetchPage(leaf_id);
            if (left) {
                reinterpret_cast<BPlusTreePage*>(left->GetData())->SetParentPageId(new_root_id);
                buffer_pool_manager_->UnpinPage(leaf_id, true);
            }
            Page *right = buffer_pool_manager_->FetchPage(new_leaf_id);
            if (right) {
                reinterpret_cast<BPlusTreePage*>(right->GetData())->SetParentPageId(new_root_id);
                buffer_pool_manager_->UnpinPage(new_leaf_id, true);
            }
            
            root_page_id_ = new_root_id;
            buffer_pool_manager_->UnpinPage(new_root_id, true);
            return true;
        }
        
        return InsertIntoParentRecursive(parent_id, split_key, leaf_id, new_leaf_id);
    }

    // --- HELPER: Parent Recursion ---
    template <typename KeyType, typename ValueType, typename KeyComparator>
    bool BPlusTree<KeyType, ValueType, KeyComparator>::InsertIntoParentRecursive(
        page_id_t parent_id, const KeyType &key, page_id_t left_child_id, page_id_t right_child_id) {
        
        (void) left_child_id;
        static thread_local int recursion_depth = 0;
        if (recursion_depth > 50) throw Exception(ExceptionType::OUT_OF_RANGE, "Recursion depth exceeded");
        
        recursion_depth++;
        try {
            Page *parent_page = buffer_pool_manager_->FetchPage(parent_id);
            if (!parent_page) { recursion_depth--; throw Exception(ExceptionType::OUT_OF_RANGE, "Cannot fetch parent"); }
            
            auto *parent = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(parent_page->GetData());
            
            if (parent->GetSize() < parent->GetMaxSize()) {
                InsertGeneric(parent, key, right_child_id, comparator_);
                Page *right = buffer_pool_manager_->FetchPage(right_child_id);
                if (right) {
                    reinterpret_cast<BPlusTreePage*>(right->GetData())->SetParentPageId(parent_id);
                    buffer_pool_manager_->UnpinPage(right_child_id, true);
                }
                buffer_pool_manager_->UnpinPage(parent_id, true);
                recursion_depth--;
                return true;
            }
            
            // Split Parent
            page_id_t grandparent_id = parent->GetParentPageId();
            page_id_t new_parent_id;
            Page *new_parent_page = buffer_pool_manager_->NewPage(&new_parent_id);
            if (!new_parent_page) {
                buffer_pool_manager_->UnpinPage(parent_id, false);
                recursion_depth--;
                throw Exception(ExceptionType::OUT_OF_RANGE, "Cannot create new parent");
            }
            
            auto *new_parent = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(new_parent_page->GetData());
            new_parent->Init(new_parent_id, grandparent_id, parent->GetMaxSize());
            
            std::vector<std::pair<KeyType, page_id_t>> entries;
            entries.push_back({KeyType{}, parent->ValueAt(0)});
            for (int i = 1; i < parent->GetSize(); i++) entries.push_back({parent->KeyAt(i), parent->ValueAt(i)});
            
            auto it = std::lower_bound(entries.begin() + 1, entries.end(), key,
                [&](const auto &p, const auto &k) { return comparator_(p.first, k) < 0; });
            entries.insert(it, {key, right_child_id});
            
            int mid = entries.size() / 2;
            parent->SetSize(mid);
            for (int i = 0; i < mid; i++) {
                if (i > 0) parent->SetKeyAt(i, entries[i].first);
                parent->SetValueAt(i, entries[i].second);
            }
            
            KeyType push_up_key = entries[mid].first;
            new_parent->SetSize(entries.size() - mid);
            new_parent->SetValueAt(0, entries[mid].second);
            for (size_t i = mid + 1; i < entries.size(); i++) {
                new_parent->SetKeyAt(i - mid, entries[i].first);
                new_parent->SetValueAt(i - mid, entries[i].second);
            }
            
            // Update children pointers
            for (int i = 0; i < new_parent->GetSize(); i++) {
                page_id_t child_id = new_parent->ValueAt(i);
                Page *child = buffer_pool_manager_->FetchPage(child_id);
                if (child) {
                    reinterpret_cast<BPlusTreePage*>(child->GetData())->SetParentPageId(new_parent_id);
                    buffer_pool_manager_->UnpinPage(child_id, true);
                }
            }
            
            buffer_pool_manager_->UnpinPage(parent_id, true);
            buffer_pool_manager_->UnpinPage(new_parent_id, true);
            
            if (grandparent_id == INVALID_PAGE_ID) {
                page_id_t new_root_id;
                Page *new_root_page = buffer_pool_manager_->NewPage(&new_root_id);
                if (!new_root_page) { recursion_depth--; throw Exception(ExceptionType::OUT_OF_RANGE, "Cannot create new root"); }
                
                auto *new_root = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(new_root_page->GetData());
                new_root->Init(new_root_id, INVALID_PAGE_ID, internal_max_size_);
                new_root->SetValueAt(0, parent_id);
                new_root->SetKeyAt(1, push_up_key);
                new_root->SetValueAt(1, new_parent_id);
                new_root->SetSize(2);
                
                Page *left = buffer_pool_manager_->FetchPage(parent_id);
                if (left) {
                    reinterpret_cast<BPlusTreePage*>(left->GetData())->SetParentPageId(new_root_id);
                    buffer_pool_manager_->UnpinPage(parent_id, true);
                }
                Page *right = buffer_pool_manager_->FetchPage(new_parent_id);
                if (right) {
                    reinterpret_cast<BPlusTreePage*>(right->GetData())->SetParentPageId(new_root_id);
                    buffer_pool_manager_->UnpinPage(new_parent_id, true);
                }
                
                root_page_id_ = new_root_id;
                buffer_pool_manager_->UnpinPage(new_root_id, true);
                recursion_depth--;
                return true;
            }
            
            bool result = InsertIntoParentRecursive(grandparent_id, push_up_key, parent_id, new_parent_id);
            recursion_depth--;
            return result;
        } catch (...) {
            recursion_depth--;
            throw;
        }
    }

    // --- STUBS (Required to satisfy linker/header declarations) ---
    template<typename KeyType, typename ValueType, typename KeyComparator>
    void BPlusTree<KeyType, ValueType, KeyComparator>::StartNewTree(const KeyType &key, const ValueType &value) {
        page_id_t new_page_id;
        Page *page = buffer_pool_manager_->NewPage(&new_page_id);
        if (!page) throw Exception(ExceptionType::OUT_OF_RANGE, "Cannot create root");
        auto *root = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *>(page->GetData());
        root->Init(new_page_id, INVALID_PAGE_ID, leaf_max_size_);
        root_page_id_ = new_page_id;
        root->SetKeyAt(0, key);
        root->SetValueAt(0, value);
        root->SetSize(1);
        buffer_pool_manager_->UnpinPage(new_page_id, true);
    }

    template<typename KeyType, typename ValueType, typename KeyComparator>
    Page *BPlusTree<KeyType, ValueType, KeyComparator>::FindLeafPage(const KeyType &key, bool leftMost, OpType op, Transaction *txn) {
        (void)key; (void)leftMost; (void)op; (void)txn; return nullptr; 
    }
    
    template<typename KeyType, typename ValueType, typename KeyComparator>
    bool BPlusTree<KeyType, ValueType, KeyComparator>::InsertIntoLeafOptimistic(
        const KeyType &key, const ValueType &value, page_id_t root_id, Transaction *txn) {
        (void)key; (void)value; (void)root_id; (void)txn; return false;
    }

    template<typename KeyType, typename ValueType, typename KeyComparator>
    bool BPlusTree<KeyType, ValueType, KeyComparator>::InsertIntoLeaf(const KeyType &key, const ValueType &value,
                                                                      Transaction *txn, bool optimistic) {
        (void)optimistic; return InsertIntoLeafPessimistic(key, value, txn);
    }

    template<typename KeyType, typename ValueType, typename KeyComparator>
    void BPlusTree<KeyType, ValueType, KeyComparator>::InsertIntoParent(BPlusTreePage *old_node, const KeyType &key,
                                                                        BPlusTreePage *new_node, Transaction *txn) {
        (void)old_node; (void)key; (void)new_node; (void)txn;
    }

    template<typename KeyType, typename ValueType, typename KeyComparator>
    template<typename N>
    N *BPlusTree<KeyType, ValueType, KeyComparator>::Split(N *node) { (void)node; return nullptr; } 

    template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;
} // namespace francodb