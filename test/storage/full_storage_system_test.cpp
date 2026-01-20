#include <iostream>
#include <vector>
#include <cassert>
#include <filesystem>

#include "catalog/catalog.h"
#include "storage/table/tuple.h"
#include "common/value.h"

using namespace francodb;

void TestFullSystem() {
    std::string db_name = "francodb_system.francodb";
    // Clean up any previous test files
    if (std::filesystem::exists(db_name)) {
        std::filesystem::remove(db_name);
    }
    if (std::filesystem::exists(db_name + ".meta")) {
        std::filesystem::remove(db_name + ".meta");
    }

    std::cout << "[TEST] Starting Full System Integration Test..." << std::endl;

    // 1. Initialize Infrastructure
    auto disk_manager = std::make_unique<DiskManager>(db_name);
    auto bpm = std::make_unique<BufferPoolManager>(50, disk_manager.get());
    auto catalog = std::make_unique<Catalog>(bpm.get());

    // 2. CREATE TABLE users (id RAKAM, name GOMLA, points RAKAM)
    std::vector<Column> cols;
    cols.emplace_back("id", TypeId::INTEGER);
    cols.emplace_back("name", TypeId::VARCHAR, (uint32_t)0);
    cols.emplace_back("points", TypeId::INTEGER);
    Schema schema(cols);

    catalog->CreateTable("users", schema);
    std::cout << "[STEP 1] Table 'users' registered in Catalog." << std::endl;

    // 3. Get Metadata back from Catalog
    TableMetadata *meta = catalog->GetTable("users");
    assert(meta != nullptr);
    assert(meta->name_ == "users");

    // 4. INSERT INTO users VALUES (1, "Ahmed", 9000)
    std::vector<Value> v1;
    v1.emplace_back(TypeId::INTEGER, 1);
    v1.emplace_back(TypeId::VARCHAR, "Ahmed");
    v1.emplace_back(TypeId::INTEGER, 9000);
    
    Tuple tuple1(v1, meta->schema_);
    RID rid1;
    meta->table_heap_->InsertTuple(tuple1, &rid1, nullptr);

    // INSERT INTO users VALUES (2, "FrancoUser", 500)
    std::vector<Value> v2;
    v2.emplace_back(TypeId::INTEGER, 2);
    v2.emplace_back(TypeId::VARCHAR, "FrancoUser");
    v2.emplace_back(TypeId::INTEGER, 500);
    
    Tuple tuple2(v2, meta->schema_);
    RID rid2;
    meta->table_heap_->InsertTuple(tuple2, &rid2, nullptr);

    std::cout << "[STEP 2] Two tuples inserted into TableHeap via Catalog metadata." << std::endl;

    // 5. SELECT * FROM users (Verification)
    Tuple fetched_t1, fetched_t2;
    meta->table_heap_->GetTuple(rid1, &fetched_t1, nullptr);
    meta->table_heap_->GetTuple(rid2, &fetched_t2, nullptr);

    // Verify Ahmed
    assert(fetched_t1.GetValue(meta->schema_, 0).GetAsInteger() == 1);
    assert(fetched_t1.GetValue(meta->schema_, 1).GetAsString() == "Ahmed");
    assert(fetched_t1.GetValue(meta->schema_, 2).GetAsInteger() == 9000);

    // Verify FrancoUser
    assert(fetched_t2.GetValue(meta->schema_, 0).GetAsInteger() == 2);
    assert(fetched_t2.GetValue(meta->schema_, 1).GetAsString() == "FrancoUser");
    
    std::cout << "  -> Read back Ahmed (Points: " << fetched_t1.GetValue(meta->schema_, 2).GetAsInteger() << ")" << std::endl;
    std::cout << "  -> Read back " << fetched_t2.GetValue(meta->schema_, 1).GetAsString() << " successfully." << std::endl;
    
    // --- 6. UPDATE points SET 9999 WHERE id = 1 ---
    std::cout << "[STEP 3] Testing Update: Changing Ahmed's points to 9999..." << std::endl;
    
    std::vector<Value> v1_updated;
    v1_updated.emplace_back(TypeId::INTEGER, 1);
    v1_updated.emplace_back(TypeId::VARCHAR, "UPDATED NAME");
    v1_updated.emplace_back(TypeId::INTEGER, 9999);
    
    Tuple updated_tuple(v1_updated, meta->schema_);
    
    // Perform the update
    bool update_ok = meta->table_heap_->UpdateTuple(updated_tuple, rid1, nullptr);
    assert(update_ok);

    // Verify update (Note: UpdateTuple usually generates a NEW RID in most heaps)
    // Since we don't have an index yet, we'd normally have to scan. 
    // For now, let's just check if the operation returned success.
    std::cout << "  -> Update successful." << std::endl;

    // --- 7. DROP TABLE users ---
    std::cout << "[STEP 4] Testing Drop Table..." << std::endl;
    bool drop_ok = catalog->DropTable("users");
    assert(drop_ok);

    // Verify it's gone from Catalog
    assert(catalog->GetTable("users") == nullptr);
    std::cout << "  -> Table 'users' dropped. Catalog lookup returned nullptr." << std::endl;
    
    
    
    std::cout << "[SUCCESS] The System can now manage tables and structured data!" << std::endl;
    
    
    
    
}

