#pragma once

#include <iostream>
#include <vector>
#include <functional>
#include <chrono>
#include <iomanip>
#include <string>
#include <sstream>

/**
 * Professional Test Framework
 * Google Test/Catch2 Style - Company-Grade Quality
 */

namespace francodb_test {

struct TestResult {
    std::string test_name;
    std::string module;
    bool passed;
    std::string error_message;
    double duration_ms;
};

class TestRunner {
private:
    std::vector<TestResult> results_;
    int total_tests_ = 0;
    int passed_tests_ = 0;
    int failed_tests_ = 0;

public:
    void RunTest(const std::string& module, const std::string& test_name, std::function<void()> test_func) {
        total_tests_++;

        auto start = std::chrono::high_resolution_clock::now();
        TestResult result;
        result.test_name = test_name;
        result.module = module;

        try {
            test_func();
            result.passed = true;
            passed_tests_++;
        } catch (const std::exception& e) {
            result.passed = false;
            result.error_message = e.what();
            failed_tests_++;
        } catch (...) {
            result.passed = false;
            result.error_message = "Unknown exception";
            failed_tests_++;
        }

        auto end = std::chrono::high_resolution_clock::now();
        result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        results_.push_back(result);

        if (result.passed) {
            std::cout << "  [PASS] " << test_name << " (" << std::fixed << std::setprecision(2)
                      << result.duration_ms << "ms)" << std::endl;
        } else {
            std::cout << "  [FAIL] " << test_name << " - " << result.error_message << std::endl;
        }
    }

    void PrintSummary() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "  TEST SUMMARY" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Total Tests:  " << total_tests_ << std::endl;
        std::cout << "Passed:       " << passed_tests_ << " [PASS]" << std::endl;
        std::cout << "Failed:       " << failed_tests_ << " [FAIL]" << std::endl;
        std::cout << "Success Rate: " << std::fixed << std::setprecision(1)
                  << (total_tests_ == 0 ? 0.0 : (100.0 * passed_tests_ / total_tests_)) << "%" << std::endl;

        if (failed_tests_ > 0) {
            std::cout << "\n========================================" << std::endl;
            std::cout << "  FAILED TESTS" << std::endl;
            std::cout << "========================================" << std::endl;
            for (const auto& result : results_) {
                if (!result.passed) {
                    std::cout << "[FAIL] [" << result.module << "] " << result.test_name << std::endl;
                    std::cout << "  Error: " << result.error_message << std::endl;
                }
            }
        }

        std::cout << "\n========================================" << std::endl;
        if (failed_tests_ == 0) {
            std::cout << "  ALL TESTS PASSED [OK]" << std::endl;
        } else {
            std::cout << "  SOME TESTS FAILED [FAIL]" << std::endl;
        }
        std::cout << "========================================\n" << std::endl;
    }

    int GetExitCode() const { return failed_tests_ == 0 ? 0 : 1; }
};

// Professional assertion macros - handle all types properly
#define ASSERT(condition, message) \
    if (!(condition)) { \
        throw std::runtime_error(std::string("Assertion failed: ") + (message)); \
    }

#define ASSERT_TRUE(condition) \
    if (!(condition)) { \
        throw std::runtime_error(std::string("Expected TRUE, got FALSE: ") + #condition); \
    }

#define ASSERT_FALSE(condition) \
    if (condition) { \
        throw std::runtime_error(std::string("Expected FALSE, got TRUE: ") + #condition); \
    }

// Template assertion for numeric types
template<typename T, typename U>
void AssertEquals(const T& actual, const U& expected, const std::string& message) {
    if (actual != expected) {
        std::ostringstream oss;
        oss << message << " (assertion failed)";
        throw std::runtime_error(oss.str());
    }
}

// String comparison specialization
inline void AssertEquals(const std::string& actual, const char* expected, const std::string& message) {
    std::string exp_str(expected);
    if (actual != exp_str) {
        std::ostringstream oss;
        oss << message << " (expected: \"" << exp_str << "\", got: \"" << actual << "\")";
        throw std::runtime_error(oss.str());
    }
}

inline void AssertEquals(const char* actual, const char* expected, const std::string& message) {
    std::string act_str(actual);
    std::string exp_str(expected);
    if (act_str != exp_str) {
        std::ostringstream oss;
        oss << message << " (expected: \"" << exp_str << "\", got: \"" << act_str << "\")";
        throw std::runtime_error(oss.str());
    }
}

#define ASSERT_EQ(actual, expected, message) \
    francodb_test::AssertEquals((actual), (expected), (message))

#define ASSERT_NE(actual, expected, message) \
    if ((actual) == (expected)) { \
        std::ostringstream oss; \
        oss << (message) << " (values should not be equal: " << (actual) << ")"; \
        throw std::runtime_error(oss.str()); \
    }

#define ASSERT_LT(actual, expected, message) \
    if ((actual) >= (expected)) { \
        std::ostringstream oss; \
        oss << (message) << " (expected: " << (actual) << " < " << (expected) << ")"; \
        throw std::runtime_error(oss.str()); \
    }

#define ASSERT_GT(actual, expected, message) \
    if ((actual) <= (expected)) { \
        std::ostringstream oss; \
        oss << (message) << " (expected: " << (actual) << " > " << (expected) << ")"; \
        throw std::runtime_error(oss.str()); \
    }

} // namespace francodb_test

