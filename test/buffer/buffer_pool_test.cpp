#include "buffer/buffer_pool_manager.h"
#include <cstdio>
#include <string>
#include <iostream>
#include <cassert>
#include <cstring>
#include <filesystem>

using namespace francodb;

void TestBufferPoolBinary() {
    const std::string filename = "test_buffer_pool.fdb";
    
    // 1. Cleanup from previous runs
    if (std::filesystem::exists(filename)) {
        std::filesystem::remove(filename);
    }

    std::cout << "[TEST] Starting Buffer Pool Test..." << std::endl;

    DiskManager *disk_manager = new DiskManager(filename);
    
    // 2. Create a small pool (Size = 5) to force evictions quickly
    // This allows us to test the "Clock" or "LRU" logic without needing gigabytes of RAM.
    BufferPoolManager *bpm = new BufferPoolManager(5, disk_manager);

    page_id_t page_id_temp;
    auto *page0 = bpm->NewPage(&page_id_temp);
    
    // Scenario 1: Fill the Pool
    // We already have Page 0. Let's create 4 more (Total 5).
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

    // Write some data to Page 0 so we can verify persistence later
    char *data_ptr = page0->GetData() + sizeof(PageHeader);
    std::strcpy(data_ptr, "Hello Page 0");
    
    // Scenario 2: The "Full Pool" Check
    // All 5 pages are currently pinned (Pin Count = 1).
    // If we ask for a 6th page, it SHOULD FAIL because there are no victims.
    std::cout << "[STEP 2] Testing pinned limit..." << std::endl;
    auto *fail_page = bpm->NewPage(&page_id_temp);
    assert(fail_page == nullptr); 
    std::cout << "  -> Correctly failed to allocate (Pool is full of pinned pages)." << std::endl;

    // Scenario 3: Unpin and Evict
    // We unpin Page 0. We mark it "Dirty" (true) because we wrote data to it.
    // Page 0 is now the only candidate for eviction.
    std::cout << "[STEP 3] Unpinning Page 0 (Dirty)..." << std::endl;
    bpm->UnpinPage(page0->GetPageId(), true);

    // Now we ask for a new page. BPM should:
    // 1. See Page 0 is unpinned.
    // 2. Write Page 0 to disk (Flush).
    // 3. Re-use that frame for Page 5.
    auto *page5 = bpm->NewPage(&page_id_temp);
    assert(page5 != nullptr);
    std::cout << "  -> Success! Page 0 was evicted to make room for Page 5." << std::endl;

    // Scenario 4: Fetch Back (Persistence Check)
    // We want to read Page 0 again. 
    // Since it was evicted, this forces a fetch from Disk.
    // If the data is "Hello Page 0", it means the Flush worked!
    
    // First, unpin Page 1 to make room (Not dirty)
    bpm->UnpinPage(page1->GetPageId(), false);
    
    std::cout << "[STEP 4] Fetching Page 0 back from disk..." << std::endl;
    page0 = bpm->FetchPage(0);
    assert(page0 != nullptr);
    
    char *read_ptr = page0->GetData() + sizeof(PageHeader);
    assert(strcmp(read_ptr, "Hello Page 0") == 0);

    // Cleanup
    // Unpin everyone so the destructor is happy (optional but good practice)
    bpm->UnpinPage(page0->GetPageId(), false);
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

int main() {
    try {
        TestBufferPoolBinary();
    } catch (const std::exception &e) {
        std::cerr << "[FAIL] Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}