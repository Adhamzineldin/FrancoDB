#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <string>

#include "ai/learning/bandit.h"
#include "ai/ai_config.h"

using namespace chronosdb;
using namespace chronosdb::ai;

/**
 * UCB1 Bandit Tests
 *
 * Tests the multi-armed bandit algorithm that selects between
 * Sequential Scan and Index Scan strategies.
 * Covers: initial state, reward computation, strategy selection,
 * per-table contextual learning, and UCB score calculation.
 */

void TestUCB1BanditInitialState() {
    std::cout << "[TEST] UCB1Bandit Initial State..." << std::endl;

    UCB1Bandit bandit;

    // Initially should not have sufficient data
    assert(!bandit.HasSufficientData());
    std::cout << "  -> No sufficient data initially" << std::endl;

    auto stats = bandit.GetStats();
    assert(stats.size() == 2); // Two arms: SeqScan and IndexScan
    std::cout << "  -> Stats has 2 arms" << std::endl;

    for (const auto& arm : stats) {
        assert(arm.total_pulls == 0);
        assert(arm.average_reward == 0.0 || std::isnan(arm.average_reward) || arm.average_reward >= 0.0);
    }
    std::cout << "  -> Both arms start with 0 pulls" << std::endl;

    std::cout << "[SUCCESS] UCB1Bandit Initial State passed!" << std::endl;
}

void TestUCB1BanditRewardRecording() {
    std::cout << "[TEST] UCB1Bandit Reward Recording..." << std::endl;

    UCB1Bandit bandit;

    // Record outcomes for sequential scan
    for (int i = 0; i < 20; i++) {
        bandit.RecordOutcome(ScanStrategy::SEQUENTIAL_SCAN, "test_table",
                             50.0, 1000); // 50ms, 1000 rows
    }

    // Record outcomes for index scan (faster)
    for (int i = 0; i < 20; i++) {
        bandit.RecordOutcome(ScanStrategy::INDEX_SCAN, "test_table",
                             5.0, 100); // 5ms, 100 rows
    }

    auto stats = bandit.GetStats();
    bool found_seq = false, found_idx = false;
    for (const auto& arm : stats) {
        if (arm.strategy == ScanStrategy::SEQUENTIAL_SCAN) {
            assert(arm.total_pulls == 20);
            found_seq = true;
            std::cout << "  -> SeqScan: " << arm.total_pulls << " pulls, avg_reward = "
                      << arm.average_reward << std::endl;
        }
        if (arm.strategy == ScanStrategy::INDEX_SCAN) {
            assert(arm.total_pulls == 20);
            found_idx = true;
            std::cout << "  -> IndexScan: " << arm.total_pulls << " pulls, avg_reward = "
                      << arm.average_reward << std::endl;
        }
    }
    assert(found_seq && found_idx);

    std::cout << "[SUCCESS] UCB1Bandit Reward Recording passed!" << std::endl;
}

void TestUCB1BanditStrategySelection() {
    std::cout << "[TEST] UCB1Bandit Strategy Selection..." << std::endl;

    UCB1Bandit bandit;

    // Train: IndexScan is much faster for this table
    for (int i = 0; i < 40; i++) {
        bandit.RecordOutcome(ScanStrategy::SEQUENTIAL_SCAN, "fast_index_table",
                             200.0, 5000); // Slow: 200ms
        bandit.RecordOutcome(ScanStrategy::INDEX_SCAN, "fast_index_table",
                             2.0, 50);     // Fast: 2ms
    }

    assert(bandit.HasSufficientData());
    std::cout << "  -> Has sufficient data after 80 outcomes" << std::endl;

    // Feature vector (simulated)
    QueryFeatures features;
    features.table_row_count_log = 15.0;
    features.where_clause_count = 1.0;
    features.has_equality_predicate = 1.0;
    features.has_index_available = 1.0;
    features.selectivity_estimate = 0.01;
    features.column_count = 3.0;
    features.has_order_by = 0.0;
    features.has_limit = 0.0;

    // After training, should prefer IndexScan for this table
    ScanStrategy selected = bandit.SelectStrategy(features, "fast_index_table");
    std::cout << "  -> Selected strategy: "
              << (selected == ScanStrategy::INDEX_SCAN ? "INDEX_SCAN" : "SEQUENTIAL_SCAN")
              << std::endl;

    // The bandit should strongly prefer index scan given the reward difference
    // (2ms vs 200ms -> reward 0.98 vs 0.33)
    assert(selected == ScanStrategy::INDEX_SCAN);
    std::cout << "  -> Correctly learned to prefer INDEX_SCAN" << std::endl;

    std::cout << "[SUCCESS] UCB1Bandit Strategy Selection passed!" << std::endl;
}

void TestUCB1BanditPerTableContextual() {
    std::cout << "[TEST] UCB1Bandit Per-Table Contextual Learning..." << std::endl;

    UCB1Bandit bandit;

    // Table A: SeqScan is better (small table, no good index)
    for (int i = 0; i < 30; i++) {
        bandit.RecordOutcome(ScanStrategy::SEQUENTIAL_SCAN, "small_table",
                             1.0, 10);    // Fast seq scan
        bandit.RecordOutcome(ScanStrategy::INDEX_SCAN, "small_table",
                             5.0, 10);    // Slow index overhead
    }

    // Table B: IndexScan is better (large table, selective query)
    for (int i = 0; i < 30; i++) {
        bandit.RecordOutcome(ScanStrategy::SEQUENTIAL_SCAN, "large_table",
                             500.0, 100000); // Slow full scan
        bandit.RecordOutcome(ScanStrategy::INDEX_SCAN, "large_table",
                             3.0, 50);       // Fast index lookup
    }

    auto stats = bandit.GetStats();
    for (const auto& arm : stats) {
        std::cout << "  -> Arm " << (arm.strategy == ScanStrategy::SEQUENTIAL_SCAN ? "SeqScan" : "IndexScan")
                  << ": pulls=" << arm.total_pulls
                  << ", avg_reward=" << arm.average_reward
                  << ", ucb=" << arm.ucb_score << std::endl;
    }

    QueryFeatures features;
    features.table_row_count_log = 5.0;
    features.where_clause_count = 0.0;
    features.has_equality_predicate = 0.0;
    features.has_index_available = 0.0;
    features.selectivity_estimate = 1.0;
    features.column_count = 5.0;
    features.has_order_by = 0.0;
    features.has_limit = 0.0;

    // For small_table, should prefer SeqScan (contextual)
    ScanStrategy small_strategy = bandit.SelectStrategy(features, "small_table");
    std::cout << "  -> small_table strategy: "
              << (small_strategy == ScanStrategy::SEQUENTIAL_SCAN ? "SEQUENTIAL" : "INDEX")
              << std::endl;

    // For large_table, should prefer IndexScan (contextual)
    features.table_row_count_log = 17.0;
    features.has_index_available = 1.0;
    ScanStrategy large_strategy = bandit.SelectStrategy(features, "large_table");
    std::cout << "  -> large_table strategy: "
              << (large_strategy == ScanStrategy::SEQUENTIAL_SCAN ? "SEQUENTIAL" : "INDEX")
              << std::endl;

    // Verify contextual differentiation
    assert(small_strategy == ScanStrategy::SEQUENTIAL_SCAN);
    assert(large_strategy == ScanStrategy::INDEX_SCAN);
    std::cout << "  -> Correctly differentiated per-table strategies" << std::endl;

    std::cout << "[SUCCESS] UCB1Bandit Per-Table Contextual Learning passed!" << std::endl;
}

void TestUCB1BanditExplorationPhase() {
    std::cout << "[TEST] UCB1Bandit Exploration Phase..." << std::endl;

    UCB1Bandit bandit;

    // With fewer than MIN_SAMPLES_BEFORE_LEARNING total pulls, should not recommend
    assert(!bandit.HasSufficientData());
    std::cout << "  -> Not sufficient data at 0 pulls" << std::endl;

    for (size_t i = 0; i < MIN_SAMPLES_BEFORE_LEARNING - 1; i++) {
        bandit.RecordOutcome(
            (i % 2 == 0) ? ScanStrategy::SEQUENTIAL_SCAN : ScanStrategy::INDEX_SCAN,
            "explore_table", 10.0, 100);
    }

    assert(!bandit.HasSufficientData());
    std::cout << "  -> Not sufficient data at " << (MIN_SAMPLES_BEFORE_LEARNING - 1) << " pulls" << std::endl;

    bandit.RecordOutcome(ScanStrategy::INDEX_SCAN, "explore_table", 10.0, 100);
    assert(bandit.HasSufficientData());
    std::cout << "  -> Sufficient data at " << MIN_SAMPLES_BEFORE_LEARNING << " pulls" << std::endl;

    std::cout << "[SUCCESS] UCB1Bandit Exploration Phase passed!" << std::endl;
}

void TestUCB1BanditReset() {
    std::cout << "[TEST] UCB1Bandit Reset..." << std::endl;

    UCB1Bandit bandit;

    for (int i = 0; i < 50; i++) {
        bandit.RecordOutcome(ScanStrategy::SEQUENTIAL_SCAN, "reset_table", 10.0, 100);
    }
    assert(bandit.HasSufficientData());

    bandit.Reset();
    assert(!bandit.HasSufficientData());

    auto stats = bandit.GetStats();
    for (const auto& arm : stats) {
        assert(arm.total_pulls == 0);
    }
    std::cout << "  -> Reset cleared all state" << std::endl;

    std::cout << "[SUCCESS] UCB1Bandit Reset passed!" << std::endl;
}
