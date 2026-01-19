#include <iostream>
#include <cstdio>
#include <cassert>
#include <filesystem>
#include <vector>
#include <algorithm>

#include "storage/index/b_plus_tree.h"
#include "buffer/buffer_pool_manager.h"
#include "storage/index/index_key.h"
#include "common/rid.h"
#include "common/value.h"

using namespace francodb;

// Helper to create a GenericKey from an integer
static GenericKey<8> MakeKey(int n) {
    GenericKey<8> k;
    Value v(TypeId::INTEGER, n);
    k.SetFromValue(v);
    return k;
}

void TestSinglePageTree() {
    std::string filename = "test_tree_single.francodb";
    if (std::filesystem::exists(filename)) {
        std::filesystem::remove(filename);
    }

    std::cout << "[TEST] Starting Single Page B+ Tree Test (GenericKey)..." << std::endl;

    auto *disk_manager = new DiskManager(filename);
    auto *bpm = new BufferPoolManager(5, disk_manager); 
    
    // Use the OFFICIAL Database Types
    // Key: GenericKey<8>, Value: RID, Comparator: GenericComparator<8>
    GenericComparator<8> comparator(TypeId::INTEGER);
    BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("test_index", bpm, comparator, 10, 10);

    // 1. Verify Empty
    assert(tree.IsEmpty());
    std::cout << "[STEP 1] Tree is initially empty. (Passed)" << std::endl;

    // 2. Insert Data
    // We map Key(1) -> RID(1, 1) effectively simulating Row 1 on Page 1
    tree.Insert(MakeKey(1), RID(1, 1)); 
    tree.Insert(MakeKey(2), RID(2, 2));
    tree.Insert(MakeKey(3), RID(3, 3));
    tree.Insert(MakeKey(4), RID(4, 4));
    tree.Insert(MakeKey(5), RID(5, 5));

    assert(!tree.IsEmpty());
    std::cout << "[STEP 2] Inserted 5 keys successfully." << std::endl;

    // 3. Read Data
    std::vector<RID> result;
    
    // Search for Key 1
    bool found = tree.GetValue(MakeKey(1), &result);
    assert(found == true);
    assert(result[0].GetPageId() == 1); // Should match RID(1,1)
    std::cout << "  -> Found Key 1: RID Page " << result[0].GetPageId() << " (Correct)" << std::endl;

    // Search for Key 3
    result.clear();
    found = tree.GetValue(MakeKey(3), &result);
    assert(found == true);
    assert(result[0].GetPageId() == 3);
    std::cout << "  -> Found Key 3: RID Page " << result[0].GetPageId() << " (Correct)" << std::endl;

    // 4. Negative Test (Search for missing key 99)
    result.clear();
    found = tree.GetValue(MakeKey(99), &result);
    assert(found == false);
    std::cout << "[STEP 3] Search for missing Key 99 returned false. (Passed)" << std::endl;

    delete bpm;
    delete disk_manager;
    std::filesystem::remove(filename); 

    std::cout << "[SUCCESS] Single Page Tree works!" << std::endl;
}

void TestBPlusTree() {
    TestSinglePageTree();
    
}
