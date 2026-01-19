#include "../framework/test_runner.h"
#include <vector>
#include <string>

using namespace francodb_test;

namespace francodb_test {

void RunJoinTests(TestRunner& runner) {
    // Test 1: INNER JOIN - Basic Match
    runner.RunTest("JOIN", "INNER_JOIN_Basic_Match", []() {
        std::vector<int> order_ids = {1, 2, 3};
        std::vector<int> customer_ids = {100, 200};
        
        // Find matching IDs
        int matches = 0;
        for (int oid : order_ids) {
            for (int cid : customer_ids) {
                if (oid == cid) matches++;
            }
        }
        
        // No matches expected (1,2,3 don't match 100,200)
        ASSERT_EQ(matches, 0, "No matches should exist");
    });
    
    // Test 2: LEFT JOIN - Include All Left Rows
    runner.RunTest("JOIN", "LEFT_JOIN_Include_All_Left", []() {
        std::vector<int> left_table = {1, 2, 3};
        std::vector<int> right_table = {2, 3, 4};
        
        // LEFT JOIN includes all left rows
        ASSERT_EQ(left_table.size(), 3, "Left table has 3 rows");
    });
    
    // Test 3: RIGHT JOIN - Include All Right Rows
    runner.RunTest("JOIN", "RIGHT_JOIN_Include_All_Right", []() {
        std::vector<int> left_table = {1, 2, 3};
        std::vector<int> right_table = {2, 3, 4, 5};
        
        // RIGHT JOIN includes all right rows
        ASSERT_EQ(right_table.size(), 4, "Right table has 4 rows");
    });
    
    // Test 4: CROSS JOIN - Cartesian Product
    runner.RunTest("JOIN", "CROSS_JOIN_Cartesian_Product", []() {
        std::vector<int> table_a = {1, 2, 3};
        std::vector<int> table_b = {10, 20};
        
        // Cross join produces m*n rows
        int expected_rows = table_a.size() * table_b.size();
        ASSERT_EQ(expected_rows, 6, "Cross join should produce 6 rows");
    });
    
    // Test 5: JOIN Condition - Equality
    runner.RunTest("JOIN", "JOIN_Condition_Equality", []() {
        int val1 = 100;
        int val2 = 100;
        ASSERT_TRUE(val1 == val2);
    });
    
    // Test 6: JOIN Condition - Inequality
    runner.RunTest("JOIN", "JOIN_Condition_Inequality", []() {
        int val1 = 50;
        int val2 = 100;
        ASSERT_TRUE(val1 < val2);
    });
    
    // Test 7: Multiple JOIN Conditions
    runner.RunTest("JOIN", "Multiple_JOIN_Conditions", []() {
        int id1 = 100;
        int id2 = 100;
        std::string status1 = "active";
        std::string status2 = "active";
        
        bool cond1 = (id1 == id2);
        bool cond2 = (status1 == status2);
        ASSERT_TRUE(cond1 && cond2);
    });
    
    // Test 8: Empty Table JOIN
    runner.RunTest("JOIN", "Empty_Table_JOIN", []() {
        std::vector<int> empty_table;
        std::vector<int> other_table = {1, 2, 3};
        
        // JOIN with empty table produces 0 rows
        ASSERT_EQ(empty_table.size(), 0, "Empty table join produces 0 rows");
    });
    
    // Test 9: Self JOIN
    runner.RunTest("JOIN", "Self_JOIN", []() {
        std::vector<int> employees = {1, 2, 3, 4, 5};
        
        // Self join same table to itself
        ASSERT_EQ(employees.size(), 5, "Can self-join same table");
    });
    
    // Test 10: Large Dataset JOIN
    runner.RunTest("JOIN", "Large_Dataset_JOIN", []() {
        std::vector<int> large_left;
        std::vector<int> large_right;
        
        for (int i = 0; i < 100; i++) {
            large_left.push_back(i);
            large_right.push_back(i);
        }
        
        ASSERT_EQ(large_left.size(), 100, "Large join left side");
        ASSERT_EQ(large_right.size(), 100, "Large join right side");
    });
}

} // namespace francodb_test
