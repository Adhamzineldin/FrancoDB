#include "framework/test_runner.h"
#include <iostream>

/**
 * FrancoDB Comprehensive Test Suite
 * Single entry point to run ALL tests for ALL modules
 * Shows pass/fail results with timing
 */

namespace francodb_test {

// Forward declarations of test modules
void RunColumnTests(TestRunner& runner);
void RunJoinTests(TestRunner& runner);
void RunForeignKeyTests(TestRunner& runner);
void RunGroupByTests(TestRunner& runner);
void RunOrderByTests(TestRunner& runner);
void RunLimitTests(TestRunner& runner);
void RunDistinctTests(TestRunner& runner);
void RunModuleStubs(TestRunner& runner);

} // namespace francodb_test

int main(int, char**) {
    using namespace francodb_test;

    std::cout << "========================================" << std::endl;
    std::cout << "  FrancoDB Comprehensive Test Suite" << std::endl;
    std::cout << "  All modules, one command" << std::endl;
    std::cout << "========================================\n" << std::endl;

    TestRunner runner;

    std::cout << "\n[1/7] Column Constraint Tests..." << std::endl;
    RunColumnTests(runner);

    std::cout << "\n[2/7] JOIN Tests..." << std::endl;
    RunJoinTests(runner);

    std::cout << "\n[3/7] Foreign Key Tests..." << std::endl;
    RunForeignKeyTests(runner);

    std::cout << "\n[4/7] GROUP BY Tests..." << std::endl;
    RunGroupByTests(runner);

    std::cout << "\n[5/7] ORDER BY Tests..." << std::endl;
    RunOrderByTests(runner);

    std::cout << "\n[6/7] LIMIT/DISTINCT Tests..." << std::endl;
    RunLimitTests(runner);
    RunDistinctTests(runner);

    std::cout << "\n[7/7] Core Module Smoke Tests..." << std::endl;
    RunModuleStubs(runner);

    runner.PrintSummary();
    return runner.GetExitCode();
}
