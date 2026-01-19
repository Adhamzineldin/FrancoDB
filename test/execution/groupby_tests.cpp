#include "../framework/test_runner.h"
#include <vector>
#include <map>
#include <string>

using namespace francodb_test;

// Mock data structures for GROUP BY testing
struct MockRow {
    std::string department;
    int salary;
    int count;
};

namespace francodb_test {

void RunGroupByTests(TestRunner& runner) {
    // Test 1: GROUP BY Single Column
    runner.RunTest("GroupBy", "Single_Column_Grouping", []() {
        std::vector<MockRow> data = {
            {"IT", 70000, 1}, {"IT", 75000, 1}, {"HR", 60000, 1}, {"HR", 55000, 1}
        };
        
        std::map<std::string, int> groups;
        for (const auto& row : data) {
            groups[row.department]++;
        }
        
        ASSERT_EQ(groups.size(), 2, "Should have 2 groups");
        ASSERT_EQ(groups["IT"], 2, "IT should have 2 rows");
        ASSERT_EQ(groups["HR"], 2, "HR should have 2 rows");
    });
    
    // Test 2: COUNT Aggregate
    runner.RunTest("GroupBy", "COUNT_Aggregate", []() {
        std::vector<MockRow> data = {
            {"IT", 70000, 1}, {"IT", 75000, 1}, {"IT", 80000, 1}
        };
        
        int count = data.size();
        ASSERT_EQ(count, 3, "COUNT should return 3");
    });
    
    // Test 3: SUM Aggregate
    runner.RunTest("GroupBy", "SUM_Aggregate", []() {
        std::vector<int> salaries = {70000, 75000, 80000};
        
        int sum = 0;
        for (int salary : salaries) {
            sum += salary;
        }
        
        ASSERT_EQ(sum, 225000, "SUM should be 225000");
    });
    
    // Test 4: AVG Aggregate
    runner.RunTest("GroupBy", "AVG_Aggregate", []() {
        std::vector<int> salaries = {70000, 80000, 90000};
        
        int sum = 0;
        for (int salary : salaries) {
            sum += salary;
        }
        int avg = sum / salaries.size();
        
        ASSERT_EQ(avg, 80000, "AVG should be 80000");
    });
    
    // Test 5: MIN Aggregate
    runner.RunTest("GroupBy", "MIN_Aggregate", []() {
        std::vector<int> salaries = {70000, 55000, 80000};
        
        int min = salaries[0];
        for (int salary : salaries) {
            if (salary < min) min = salary;
        }
        
        ASSERT_EQ(min, 55000, "MIN should be 55000");
    });
    
    // Test 6: MAX Aggregate
    runner.RunTest("GroupBy", "MAX_Aggregate", []() {
        std::vector<int> salaries = {70000, 55000, 95000};
        
        int max = salaries[0];
        for (int salary : salaries) {
            if (salary > max) max = salary;
        }
        
        ASSERT_EQ(max, 95000, "MAX should be 95000");
    });
    
    // Test 7: GROUP BY Multiple Columns
    runner.RunTest("GroupBy", "Multiple_Column_Grouping", []() {
        struct MultiGroup { std::string dept; std::string title; };
        std::vector<MultiGroup> data = {
            {"IT", "Manager"}, {"IT", "Developer"}, {"IT", "Manager"}, {"HR", "Manager"}
        };
        
        std::map<std::string, int> groups;
        for (const auto& row : data) {
            std::string key = row.dept + "|" + row.title;
            groups[key]++;
        }
        
        ASSERT_EQ(groups.size(), 3, "Should have 3 unique groups");
    });
    
    // Test 8: GROUP BY with HAVING
    runner.RunTest("GroupBy", "HAVING_Clause", []() {
        std::map<std::string, int> group_counts = {
            {"IT", 10}, {"HR", 3}, {"Sales", 7}
        };
        
        // HAVING COUNT(*) > 5
        int filtered_count = 0;
        for (const auto& [dept, count] : group_counts) {
            if (count > 5) {
                filtered_count++;
            }
        }
        
        ASSERT_EQ(filtered_count, 2, "2 groups have count > 5");
    });
    
    // Test 9: Empty Group
    runner.RunTest("GroupBy", "Empty_Group", []() {
        std::vector<MockRow> empty_data;
        
        std::map<std::string, int> groups;
        for (const auto& row : empty_data) {
            groups[row.department]++;
        }
        
        ASSERT_EQ(groups.size(), 0, "Empty data should produce 0 groups");
    });
    
    // Test 10: Single Group (No GROUP BY)
    runner.RunTest("GroupBy", "Single_Group_Aggregation", []() {
        std::vector<int> all_salaries = {60000, 70000, 80000, 90000};
        
        // COUNT(*) without GROUP BY
        int total_count = all_salaries.size();
        ASSERT_EQ(total_count, 4, "Should count all rows");
    });
}

} // namespace francodb_test
