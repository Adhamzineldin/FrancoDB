#include "framework/test_runner.h"
#include <iostream>

/**
 * FrancoDB Comprehensive Test Suite
 * Single entry point to run ALL tests for ALL modules
 */

namespace francodb_test {
// SQL Feature Tests
void RunColumnTests(TestRunner& runner);
void RunJoinTests(TestRunner& runner);
void RunForeignKeyTests(TestRunner& runner);
void RunGroupByTests(TestRunner& runner);
void RunOrderByTests(TestRunner& runner);
void RunLimitTests(TestRunner& runner);
void RunDistinctTests(TestRunner& runner);
void RunModuleStubs(TestRunner& runner);
void RunAllIntegrationTests(TestRunner& runner);
void RunBufferPoolTests(TestRunner& runner);
} // namespace francodb_test

// Storage & System Tests
void TestTuplePacking();
void TestTableHeap();
void TestDiskRecycling();
void TestFullSystem();
void TestDiskPersistence();
void TestBPlusTree();
void TestBPlusTreeSplit();
void TestBPlusTreeConcurrent();

// Execution & Parser Tests  
void TestExecutionEngine();
void TestIndexExecution();
void TestLexer();
void TestParser();
void TestEnterpriseFeatures(); // ENTERPRISE PARSER TESTS

// Concurrency Tests
void TestBasicExecution();
void TestMassiveConcurrency();
void TestShutdown();
void TestBasicExecutionRW();
void TestMassiveConcurrencyRW();
void TestShutdownRW();
void TestReadWriteMix();
void TestRealWorldTraffic();

// System Tests
void TestFrancoDBSystem();
void TestConsistencyClient();
void TestStressClient();
void TestRecovery();
void TestTimeTravel();

int main(int, char**) {
    using namespace francodb_test;
    
    std::cout << "========================================" << std::endl;
    std::cout << "  FrancoDB COMPREHENSIVE TEST SUITE" << std::endl;
    std::cout << "  ALL MODULES | ALL FEATURES | S+ GRADE" << std::endl;
    std::cout << "========================================\n" << std::endl;

    TestRunner runner;

    // SQL FEATURES
    std::cout << "\n╔═══ SQL FEATURES ═══╗" << std::endl;
    RunColumnTests(runner);
    RunJoinTests(runner);
    RunForeignKeyTests(runner);
    RunGroupByTests(runner);
    RunOrderByTests(runner);
    RunLimitTests(runner);
    RunDistinctTests(runner);

    // CORE MODULES
    std::cout << "\n╔═══ CORE MODULES ═══╗" << std::endl;
    RunModuleStubs(runner);

    // BUFFER & STORAGE
    std::cout << "\n╔═══ BUFFER & STORAGE ═══╗" << std::endl;
    RunBufferPoolTests(runner);
    runner.RunTest("Storage", "Tuple Packing", [] { TestTuplePacking(); });
    runner.RunTest("Storage", "Table Heap", [] { TestTableHeap(); });
    runner.RunTest("Storage", "Disk Recycling", [] { TestDiskRecycling(); });
    runner.RunTest("Storage", "Full Storage System", [] { TestFullSystem(); });
    runner.RunTest("Storage", "Disk Persistence", [] { TestDiskPersistence(); });
    
    // B+ TREE TESTS
    std::cout << "\n╔═══ B+ TREE INDEX ═══╗" << std::endl;
    runner.RunTest("Index", "B+ Tree Basic", [] { TestBPlusTree(); });
    runner.RunTest("Index", "B+ Tree Split", [] { TestBPlusTreeSplit(); });
    runner.RunTest("Index", "B+ Tree Concurrent", [] { TestBPlusTreeConcurrent(); });

    // EXECUTION ENGINE
    std::cout << "\n╔═══ EXECUTION ENGINE ═══╗" << std::endl;
    runner.RunTest("Execution", "Basic Execution", [] { TestExecutionEngine(); });
    runner.RunTest("Execution", "Index Execution", [] { TestIndexExecution(); });

    // PARSER
    std::cout << "\n╔═══ PARSER ═══╗" << std::endl;
    runner.RunTest("Parser", "Lexer", [] { TestLexer(); });
    runner.RunTest("Parser", "Parser", [] { TestParser(); });
    TestEnterpriseFeatures(); // Run comprehensive enterprise tests

    // CONCURRENCY
    std::cout << "\n╔═══ CONCURRENCY ═══╗" << std::endl;
    runner.RunTest("Concurrency", "Thread Pool Basic", [] { TestBasicExecution(); });
    runner.RunTest("Concurrency", "Thread Pool Massive", [] { TestMassiveConcurrency(); });
    runner.RunTest("Concurrency", "Thread Pool Shutdown", [] { TestShutdown(); });
    runner.RunTest("Concurrency", "Thread Pool Basic RW", [] { TestBasicExecutionRW(); });
    runner.RunTest("Concurrency", "Thread Pool Massive RW", [] { TestMassiveConcurrencyRW(); });
    runner.RunTest("Concurrency", "Thread Pool Shutdown RW", [] { TestShutdownRW(); });
    runner.RunTest("Concurrency", "Read/Write Mix", [] { TestReadWriteMix(); });
    runner.RunTest("Concurrency", "Real World Traffic", [] { TestRealWorldTraffic(); });

    // SYSTEM TESTS
    std::cout << "\n╔═══ SYSTEM TESTS ═══╗" << std::endl;
    runner.RunTest("System", "FrancoDB System", [] { TestFrancoDBSystem(); });
    runner.RunTest("System", "Consistency Client", [] { TestConsistencyClient(); });
    runner.RunTest("System", "Stress Client", [] { TestStressClient(); });
    runner.RunTest("System", "Log Manager", [] { TestRecovery(); });
    runner.RunTest("System", "Time Travel Test", [] { TestTimeTravel(); });

    // INTEGRATION
    std::cout << "\n╔═══ INTEGRATION ═══╗" << std::endl;
    RunAllIntegrationTests(runner);

    runner.PrintSummary();
    return runner.GetExitCode();
}

