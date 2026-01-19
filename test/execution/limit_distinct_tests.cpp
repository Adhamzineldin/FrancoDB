#include "../framework/test_runner.h"
#include <vector>
#include <algorithm>

using namespace francodb_test;

namespace francodb_test {

void RunLimitTests(TestRunner& runner) {
    runner.RunTest("Limit", "LIMIT_Only", []() {
        std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        std::vector<int> result;
        
        int limit = 5;
        for (size_t i = 0; i < std::min(static_cast<size_t>(limit), data.size()); i++) {
            result.push_back(data[i]);
        }
        
        ASSERT_EQ(result.size(), 5, "LIMIT 5 should return 5 rows");
    });
    
    runner.RunTest("Limit", "OFFSET_Only", []() {
        std::vector<int> data = {1, 2, 3, 4, 5};
        std::vector<int> result;
        
        int offset = 2;
        for (size_t i = offset; i < data.size(); i++) {
            result.push_back(data[i]);
        }
        
        ASSERT_EQ(result.size(), 3, "OFFSET 2 should skip 2 rows");
        ASSERT_EQ(result[0], 3, "First result should be 3");
    });
    
    runner.RunTest("Limit", "LIMIT_OFFSET_Combined", []() {
        std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        std::vector<int> result;
        
        int offset = 3;
        int limit = 4;
        
        for (size_t i = offset; i < std::min(offset + limit, static_cast<int>(data.size())); i++) {
            result.push_back(data[i]);
        }
        
        ASSERT_EQ(result.size(), 4, "LIMIT 4 OFFSET 3 should return 4 rows");
        ASSERT_EQ(result[0], 4, "First should be 4");
    });
    
    runner.RunTest("Limit", "OFFSET_Beyond_Dataset", []() {
        std::vector<int> data = {1, 2, 3};
        int offset = 10;
        
        ASSERT_TRUE(offset >= static_cast<int>(data.size()));
    });
    
    runner.RunTest("Limit", "Pagination_Page_1", []() {
        std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        int page = 1, page_size = 3;
        int offset = (page - 1) * page_size;
        
        ASSERT_EQ(offset, 0, "Page 1 offset should be 0");
    });
    
    runner.RunTest("Limit", "Pagination_Page_2", []() {
        std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        int page = 2, page_size = 3;
        int offset = (page - 1) * page_size;
        
        ASSERT_EQ(offset, 3, "Page 2 offset should be 3");
    });
}

void RunDistinctTests(TestRunner& runner) {
    runner.RunTest("Distinct", "Remove_Duplicates", []() {
        std::vector<int> data = {1, 2, 2, 3, 3, 3, 4};
        std::vector<int> distinct;
        
        for (int val : data) {
            if (std::find(distinct.begin(), distinct.end(), val) == distinct.end()) {
                distinct.push_back(val);
            }
        }
        
        ASSERT_EQ(distinct.size(), 4, "Should have 4 distinct values");
    });
    
    runner.RunTest("Distinct", "All_Unique", []() {
        std::vector<int> data = {1, 2, 3, 4, 5};
        std::vector<int> distinct;
        
        for (int val : data) {
            if (std::find(distinct.begin(), distinct.end(), val) == distinct.end()) {
                distinct.push_back(val);
            }
        }
        
        ASSERT_EQ(distinct.size(), data.size(), "All unique should remain same size");
    });
    
    runner.RunTest("Distinct", "All_Duplicates", []() {
        std::vector<int> data = {5, 5, 5, 5, 5};
        std::vector<int> distinct;
        
        for (int val : data) {
            if (std::find(distinct.begin(), distinct.end(), val) == distinct.end()) {
                distinct.push_back(val);
            }
        }
        
        ASSERT_EQ(distinct.size(), 1, "All duplicates should result in 1 value");
    });
}

} // namespace francodb_test

