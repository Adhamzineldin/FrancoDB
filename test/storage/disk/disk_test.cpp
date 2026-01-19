#include "storage/disk/disk_manager.h"
#include "storage/page/page.h"
#include <cstring>
#include <iostream>
#include <filesystem>
#include <cassert>

using namespace francodb;

void TestPersistence() {
    std::string filename = "test_persistence.francodb";
    
    // 1. Clean up previous runs
    if (std::filesystem::exists(filename)) {
        std::filesystem::remove(filename);
    }

    std::cout << "[TEST] Starting Persistence Test..." << std::endl;

    {
        // 2. Open the Database (Constructor checks for Magic Bytes)
        std::cout << "[STEP 1] creating disk manager..." << std::endl;
        DiskManager dm(filename);

        // 3. Prepare data
        char data[PAGE_SIZE];
        std::memset(data, 0, PAGE_SIZE);
        std::string message = "FrancoDB is persistent!";
        std::memcpy(data, message.c_str(), message.length());

        // 4. Write to Page 1 (Page 0 is reserved for Magic/Metadata)
        std::cout << "[STEP 2] Writing Page 1..." << std::endl;
        dm.WritePage(1, data);
        
        // 5. Destructor runs here, closing the file
    }

    // 6. RE-OPEN the database to prove data survived
    {
        std::cout << "[STEP 3] Re-opening disk manager..." << std::endl;
        DiskManager dm(filename);
        
        char read_buffer[PAGE_SIZE];
        std::memset(read_buffer, 0, PAGE_SIZE);
        
        // 7. Read Page 1
        dm.ReadPage(1, read_buffer);
        
        // 8. Verify
        std::string result(read_buffer);
        std::cout << "[RESULT] Read back: " << result << std::endl;
        
        assert(result == "FrancoDB is persistent!");
        std::cout << "[SUCCESS] Data matched!" << std::endl;
    }

    // Clean up
    // std::filesystem::remove(filename);
}

void TestDiskPersistence() {
    try {
        TestPersistence();
    } catch (const std::exception &e) {
        std::cerr << "[FAIL] Exception: " << e.what() << std::endl;
        
    }
    
}
