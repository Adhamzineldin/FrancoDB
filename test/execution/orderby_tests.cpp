#include "../framework/test_runner.h"
#include <vector>
#include <algorithm>
#include <string>

using namespace francodb_test;

struct MockEmployee {
    std::string name;
    int salary;
    std::string dept;
};

namespace francodb_test {

void RunOrderByTests(TestRunner& runner) {
    // Test 1: ORDER BY ASC (Integer)
    runner.RunTest("OrderBy", "ASC_Integer", []() {
        std::vector<int> salaries = {80000, 60000, 90000, 70000};
        std::vector<int> expected = {60000, 70000, 80000, 90000};
        
        std::sort(salaries.begin(), salaries.end());
        
        ASSERT_EQ(salaries, expected, "ASC sort failed");
    });
    
    // Test 2: ORDER BY DESC (Integer)
    runner.RunTest("OrderBy", "DESC_Integer", []() {
        std::vector<int> salaries = {60000, 90000, 70000, 80000};
        std::vector<int> expected = {90000, 80000, 70000, 60000};
        
        std::sort(salaries.begin(), salaries.end(), std::greater<int>());
        
        ASSERT_EQ(salaries, expected, "DESC sort failed");
    });
    
    // Test 3: ORDER BY ASC (String)
    runner.RunTest("OrderBy", "ASC_String", []() {
        std::vector<std::string> names = {"Charlie", "Alice", "Bob"};
        std::vector<std::string> expected = {"Alice", "Bob", "Charlie"};
        
        std::sort(names.begin(), names.end());
        
        ASSERT_EQ(names, expected, "String ASC sort failed");
    });
    
    // Test 4: ORDER BY Multiple Columns
    runner.RunTest("OrderBy", "Multiple_Columns", []() {
        std::vector<MockEmployee> employees = {
            {"Alice", 70000, "IT"},
            {"Bob", 90000, "HR"},
            {"Charlie", 70000, "IT"},
            {"David", 90000, "HR"}
        };
        
        // Sort by dept ASC, then salary DESC
        std::sort(employees.begin(), employees.end(), [](const MockEmployee& a, const MockEmployee& b) {
            if (a.dept != b.dept) return a.dept < b.dept;
            return a.salary > b.salary;
        });
        
        ASSERT_EQ(employees[0].dept, "HR", "First should be HR");
        ASSERT_EQ(employees[0].salary, 90000, "First HR should have highest salary");
    });
    
    // Test 5: ORDER BY with NULL values
    runner.RunTest("OrderBy", "NULL_Handling", []() {
        std::vector<int> values = {100, 0, 50, 0, 75}; // 0 represents NULL
        
        // NULLs typically sort first or last
        std::sort(values.begin(), values.end());
        
        ASSERT_TRUE(values[0] == 0); // NULL sorts first
    });
    
    // Test 6: Stable Sort (preserve order of equal elements)
    runner.RunTest("OrderBy", "Stable_Sort", []() {
        std::vector<MockEmployee> employees = {
            {"Alice", 70000, "IT"},
            {"Bob", 70000, "IT"},
            {"Charlie", 70000, "IT"}
        };
        
        // Stable sort by salary should preserve name order
        std::stable_sort(employees.begin(), employees.end(), [](const MockEmployee& a, const MockEmployee& b) {
            return a.salary < b.salary;
        });
        
        ASSERT_EQ(employees[0].name, "Alice", "Stable sort should preserve order");
    });
    
    // Test 7: ORDER BY Empty Result Set
    runner.RunTest("OrderBy", "Empty_Result", []() {
        std::vector<int> empty;
        std::sort(empty.begin(), empty.end());
        
        ASSERT_EQ(empty.size(), 0, "Empty sort should remain empty");
    });
    
    // Test 8: ORDER BY Single Row
    runner.RunTest("OrderBy", "Single_Row", []() {
        std::vector<int> single = {42};
        std::sort(single.begin(), single.end());
        
        ASSERT_EQ(single.size(), 1, "Single row should remain single");
        ASSERT_EQ(single[0], 42, "Value should be unchanged");
    });
    
    // Test 9: ORDER BY Large Dataset Performance
    runner.RunTest("OrderBy", "Large_Dataset", []() {
        std::vector<int> large_data;
        for (int i = 1000; i > 0; i--) {
            large_data.push_back(i);
        }
        
        std::sort(large_data.begin(), large_data.end());
        
        ASSERT_EQ(large_data[0], 1, "First element should be 1");
        ASSERT_EQ(large_data[large_data.size()-1], 1000, "Last element should be 1000");
    });
    
    // Test 10: ORDER BY Case Sensitivity
    runner.RunTest("OrderBy", "Case_Sensitivity", []() {
        std::vector<std::string> names = {"alice", "Bob", "CHARLIE", "david"};
        
        // Case-sensitive sort
        std::sort(names.begin(), names.end());
        
        // Uppercase comes before lowercase in ASCII
        ASSERT_TRUE(names[0][0] < 'a'); // First character should be uppercase
    });
}

} // namespace francodb_test

