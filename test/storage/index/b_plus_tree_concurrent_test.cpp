#include <iostream>
#include <cstdio>
#include <cassert>
#include <filesystem>
#include <vector>
#include <thread>
#include <algorithm>
#include <random>

#include "storage/index/b_plus_tree.h"
#include "buffer/buffer_pool_manager.h"
#include "storage/index/index_key.h"
#include "common/rid.h"
#include "common/value.h"

using namespace francodb;

// --- HELPERS ---
static GenericKey<8> MakeKey(int n) {
    GenericKey<8> k;
    Value v(TypeId::INTEGER, n);
    k.SetFromValue(v);
    return k;
}

void InsertRange(BPlusTree<GenericKey<8>, RID, GenericComparator<8>> *tree, int start, int end, int thread_id) {
    for (int i = start; i < end; i++) {
        GenericKey<8> key = MakeKey(i);
        RID rid(i, thread_id); // PageID = Key, SlotID = ThreadID
        tree->Insert(key, rid);
    }
}

void ReadRange(BPlusTree<GenericKey<8>, RID, GenericComparator<8>> *tree, int start, int end) {
    std::vector<RID> results;
    for (int i = start; i < end; i++) {
        GenericKey<8> key = MakeKey(i);
        results.clear();
        tree->GetValue(key, &results);
        // We don't assert here because in the Mixed Test, data might not be inserted yet.
        // We just want to ensure we don't CRASH while reading.
    }
}

// --- TEST 1: CONCURRENT INSERTS ---
void TestConcurrentInsert(int num_threads, int keys_per_thread) {
    std::string filename = "test_concurrent_insert.francodb";
    if (std::filesystem::exists(filename)) std::filesystem::remove(filename);

    std::cout << "\n[TEST] Concurrent Insert (" << num_threads << " Threads, " << keys_per_thread << " Keys/Thread)..." << std::endl;

    auto *disk_manager = new DiskManager(filename);
    // Large Buffer Pool to reduce disk I/O noise, focusing on Lock Contention
    auto *bpm = new BufferPoolManager(100, disk_manager); 
    
    GenericComparator<8> comparator(TypeId::INTEGER);
    // Leaf Size = 2 forces splits constantly! High contention.
    BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("test_index", bpm, comparator, 2, 3);

    std::vector<std::thread> threads;

    // Launch Threads
    for (int i = 0; i < num_threads; i++) {
        int start_key = i * keys_per_thread;
        int end_key = start_key + keys_per_thread;
        threads.emplace_back(std::thread(InsertRange, &tree, start_key, end_key, i));
    }

    // Wait for all
    for (auto &t : threads) {
        t.join();
    }
    std::cout << "  -> All threads finished." << std::endl;

    // Verify Data
    int total_keys = num_threads * keys_per_thread;
    int found_count = 0;
    
    std::vector<RID> result;
    for (int i = 0; i < total_keys; i++) {
        result.clear();
        if (tree.GetValue(MakeKey(i), &result)) {
            found_count++;
        }
    }

    if (found_count == total_keys) {
        std::cout << "[PASS] All " << total_keys << " keys found successfully." << std::endl;
    } else {
        std::cout << "[FAIL] Expected " << total_keys << " keys, found " << found_count << std::endl;
        // assert(false);
    }

    delete bpm;
    delete disk_manager;
    // std::filesystem::remove(filename);
}

// --- TEST 2: MIXED READ/WRITE ---
void TestMixedReadWrite() {
    std::string filename = "test_mixed_rw.francodb";
    if (std::filesystem::exists(filename)) std::filesystem::remove(filename);

    std::cout << "\n[TEST] Mixed Read/Write Stampede..." << std::endl;

    auto *disk_manager = new DiskManager(filename);
    auto *bpm = new BufferPoolManager(50, disk_manager);
    GenericComparator<8> comparator(TypeId::INTEGER);
    BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("test_index", bpm, comparator, 3, 3);

    // 1. Writer Threads
    std::thread w1(InsertRange, &tree, 0, 1000, 1);
    std::thread w2(InsertRange, &tree, 1000, 2000, 2);

    // 2. Reader Threads (Try to read ranges overlapping with writers)
    // They shouldn't crash, but they might miss keys not yet inserted.
    std::thread r1(ReadRange, &tree, 0, 1000); 
    std::thread r2(ReadRange, &tree, 1000, 2000); 

    w1.join();
    w2.join();
    r1.join();
    r2.join();

    std::cout << "  -> Read/Write threads finished without crashing." << std::endl;

    // Final Verification
    std::vector<RID> result;
    bool all_found = true;
    for (int i = 0; i < 2000; i++) {
        result.clear();
        if (!tree.GetValue(MakeKey(i), &result)) {
            all_found = false;
            break;
        }
    }

    if (all_found) {
        std::cout << "[PASS] Mixed Workload Data Integrity Verified." << std::endl;
    } else {
        std::cout << "[FAIL] Data lost during mixed workload." << std::endl;
    }

    delete bpm;
    delete disk_manager;
}

void TestBPlusTreeConcurrent() {
    // 5 Threads, 500 keys each = 2500 insertions
    TestConcurrentInsert(5, 500);
    
    TestMixedReadWrite();
    
}
