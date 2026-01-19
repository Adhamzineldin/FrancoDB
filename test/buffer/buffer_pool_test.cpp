#include "../framework/test_runner.h"
#include "buffer/buffer_pool_manager.h"
#include <cstdio>
#include <string>
#include <iostream>
#include <cassert>
#include <cstring>
#include <filesystem>

using namespace francodb;

void TestBufferPoolBinary() {
    const std::string filename = "test_buffer_pool.francodb";
    
    // 1. Cleanup from previous runs
    if (std::filesystem::exists(filename)) {
        std::filesystem::remove(filename);
    }

    std::cout << "[TEST] Starting Buffer Pool Test..." << std::endl;

    DiskManager *disk_manager = new DiskManager(filename);
    
    // 2. Create a small pool (Size = 5) to force evictions
    BufferPoolManager *bpm = new BufferPoolManager(5, disk_manager);

    page_id_t page_id_temp;
    auto *page0 = bpm->NewPage(&page_id_temp);
    
    // IMPORTANT: Capture the actual ID assigned to the first page.
    // (If BPM starts at 1, this will be 1. If 0, it will be 0).
    page_id_t first_page_id = page0->GetPageId();
    std::cout << "[INFO] First Page created with ID: " << first_page_id << std::endl;

    // Scenario 1: Fill the Pool
    std::cout << "[STEP 1] Filling the pool..." << std::endl;
    
    auto *page1 = bpm->NewPage(&page_id_temp);
    auto *page2 = bpm->NewPage(&page_id_temp);
    auto *page3 = bpm->NewPage(&page_id_temp);
    auto *page4 = bpm->NewPage(&page_id_temp);

    // Assert all pages were created successfully
    assert(page0 != nullptr);
    assert(page1 != nullptr);
    assert(page2 != nullptr);
    assert(page3 != nullptr);
    assert(page4 != nullptr);

    // Write data to the first page.
    // CRITICAL FIX: We add an offset to avoid overwriting the Page Header (PageID).
    // If PageHeader struct isn't visible here, we use sizeof(page_id_t) which is 4 bytes.
    // But sizeof(PageHeader) is safer if available.
    int offset = sizeof(page_id_t); // Safest minimum offset
    char *data_ptr = page0->GetData() + offset;
    std::strcpy(data_ptr, "Hello Page 0");
    
    // Scenario 2: The "Full Pool" Check
    std::cout << "[STEP 2] Testing pinned limit..." << std::endl;
    auto *fail_page = bpm->NewPage(&page_id_temp);
    assert(fail_page == nullptr); 
    std::cout << "  -> Correctly failed to allocate (Pool is full of pinned pages)." << std::endl;

    // Scenario 3: Unpin and Evict
    std::cout << "[STEP 3] Unpinning Page " << first_page_id << " (Dirty)..." << std::endl;
    
    // FIX: Unpin using the captured ID
    bpm->UnpinPage(first_page_id, true);

    // Now we ask for a new page. It should evict the one we just unpinned.
    auto *page5 = bpm->NewPage(&page_id_temp);
    assert(page5 != nullptr);
    std::cout << "  -> Success! Old page was evicted to make room for Page " << page5->GetPageId() << std::endl;

    // Scenario 4: Fetch Back (Persistence Check)
    // First, unpin Page 1 (or whatever the second page was) to make room
    bpm->UnpinPage(page1->GetPageId(), false);
    
    std::cout << "[STEP 4] Fetching Page " << first_page_id << " back from disk..." << std::endl;
    
    // FIX: Fetch using the captured ID
    page0 = bpm->FetchPage(first_page_id);
    assert(page0 != nullptr);
    
    // Verify data
    char *read_ptr = page0->GetData() + offset;
    assert(strcmp(read_ptr, "Hello Page 0") == 0);
    std::cout << "  -> Data matched! Persistence is working." << std::endl;

    // Cleanup
    bpm->UnpinPage(first_page_id, false);
    bpm->UnpinPage(page2->GetPageId(), false);
    bpm->UnpinPage(page3->GetPageId(), false);
    bpm->UnpinPage(page4->GetPageId(), false);
    bpm->UnpinPage(page5->GetPageId(), false);

    delete bpm;
    delete disk_manager;
    
    // Clean up the file
    // std::filesystem::remove(filename); 
    
    std::cout << "[SUCCESS] All Buffer Pool tests passed!" << std::endl;
}

namespace francodb_test {
void RunBufferPoolTests(TestRunner& runner) {
    runner.RunTest("Buffer", "Buffer Pool Binary Test", []() {
        TestBufferPoolBinary();
    });
}
} // namespace francodb_test
