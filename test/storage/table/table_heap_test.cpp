#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cassert>
#include <filesystem>
#include <algorithm>

#include "storage/table/table_heap.h"
#include "buffer/buffer_pool_manager.h"
#include "common/exception.h"

using namespace francodb;

// Helper to create a tuple from a string
Tuple CreateTuple(const std::string &val) {
    std::vector<char> data(val.begin(), val.end());
    return Tuple(data);
}

// Helper to assert tuple content
void CheckTuple(Tuple &tuple, const std::string &expected_val) {
    std::string content(tuple.GetData(), tuple.GetLength());
    if (content != expected_val) {
        std::cout << "[FAIL] Expected '" << expected_val << "' but got '" << content << "'" << std::endl;
        assert(false);
    }
}

void TestTableHeap() {
    std::string filename = "test_table_heap.francodb";
    if (std::filesystem::exists(filename)) {
        std::filesystem::remove(filename);
    }

    std::cout << "[TEST] Starting Table Heap Test..." << std::endl;

    // 1. Setup
    auto *disk_manager = new DiskManager(filename);
    auto *bpm = new BufferPoolManager(50, disk_manager); // 50 pages RAM
    auto *table = new TableHeap(bpm, nullptr);

    std::cout << "[STEP 1] Table Heap Created. First Page: " << table->GetFirstPageId() << std::endl;

    // 2. Insert Data (Enough to fill multiple pages)
    // A page is 4096 bytes. Header is ~16 bytes.
    // If each tuple is ~100 bytes, we fit ~40 tuples per page.
    // Let's insert 100 tuples to force 2-3 pages.
    std::vector<RID> rids;
    int count = 100;
    
    for (int i = 0; i < count; i++) {
        std::string val = "Tuple_Data_" + std::to_string(i);
        // Make it slightly variable length
        if (i % 2 == 0) val += "_EXTRA_LONG_STRING_FOR_PADDING";
        
        Tuple tuple = CreateTuple(val);
        RID rid;
        bool res = table->InsertTuple(tuple, &rid, nullptr);
        
        assert(res == true);
        rids.push_back(rid);
    }
    
    std::cout << "[STEP 2] Inserted " << count << " tuples successfully." << std::endl;

    // 3. Read Verification
    for (int i = 0; i < count; i++) {
        Tuple t;
        bool res = table->GetTuple(rids[i], &t, nullptr);
        assert(res == true);
        
        std::string expected = "Tuple_Data_" + std::to_string(i);
        if (i % 2 == 0) expected += "_EXTRA_LONG_STRING_FOR_PADDING";
        
        CheckTuple(t, expected);
    }
    std::cout << "[STEP 3] Verified all tuples read back correctly." << std::endl;

    // 4. Delete Logic
    // Delete the 10th tuple
    std::cout << "[STEP 4] Testing Deletion..." << std::endl;
    bool del_res = table->MarkDelete(rids[10], nullptr);
    assert(del_res == true);

    // Try to read it back (Should fail)
    Tuple t_deleted;
    bool read_res = table->GetTuple(rids[10], &t_deleted, nullptr);
    assert(read_res == false); // Should return false because it's deleted
    std::cout << "  -> Deleted tuple 10 successfully." << std::endl;

    // 5. Update Logic (Delete Old + Insert New)
    std::cout << "[STEP 5] Testing Update..." << std::endl;
    std::string new_val = "UPDATED_TUPLE_VALUE_999";
    Tuple new_tuple = CreateTuple(new_val);
    
    // Update tuple 20
    bool update_res = table->UpdateTuple(new_tuple, rids[20], nullptr);
    assert(update_res == true);

    // Verify Old is gone
    bool read_old = table->GetTuple(rids[20], &t_deleted, nullptr);
    assert(read_old == false);

    // Note: In a real DB, UpdateTuple usually returns the NEW RID so you can update the index.
    // Our void/bool implementation assumes the user doesn't track the new location easily yet.
    // Ideally UpdateTuple should return the new RID.
    // For this test, we assume success means it's in there somewhere.
    
    std::cout << "  -> Updated tuple 20 successfully." << std::endl;

    // Cleanup
    delete table;
    delete bpm;
    delete disk_manager;
    // std::filesystem::remove(filename);

    std::cout << "[SUCCESS] Table Heap Test Passed!" << std::endl;
}

int main() {
    TestTableHeap();
    return 0;
}