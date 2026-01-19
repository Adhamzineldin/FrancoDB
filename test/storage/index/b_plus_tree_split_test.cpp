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

GenericKey<8> MakeKeySplit(int n) {
    GenericKey<8> k;
    Value v(TypeId::INTEGER, n);
    k.SetFromValue(v);
    return k;
}

void TestSplitTree() {
    std::string filename = "test_tree_split.francodb";
    if (std::filesystem::exists(filename)) {
        std::filesystem::remove(filename);
    }

    std::cout << "[TEST] Starting Split B+ Tree Test (GenericKey)..." << std::endl;

    auto *disk_manager = new DiskManager(filename);
    auto *bpm = new BufferPoolManager(20, disk_manager); 
    
    GenericComparator<8> comparator(TypeId::INTEGER);
    // Max Size = 5 forces splits very quickly
    BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("test_index", bpm, comparator, 5, 5);

    // 1. Insert 15 keys
    int n = 15;
    for (int i = 1; i <= n; i++) {
        // Map Key i -> RID(i, i*100)
        tree.Insert(MakeKeySplit(i), RID(i, i * 100));
    }

    std::cout << "[STEP 1] Inserted " << n << " keys." << std::endl;

    // 2. Read them all back
    std::vector<RID> result;
    for (int i = 1; i <= n; i++) {
        result.clear();
        bool found = tree.GetValue(MakeKeySplit(i), &result);
        
        if (!found || result[0].GetSlotId() != (uint32_t)(i * 100)) {
            std::cout << "[FAIL] Lost Key " << i << std::endl;
            assert(false);
        }
    }
    std::cout << "[STEP 2] All keys found! Splitting logic works." << std::endl;

    delete bpm;
    delete disk_manager;
    std::filesystem::remove(filename);

    std::cout << "[SUCCESS] B+ Tree Split Test Passed!" << std::endl;
}

void TestBPlusTreeSplit() {
    TestSplitTree();
    
}
