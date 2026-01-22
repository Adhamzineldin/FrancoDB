#include <iostream>
#include <filesystem>
#include <cassert>
#include "storage/disk/disk_manager.h"
#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"

using namespace francodb;

void TestDiskRecycling() {
    std::string db_name = "recycle_test.francodb";
    // Clean up any previous test files
    if (std::filesystem::exists(db_name)) {
        std::filesystem::remove(db_name);
    }
    if (std::filesystem::exists(db_name + ".meta")) {
        std::filesystem::remove(db_name + ".meta");
    }

    std::cout << "[TEST] Starting Disk Recycle Test..." << std::endl;

    auto disk_manager = std::make_unique<DiskManager>(db_name);
    auto bpm = std::make_unique<BufferPoolManager>(50, disk_manager.get());
    auto catalog = std::make_unique<Catalog>(bpm.get());

    // 1. Create Table A and fill it with data to create at least 3 pages
    std::vector<Column> cols = {Column("data", TypeId::VARCHAR, (uint32_t)100, false)};
    Schema schema(cols);
    catalog->CreateTable("TableA", schema);
    TableMetadata *metaA = catalog->GetTable("TableA");

    // Insert ~150 long tuples (approx 4-5 pages)
    std::string long_str(200, 'A'); 
    for (int i = 0; i < 150; i++) {
        std::vector<Value> v = {Value(TypeId::VARCHAR, long_str)};
        RID rid;
        metaA->table_heap_->InsertTuple(Tuple(v, schema), &rid, nullptr);
    }

    int size_after_insert = disk_manager->GetNumPages();
    std::cout << "[STEP 1] TableA created. File size: " << size_after_insert << " pages." << std::endl;

    // 2. DROP TableA
    catalog->DropTable("TableA");
    std::cout << "[STEP 2] TableA dropped. Pages marked as free in Bitmap." << std::endl;

    // 3. Create TableB and insert data
    catalog->CreateTable("TableB", schema);
    TableMetadata *metaB = catalog->GetTable("TableB");
    
    for (int i = 0; i < 100; i++) {
        std::vector<Value> v = {Value(TypeId::VARCHAR, long_str)};
        RID rid;
        metaB->table_heap_->InsertTuple(Tuple(v, schema), &rid, nullptr);
    }

    int size_final = disk_manager->GetNumPages();
    std::cout << "[STEP 3] TableB created and filled. File size: " << size_final << " pages." << std::endl;

    // S-CLASS ASSERTION:
    // If recycling works, size_final should be EQUAL to size_after_insert.
    // If it failed, size_final would be approx size_after_insert + 3.
    assert(size_final <= size_after_insert);

    std::cout << "[SUCCESS] Disk space was successfully recycled!" << std::endl;
}

