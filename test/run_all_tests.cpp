#include "framework/test_runner.h"
#include <iostream>

/**
 * ChronosDB Comprehensive Test Suite
 * Single entry point to run ALL tests for ALL modules
 */

namespace chronosdb_test {
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
} // namespace chronosdb_test

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
void TestChronosDBSystem();
void TestConsistencyClient();
void TestStressClient();
void TestRecovery();
void TestTimeTravel();
void TestCheckpoint();

// AI Module Tests - MetricsStore
void TestMetricsStoreBasicRecording();
void TestMetricsStoreCountEvents();
void TestMetricsStoreMutationCount();
void TestMetricsStoreConcurrentAccess();
void TestMetricsStoreRingBufferOverflow();

// AI Module Tests - DML Observer
void TestDMLObserverRegistration();
void TestDMLObserverNotification();
void TestDMLObserverBlocking();
void TestDMLObserverMultipleObservers();

// AI Module Tests - UCB1 Bandit
void TestUCB1BanditInitialState();
void TestUCB1BanditRewardRecording();
void TestUCB1BanditStrategySelection();
void TestUCB1BanditPerTableContextual();
void TestUCB1BanditExplorationPhase();
void TestUCB1BanditReset();

// AI Module Tests - Anomaly Detection
void TestAnomalySeverityClassification();
void TestAnomalySeverityToString();
void TestAnomalyDetectorRecording();
void TestMutationMonitorBasic();
void TestMutationMonitorHistoricalRates();
void TestUserBehaviorProfiler();

// AI Module Tests - Hotspot Detection
void TestTemporalAccessTrackerBasic();
void TestTemporalAccessTrackerHotTimestamps();
void TestTemporalAccessTrackerFrequencyHistogram();
void TestHotspotDetectorDBSCAN();
void TestHotspotDetectorNoHotspots();
void TestHotspotDetectorCUSUM();
void TestHotspotDetectorSingleCluster();

// AI Module Tests - Scheduler
void TestAISchedulerLifecycle();
void TestAISchedulerPeriodicTask();
void TestAISchedulerOneShotTask();
void TestAISchedulerTaskListing();
void TestAISchedulerCancellation();

int main(int, char**) {
    using namespace chronosdb_test;
    
    std::cout << "========================================" << std::endl;
    std::cout << "  ChronosDB COMPREHENSIVE TEST SUITE" << std::endl;
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
    runner.RunTest("System", "ChronosDB System", [] { TestChronosDBSystem(); });
    runner.RunTest("System", "Consistency Client", [] { TestConsistencyClient(); });
    runner.RunTest("System", "Stress Client", [] { TestStressClient(); });
    runner.RunTest("System", "Log Manager", [] { TestRecovery(); });
    runner.RunTest("System", "Time Travel Test", [] { TestTimeTravel(); });
    runner.RunTest("System", "Time Travel Test", [] { TestCheckpoint(); });


    // AI MODULE - METRICS STORE
    std::cout << "\n╔═══ AI: METRICS STORE ═══╗" << std::endl;
    runner.RunTest("AI-Metrics", "Basic Recording", [] { TestMetricsStoreBasicRecording(); });
    runner.RunTest("AI-Metrics", "Count Events", [] { TestMetricsStoreCountEvents(); });
    runner.RunTest("AI-Metrics", "Mutation Count", [] { TestMetricsStoreMutationCount(); });
    runner.RunTest("AI-Metrics", "Concurrent Access", [] { TestMetricsStoreConcurrentAccess(); });
    runner.RunTest("AI-Metrics", "Ring Buffer Overflow", [] { TestMetricsStoreRingBufferOverflow(); });

    // AI MODULE - DML OBSERVER
    std::cout << "\n╔═══ AI: DML OBSERVER ═══╗" << std::endl;
    runner.RunTest("AI-Observer", "Registration", [] { TestDMLObserverRegistration(); });
    runner.RunTest("AI-Observer", "Notification", [] { TestDMLObserverNotification(); });
    runner.RunTest("AI-Observer", "Blocking", [] { TestDMLObserverBlocking(); });
    runner.RunTest("AI-Observer", "Multiple Observers", [] { TestDMLObserverMultipleObservers(); });

    // AI MODULE - UCB1 BANDIT (Self-Learning Engine)
    std::cout << "\n╔═══ AI: UCB1 BANDIT ═══╗" << std::endl;
    runner.RunTest("AI-Bandit", "Initial State", [] { TestUCB1BanditInitialState(); });
    runner.RunTest("AI-Bandit", "Reward Recording", [] { TestUCB1BanditRewardRecording(); });
    runner.RunTest("AI-Bandit", "Strategy Selection", [] { TestUCB1BanditStrategySelection(); });
    runner.RunTest("AI-Bandit", "Per-Table Contextual", [] { TestUCB1BanditPerTableContextual(); });
    runner.RunTest("AI-Bandit", "Exploration Phase", [] { TestUCB1BanditExplorationPhase(); });
    runner.RunTest("AI-Bandit", "Reset", [] { TestUCB1BanditReset(); });

    // AI MODULE - ANOMALY DETECTION (Immune System)
    std::cout << "\n╔═══ AI: ANOMALY DETECTION ═══╗" << std::endl;
    runner.RunTest("AI-Immune", "Severity Classification", [] { TestAnomalySeverityClassification(); });
    runner.RunTest("AI-Immune", "Severity ToString", [] { TestAnomalySeverityToString(); });
    runner.RunTest("AI-Immune", "Anomaly Recording", [] { TestAnomalyDetectorRecording(); });
    runner.RunTest("AI-Immune", "Mutation Monitor", [] { TestMutationMonitorBasic(); });
    runner.RunTest("AI-Immune", "Historical Rates", [] { TestMutationMonitorHistoricalRates(); });
    runner.RunTest("AI-Immune", "User Behavior Profiler", [] { TestUserBehaviorProfiler(); });

    // AI MODULE - TEMPORAL HOTSPOT DETECTION
    std::cout << "\n╔═══ AI: TEMPORAL HOTSPOT ═══╗" << std::endl;
    runner.RunTest("AI-Temporal", "Access Tracker Basic", [] { TestTemporalAccessTrackerBasic(); });
    runner.RunTest("AI-Temporal", "Hot Timestamps", [] { TestTemporalAccessTrackerHotTimestamps(); });
    runner.RunTest("AI-Temporal", "Frequency Histogram", [] { TestTemporalAccessTrackerFrequencyHistogram(); });
    runner.RunTest("AI-Temporal", "DBSCAN Clustering", [] { TestHotspotDetectorDBSCAN(); });
    runner.RunTest("AI-Temporal", "No Hotspots", [] { TestHotspotDetectorNoHotspots(); });
    runner.RunTest("AI-Temporal", "CUSUM Change Points", [] { TestHotspotDetectorCUSUM(); });
    runner.RunTest("AI-Temporal", "Single Cluster", [] { TestHotspotDetectorSingleCluster(); });

    // AI MODULE - SCHEDULER
    std::cout << "\n╔═══ AI: SCHEDULER ═══╗" << std::endl;
    runner.RunTest("AI-Scheduler", "Lifecycle", [] { TestAISchedulerLifecycle(); });
    runner.RunTest("AI-Scheduler", "Periodic Task", [] { TestAISchedulerPeriodicTask(); });
    runner.RunTest("AI-Scheduler", "One-Shot Task", [] { TestAISchedulerOneShotTask(); });
    runner.RunTest("AI-Scheduler", "Task Listing", [] { TestAISchedulerTaskListing(); });
    runner.RunTest("AI-Scheduler", "Cancellation", [] { TestAISchedulerCancellation(); });

    // INTEGRATION
    std::cout << "\n╔═══ INTEGRATION ═══╗" << std::endl;
    RunAllIntegrationTests(runner);

    runner.PrintSummary();
    return runner.GetExitCode();
}

