#include "../framework/test_runner.h"
#include "storage/table/column.h"
#include "common/value.h"

using namespace francodb_test;
using namespace francodb;

namespace francodb_test {

void RunColumnTests(TestRunner& runner) {
    // Test 1: Create NOT NULL Column
    runner.RunTest("Column", "NOT_NULL_Column", []() {
        Column col("email", TypeId::VARCHAR, static_cast<uint32_t>(255), false, false, false);
        ASSERT_FALSE(col.IsNullable());
    });
    
    // Test 2: Create NULLABLE Column
    runner.RunTest("Column", "NULLABLE_Column", []() {
        Column col("phone", TypeId::VARCHAR, static_cast<uint32_t>(20), false, true, false);
        ASSERT_TRUE(col.IsNullable());
    });
    
    // Test 3: PRIMARY KEY Column
    runner.RunTest("Column", "PRIMARY_KEY_Column", []() {
        Column col("id", TypeId::INTEGER, true, false, false);
        ASSERT_TRUE(col.IsPrimaryKey());
    });
    
    // Test 4: UNIQUE Constraint
    runner.RunTest("Column", "UNIQUE_Column", []() {
        Column col("username", TypeId::VARCHAR, static_cast<uint32_t>(64), false, false, true);
        ASSERT_TRUE(col.IsUnique());
    });
    
    // Test 5: Integer Column Type
    runner.RunTest("Column", "Integer_Column_Type", []() {
        Column col("age", TypeId::INTEGER, false, false, false);
        ASSERT_EQ(col.GetType(), TypeId::INTEGER, "Type should be INTEGER");
    });
    
    // Test 6: VARCHAR Column with Length
    runner.RunTest("Column", "VARCHAR_Column_Length", []() {
        Column col("name", TypeId::VARCHAR, static_cast<uint32_t>(100), false, false, false);
        ASSERT_EQ(col.GetLength(), 100, "Length should be 100");
    });
    
    // Test 7: Column Name
    runner.RunTest("Column", "Column_Name", []() {
        Column col("test_column", TypeId::INTEGER, false, false, false);
        ASSERT_EQ(col.GetName(), "test_column", "Name should match");
    });
    
    // Test 8: Multiple Constraints
    runner.RunTest("Column", "Multiple_Constraints", []() {
        Column col("id", TypeId::INTEGER, true, false, true);
        ASSERT_TRUE(col.IsPrimaryKey());
        ASSERT_TRUE(col.IsUnique());
        ASSERT_FALSE(col.IsNullable());
    });
    
    // Test 9: Boolean Column
    runner.RunTest("Column", "Boolean_Column", []() {
        Column col("is_active", TypeId::BOOLEAN, false, false, false);
        ASSERT_EQ(col.GetType(), TypeId::BOOLEAN, "Type should be BOOLEAN");
    });
    
    // Test 10: Copy Column
    runner.RunTest("Column", "Copy_Column", []() {
        Column col1("original", TypeId::INTEGER, true, false, false);
        Column col2 = col1;
        ASSERT_EQ(col2.GetName(), "original", "Copy should have same name");
        ASSERT_TRUE(col2.IsPrimaryKey());
    });
}

} // namespace francodb_test
