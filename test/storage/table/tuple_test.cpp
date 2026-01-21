#include <iostream>
#include <vector>
#include <cassert>
#include <string>

#include "storage/table/tuple.h"
#include "storage/table/schema.h"
#include "common/value.h"

using namespace francodb;

void TestTuplePacking() {
    std::cout << "[TEST] Starting S-Grade Tuple Packing Test..." << std::endl;

    // 1. Define the Schema: (ID: INT, Name: GOMLA/VARCHAR, IsActive: BOOL)
    std::vector<Column> cols;
    cols.emplace_back("id", TypeId::INTEGER);
    cols.emplace_back("name", TypeId::VARCHAR, (uint32_t)0); // Length 0 because it's dynamic
    cols.emplace_back("is_active", TypeId::BOOLEAN);

    Schema schema(cols);
    std::cout << "[STEP 1] Schema created. Fixed length: " << schema.GetLength() << " bytes." << std::endl;
    // Expected Fixed Length: 4 (Int) + 8 (Varchar Offset/Len) + 1 (Bool) = 13 bytes.

    // 2. Create Values
    std::vector<Value> values;
    values.emplace_back(TypeId::INTEGER, 42);
    values.emplace_back(TypeId::VARCHAR, "Franco_Database_Project_S_Grade");
    values.emplace_back(TypeId::BOOLEAN, 1); // True

    // 3. Pack into Tuple
    Tuple tuple(values, schema);
    std::cout << "[STEP 2] Tuple packed. Total size on disk: " << tuple.GetLength() << " bytes." << std::endl;

    // 4. Read Values Back
    Value v1 = tuple.GetValue(schema, 0); // id
    Value v2 = tuple.GetValue(schema, 1); // name
    Value v3 = tuple.GetValue(schema, 2); // is_active

    // 5. Assertions
    assert(v1.GetAsInteger() == 42);
    std::cout << v2.GetAsString() << std::endl;
    assert(v2.GetAsString() == "Franco_Database_Project_S_Grade");
    assert(v3.GetAsString() == "true");

    std::cout << "  -> ID: " << v1.GetAsInteger() << " (Correct)" << std::endl;
    std::cout << "  -> Name: " << v2.GetAsString() << " (Correct)" << std::endl;
    std::cout << "  -> IsActive: " << v3.GetAsString() << " (Correct)" << std::endl;

    // 6. Test with a different size string to ensure dynamic works
    std::vector<Value> values2;
    values2.emplace_back(TypeId::INTEGER, 99);
    values2.emplace_back(TypeId::VARCHAR, "Short");
    values2.emplace_back(TypeId::BOOLEAN, 0);

    Tuple tuple2(values2, schema);
    assert(tuple2.GetValue(schema, 1).GetAsString() == "Short");
    std::cout << "[STEP 3] Dynamic resizing verified with different string lengths." << std::endl;

    std::cout << "[SUCCESS] Tuple Packing logic is solid!" << std::endl;
}

