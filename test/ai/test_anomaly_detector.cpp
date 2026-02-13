#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <string>
#include <chrono>
#include <thread>

#include "ai/immune/anomaly_detector.h"
#include "ai/immune/mutation_monitor.h"
#include "ai/immune/user_profiler.h"
#include "ai/ai_config.h"

using namespace chronosdb;
using namespace chronosdb::ai;

/**
 * Anomaly Detection Tests
 *
 * Tests the Z-score anomaly detection engine, mutation monitor,
 * and user behavior profiler.
 * Covers: severity classification, anomaly recording, mutation tracking,
 * user profiling, and end-to-end anomaly detection.
 */

// Helper to get current time in microseconds
static uint64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void TestAnomalySeverityClassification() {
    std::cout << "[TEST] Anomaly Severity Classification..." << std::endl;

    // z < 2.0 -> NONE
    assert(AnomalyDetector::Classify(0.0) == AnomalySeverity::NONE);
    assert(AnomalyDetector::Classify(1.0) == AnomalySeverity::NONE);
    assert(AnomalyDetector::Classify(1.99) == AnomalySeverity::NONE);
    std::cout << "  -> z < 2.0 correctly classified as NONE" << std::endl;

    // z >= 2.0 -> LOW
    assert(AnomalyDetector::Classify(2.0) == AnomalySeverity::LOW);
    assert(AnomalyDetector::Classify(2.5) == AnomalySeverity::LOW);
    assert(AnomalyDetector::Classify(2.99) == AnomalySeverity::LOW);
    std::cout << "  -> 2.0 <= z < 3.0 correctly classified as LOW" << std::endl;

    // z >= 3.0 -> MEDIUM
    assert(AnomalyDetector::Classify(3.0) == AnomalySeverity::MEDIUM);
    assert(AnomalyDetector::Classify(3.5) == AnomalySeverity::MEDIUM);
    assert(AnomalyDetector::Classify(3.99) == AnomalySeverity::MEDIUM);
    std::cout << "  -> 3.0 <= z < 4.0 correctly classified as MEDIUM" << std::endl;

    // z >= 4.0 -> HIGH
    assert(AnomalyDetector::Classify(4.0) == AnomalySeverity::HIGH);
    assert(AnomalyDetector::Classify(10.0) == AnomalySeverity::HIGH);
    assert(AnomalyDetector::Classify(100.0) == AnomalySeverity::HIGH);
    std::cout << "  -> z >= 4.0 correctly classified as HIGH" << std::endl;

    // Negative z-scores should be NONE
    assert(AnomalyDetector::Classify(-1.0) == AnomalySeverity::NONE);
    assert(AnomalyDetector::Classify(-5.0) == AnomalySeverity::NONE);
    std::cout << "  -> Negative z-scores classified as NONE" << std::endl;

    std::cout << "[SUCCESS] Anomaly Severity Classification passed!" << std::endl;
}

void TestAnomalySeverityToString() {
    std::cout << "[TEST] Anomaly Severity ToString..." << std::endl;

    assert(AnomalyDetector::SeverityToString(AnomalySeverity::NONE) == "NONE");
    assert(AnomalyDetector::SeverityToString(AnomalySeverity::LOW) == "LOW");
    assert(AnomalyDetector::SeverityToString(AnomalySeverity::MEDIUM) == "MEDIUM");
    assert(AnomalyDetector::SeverityToString(AnomalySeverity::HIGH) == "HIGH");
    std::cout << "  -> All severity levels have correct string representations" << std::endl;

    std::cout << "[SUCCESS] Anomaly Severity ToString passed!" << std::endl;
}

void TestAnomalyDetectorRecording() {
    std::cout << "[TEST] AnomalyDetector Recording..." << std::endl;

    AnomalyDetector detector;

    assert(detector.GetTotalAnomalies() == 0);
    std::cout << "  -> Initially 0 anomalies" << std::endl;

    // Record some anomalies
    AnomalyReport report;
    report.table_name = "orders";
    report.user = "suspicious_user";
    report.severity = AnomalySeverity::LOW;
    report.z_score = 2.5;
    report.current_rate = 100.0;
    report.mean_rate = 20.0;
    report.std_dev = 32.0;
    report.timestamp_us = NowUs();
    report.description = "Elevated mutation rate on orders table";

    detector.RecordAnomaly(report);
    assert(detector.GetTotalAnomalies() == 1);

    // Record a HIGH severity anomaly
    AnomalyReport high_report;
    high_report.table_name = "critical_data";
    high_report.user = "attacker";
    high_report.severity = AnomalySeverity::HIGH;
    high_report.z_score = 5.2;
    high_report.current_rate = 500.0;
    high_report.mean_rate = 10.0;
    high_report.std_dev = 94.23;
    high_report.timestamp_us = NowUs();
    high_report.description = "Massive deletion spike detected";

    detector.RecordAnomaly(high_report);
    assert(detector.GetTotalAnomalies() == 2);

    auto recent = detector.GetRecentAnomalies(10);
    assert(recent.size() == 2);
    std::cout << "  -> Recorded 2 anomalies, retrieved " << recent.size() << std::endl;

    // Verify data integrity
    bool found_high = false;
    for (const auto& a : recent) {
        if (a.severity == AnomalySeverity::HIGH) {
            assert(a.table_name == "critical_data");
            assert(a.z_score > 5.0);
            found_high = true;
        }
    }
    assert(found_high);
    std::cout << "  -> HIGH severity anomaly data preserved correctly" << std::endl;

    std::cout << "[SUCCESS] AnomalyDetector Recording passed!" << std::endl;
}

void TestMutationMonitorBasic() {
    std::cout << "[TEST] MutationMonitor Basic..." << std::endl;

    MutationMonitor monitor;

    uint64_t now = NowUs();

    // Record mutations for different tables
    for (int i = 0; i < 50; i++) {
        monitor.RecordMutation("orders", DMLOperation::INSERT, 1, now + i * 1000);
    }
    for (int i = 0; i < 30; i++) {
        monitor.RecordMutation("products", DMLOperation::UPDATE, 2, now + i * 1000);
    }
    for (int i = 0; i < 10; i++) {
        monitor.RecordMutation("orders", DMLOperation::DELETE_OP, 5, now + i * 1000);
    }

    // Check mutation counts
    uint64_t orders_count = monitor.GetMutationCount("orders", 60ULL * 1000000);
    assert(orders_count == 60); // 50 inserts + 10 deletes
    std::cout << "  -> 'orders' mutations = " << orders_count << " (expected 60)" << std::endl;

    uint64_t products_count = monitor.GetMutationCount("products", 60ULL * 1000000);
    assert(products_count == 30);
    std::cout << "  -> 'products' mutations = " << products_count << " (expected 30)" << std::endl;

    // Check monitored tables
    auto tables = monitor.GetMonitoredTables();
    assert(tables.size() == 2);
    std::cout << "  -> Monitoring " << tables.size() << " tables" << std::endl;

    // Mutation rate should be > 0 for active tables
    double rate = monitor.GetMutationRate("orders");
    std::cout << "  -> 'orders' mutation rate = " << rate << " rows/sec" << std::endl;
    assert(rate >= 0.0);

    std::cout << "[SUCCESS] MutationMonitor Basic passed!" << std::endl;
}

void TestMutationMonitorHistoricalRates() {
    std::cout << "[TEST] MutationMonitor Historical Rates..." << std::endl;

    MutationMonitor monitor;

    uint64_t now = NowUs();
    uint64_t interval = RATE_INTERVAL_US; // 1 minute intervals

    // Simulate mutations across multiple intervals
    // Interval 0: 10 mutations
    for (int i = 0; i < 10; i++) {
        monitor.RecordMutation("test_table", DMLOperation::INSERT, 1,
                               now - 5 * interval + i * 100);
    }
    // Interval 1: 20 mutations
    for (int i = 0; i < 20; i++) {
        monitor.RecordMutation("test_table", DMLOperation::INSERT, 1,
                               now - 4 * interval + i * 100);
    }
    // Interval 2: 15 mutations
    for (int i = 0; i < 15; i++) {
        monitor.RecordMutation("test_table", DMLOperation::UPDATE, 1,
                               now - 3 * interval + i * 100);
    }

    auto rates = monitor.GetHistoricalRates("test_table", 10, interval);
    std::cout << "  -> Historical rates vector size = " << rates.size() << std::endl;
    for (size_t i = 0; i < rates.size(); i++) {
        std::cout << "    Interval " << i << ": " << rates[i] << " mutations" << std::endl;
    }

    // Empty table should return empty rates
    auto empty_rates = monitor.GetHistoricalRates("nonexistent", 10, interval);
    assert(empty_rates.empty() || empty_rates[0] == 0.0);
    std::cout << "  -> Non-existent table returns empty/zero rates" << std::endl;

    std::cout << "[SUCCESS] MutationMonitor Historical Rates passed!" << std::endl;
}

void TestUserBehaviorProfiler() {
    std::cout << "[TEST] UserBehaviorProfiler..." << std::endl;

    UserBehaviorProfiler profiler;

    uint64_t now = NowUs();

    // Simulate normal user behavior
    for (int i = 0; i < 100; i++) {
        profiler.RecordEvent("normal_user", DMLOperation::SELECT, "orders",
                             now + i * 10000); // Steady rate
        if (i % 10 == 0) {
            profiler.RecordEvent("normal_user", DMLOperation::INSERT, "orders",
                                 now + i * 10000);
        }
    }

    // Simulate anomalous user behavior (rapid-fire deletes)
    for (int i = 0; i < 200; i++) {
        profiler.RecordEvent("bad_user", DMLOperation::DELETE_OP, "critical_data",
                             now + i * 100); // Very rapid
    }

    // Check profiles
    auto normal_profile = profiler.GetProfile("normal_user");
    assert(normal_profile.username == "normal_user");
    assert(normal_profile.total_events > 0);
    std::cout << "  -> normal_user: " << normal_profile.total_events << " events" << std::endl;

    auto bad_profile = profiler.GetProfile("bad_user");
    assert(bad_profile.username == "bad_user");
    assert(bad_profile.total_events == 200);
    std::cout << "  -> bad_user: " << bad_profile.total_events << " events" << std::endl;

    // Deviation scores
    double normal_deviation = profiler.GetDeviationScore("normal_user");
    double bad_deviation = profiler.GetDeviationScore("bad_user");
    std::cout << "  -> normal_user deviation = " << normal_deviation << std::endl;
    std::cout << "  -> bad_user deviation = " << bad_deviation << std::endl;

    // Both scores should be non-negative
    assert(normal_deviation >= 0.0);
    assert(bad_deviation >= 0.0);

    // Check all profiles
    auto all = profiler.GetAllProfiles();
    assert(all.size() == 2);
    std::cout << "  -> Total profiled users = " << all.size() << std::endl;

    std::cout << "[SUCCESS] UserBehaviorProfiler passed!" << std::endl;
}
