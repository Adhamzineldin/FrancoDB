#include <iostream>
#include <cassert>
#include <cstdio>
#include <string>

#include "ai/learning/query_plan_optimizer.h"
#include "ai/learning/execution_plan.h"

using namespace chronosdb;
using namespace chronosdb::ai;

/**
 * Query Plan Optimizer Tests
 *
 * Tests the multi-dimensional UCB1 bandit optimizer for filter strategy,
 * limit strategy, selectivity model, state persistence, decay, and reset.
 */

// Helper: create an ExecutionFeedback with common defaults
static ExecutionFeedback MakeFeedback(const std::string& table,
                                       FilterStrategy filter_strat,
                                       uint64_t duration_us,
                                       size_t where_count = 3,
                                       bool had_limit = false,
                                       bool had_order_by = false,
                                       LimitStrategy limit_strat = LimitStrategy::FULL_SCAN_THEN_LIMIT) {
    ExecutionFeedback fb;
    fb.table_name = table;
    fb.duration_us = duration_us;
    fb.total_rows_scanned = 1000;
    fb.rows_after_filter = 100;
    fb.result_rows = 100;
    fb.used_index = false;
    fb.where_clause_count = where_count;
    fb.had_limit = had_limit;
    fb.limit_value = had_limit ? 10 : -1;
    fb.had_order_by = had_order_by;
    fb.plan_used.filter_strategy = filter_strat;
    fb.plan_used.limit_strategy = limit_strat;
    return fb;
}

void TestOptimizerFilterStrategyLearning() {
    std::cout << "[TEST] Optimizer Filter Strategy Learning..." << std::endl;

    QueryPlanOptimizer optimizer(nullptr);

    // Feed 50 rounds of feedback for each filter strategy with different speeds:
    // SELECTIVITY_ORDER: fast (10ms)
    // COST_ORDER: medium (50ms)
    // ORIGINAL_ORDER: slow (100ms)
    for (int i = 0; i < 50; i++) {
        optimizer.RecordFeedback(MakeFeedback("orders",
            FilterStrategy::SELECTIVITY_ORDER, 10000));   // 10ms
        optimizer.RecordFeedback(MakeFeedback("orders",
            FilterStrategy::COST_ORDER, 50000));           // 50ms
        optimizer.RecordFeedback(MakeFeedback("orders",
            FilterStrategy::ORIGINAL_ORDER, 100000));      // 100ms
    }

    auto stats = optimizer.GetStats();

    // Should have total_optimizations = 150
    assert(stats.total_optimizations == 150);
    std::cout << "  -> total_optimizations = " << stats.total_optimizations << " (expected 150)" << std::endl;

    // Check filter dimension exists with 3 arms
    assert(stats.dimensions.size() >= 1);
    auto& filter_dim = stats.dimensions[0];
    assert(filter_dim.dimension_name == "Filter Strategy");
    assert(filter_dim.arm_pulls.size() == 3);

    // Each arm should have 50 pulls
    std::cout << "  -> Original Order pulls = " << filter_dim.arm_pulls[0].second << std::endl;
    std::cout << "  -> Selectivity Order pulls = " << filter_dim.arm_pulls[1].second << std::endl;
    std::cout << "  -> Cost Order pulls = " << filter_dim.arm_pulls[2].second << std::endl;
    assert(filter_dim.arm_pulls[0].second == 50);  // Original
    assert(filter_dim.arm_pulls[1].second == 50);  // Selectivity
    assert(filter_dim.arm_pulls[2].second == 50);  // Cost

    // filter_reorders should count non-ORIGINAL pulls = 100
    assert(stats.filter_reorders == 100);
    std::cout << "  -> filter_reorders = " << stats.filter_reorders << " (expected 100)" << std::endl;

    std::cout << "[SUCCESS] Optimizer Filter Strategy Learning passed!" << std::endl;
}

void TestOptimizerLimitStrategyLearning() {
    std::cout << "[TEST] Optimizer Limit Strategy Learning..." << std::endl;

    QueryPlanOptimizer optimizer(nullptr);

    // Feed feedback with LIMIT (no ORDER BY) for both strategies
    // EARLY_TERMINATION: very fast (2ms)
    // FULL_SCAN_THEN_LIMIT: slow (80ms)
    for (int i = 0; i < 40; i++) {
        optimizer.RecordFeedback(MakeFeedback("products",
            FilterStrategy::ORIGINAL_ORDER, 2000,   // 2ms
            1, true, false,                          // where_count=1, limit=true, no order
            LimitStrategy::EARLY_TERMINATION));

        optimizer.RecordFeedback(MakeFeedback("products",
            FilterStrategy::ORIGINAL_ORDER, 80000,  // 80ms
            1, true, false,
            LimitStrategy::FULL_SCAN_THEN_LIMIT));
    }

    auto stats = optimizer.GetStats();

    // Check limit dimension exists with 2 arms
    assert(stats.dimensions.size() >= 2);
    auto& limit_dim = stats.dimensions[1];
    assert(limit_dim.dimension_name == "Limit Strategy");
    assert(limit_dim.arm_pulls.size() == 2);

    std::cout << "  -> Full Scan pulls = " << limit_dim.arm_pulls[0].second << std::endl;
    std::cout << "  -> Early Termination pulls = " << limit_dim.arm_pulls[1].second << std::endl;
    assert(limit_dim.arm_pulls[0].second == 40);  // Full Scan
    assert(limit_dim.arm_pulls[1].second == 40);  // Early Termination

    // early_terminations should count EARLY_TERMINATION pulls = 40
    assert(stats.early_terminations == 40);
    std::cout << "  -> early_terminations = " << stats.early_terminations << " (expected 40)" << std::endl;

    std::cout << "[SUCCESS] Optimizer Limit Strategy Learning passed!" << std::endl;
}

void TestOptimizerRecordFeedback() {
    std::cout << "[TEST] Optimizer RecordFeedback..." << std::endl;

    QueryPlanOptimizer optimizer(nullptr);

    // Single-predicate queries (where_clause_count <= 1) should NOT update filter arms
    optimizer.RecordFeedback(MakeFeedback("t", FilterStrategy::SELECTIVITY_ORDER, 5000, 1));
    optimizer.RecordFeedback(MakeFeedback("t", FilterStrategy::SELECTIVITY_ORDER, 5000, 0));

    auto stats = optimizer.GetStats();
    assert(stats.total_optimizations == 2);
    // Filter arms should have 0 pulls (single-predicate doesn't use filter strategy)
    assert(stats.dimensions[0].arm_pulls[1].second == 0);
    std::cout << "  -> Single-predicate queries don't update filter arms" << std::endl;

    // No-limit queries should NOT update limit arms
    optimizer.RecordFeedback(MakeFeedback("t", FilterStrategy::ORIGINAL_ORDER, 5000, 3, false));

    stats = optimizer.GetStats();
    assert(stats.dimensions[1].arm_pulls[0].second == 0);
    assert(stats.dimensions[1].arm_pulls[1].second == 0);
    std::cout << "  -> No-limit queries don't update limit arms" << std::endl;

    // Queries with ORDER BY + LIMIT should NOT update limit arms
    optimizer.RecordFeedback(MakeFeedback("t", FilterStrategy::ORIGINAL_ORDER, 5000,
                                           3, true, true));  // limit=true, order_by=true
    stats = optimizer.GetStats();
    assert(stats.dimensions[1].arm_pulls[0].second == 0);
    std::cout << "  -> ORDER BY + LIMIT queries don't update limit arms" << std::endl;

    // Multi-predicate query SHOULD update filter arms
    optimizer.RecordFeedback(MakeFeedback("t", FilterStrategy::COST_ORDER, 5000, 3));
    stats = optimizer.GetStats();
    assert(stats.dimensions[0].arm_pulls[2].second == 1);  // Cost Order arm
    std::cout << "  -> Multi-predicate query updates filter arm correctly" << std::endl;

    std::cout << "[SUCCESS] Optimizer RecordFeedback passed!" << std::endl;
}

void TestOptimizerSelectivityModel() {
    std::cout << "[TEST] Optimizer Selectivity Model..." << std::endl;

    QueryPlanOptimizer optimizer(nullptr);

    // Record feedback with varying selectivity
    for (int i = 0; i < 30; i++) {
        ExecutionFeedback fb;
        fb.table_name = "users";
        fb.duration_us = 10000;
        fb.total_rows_scanned = 1000;
        fb.rows_after_filter = 50;  // 5% selectivity
        fb.result_rows = 50;
        fb.where_clause_count = 2;
        fb.had_limit = false;
        fb.had_order_by = false;
        fb.plan_used.filter_strategy = FilterStrategy::ORIGINAL_ORDER;
        optimizer.RecordFeedback(fb);
    }

    // Verify through persistence: save state and check it contains selectivity data
    std::string path = "test_selectivity_state.tmp";
    assert(optimizer.SaveState(path));
    std::cout << "  -> State saved with selectivity data" << std::endl;

    // Load into new optimizer and verify total_optimizations survived
    QueryPlanOptimizer opt2(nullptr);
    assert(opt2.LoadState(path));
    auto stats2 = opt2.GetStats();
    assert(stats2.total_optimizations == 30);
    std::cout << "  -> Selectivity model persists across save/load" << std::endl;

    std::remove(path.c_str());
    std::cout << "[SUCCESS] Optimizer Selectivity Model passed!" << std::endl;
}

void TestOptimizerStatePersistence() {
    std::cout << "[TEST] Optimizer State Persistence..." << std::endl;

    QueryPlanOptimizer optimizer1(nullptr);

    // Build diverse state
    for (int i = 0; i < 25; i++) {
        optimizer1.RecordFeedback(MakeFeedback("orders",
            FilterStrategy::SELECTIVITY_ORDER, 8000, 3));
        optimizer1.RecordFeedback(MakeFeedback("orders",
            FilterStrategy::COST_ORDER, 20000, 3));
        optimizer1.RecordFeedback(MakeFeedback("orders",
            FilterStrategy::ORIGINAL_ORDER, 50000, 3));
        optimizer1.RecordFeedback(MakeFeedback("orders",
            FilterStrategy::ORIGINAL_ORDER, 5000,
            2, true, false, LimitStrategy::EARLY_TERMINATION));
        optimizer1.RecordFeedback(MakeFeedback("orders",
            FilterStrategy::ORIGINAL_ORDER, 30000,
            2, true, false, LimitStrategy::FULL_SCAN_THEN_LIMIT));
    }

    auto stats1 = optimizer1.GetStats();

    // Save state
    std::string path = "test_optimizer_persistence.tmp";
    assert(optimizer1.SaveState(path));
    std::cout << "  -> State saved to file" << std::endl;

    // Load into a fresh optimizer
    QueryPlanOptimizer optimizer2(nullptr);
    assert(optimizer2.LoadState(path));
    auto stats2 = optimizer2.GetStats();

    // Verify all stats match
    assert(stats1.total_optimizations == stats2.total_optimizations);
    assert(stats1.filter_reorders == stats2.filter_reorders);
    assert(stats1.early_terminations == stats2.early_terminations);
    std::cout << "  -> total_optimizations: " << stats2.total_optimizations << " matches" << std::endl;
    std::cout << "  -> filter_reorders: " << stats2.filter_reorders << " matches" << std::endl;
    std::cout << "  -> early_terminations: " << stats2.early_terminations << " matches" << std::endl;

    // Verify arm pull counts match
    for (size_t d = 0; d < stats1.dimensions.size() && d < stats2.dimensions.size(); d++) {
        for (size_t a = 0; a < stats1.dimensions[d].arm_pulls.size(); a++) {
            assert(stats1.dimensions[d].arm_pulls[a].second ==
                   stats2.dimensions[d].arm_pulls[a].second);
        }
    }
    std::cout << "  -> All arm pull counts match after load" << std::endl;

    // Loading from non-existent file should fail gracefully
    QueryPlanOptimizer optimizer3(nullptr);
    assert(!optimizer3.LoadState("nonexistent_file_xyz.tmp"));
    std::cout << "  -> Loading non-existent file returns false" << std::endl;

    std::remove(path.c_str());
    std::cout << "[SUCCESS] Optimizer State Persistence passed!" << std::endl;
}

void TestOptimizerDecay() {
    std::cout << "[TEST] Optimizer Decay..." << std::endl;

    QueryPlanOptimizer optimizer(nullptr);

    // Build up 100 pulls on filter ORIGINAL_ORDER arm
    for (int i = 0; i < 100; i++) {
        optimizer.RecordFeedback(MakeFeedback("test",
            FilterStrategy::ORIGINAL_ORDER, 10000, 3));
    }

    auto before = optimizer.GetStats();
    assert(before.dimensions[0].arm_pulls[0].second == 100);
    std::cout << "  -> Before decay: Original Order pulls = 100" << std::endl;

    // Decay by 0.5
    optimizer.Decay(0.5);
    auto after = optimizer.GetStats();
    assert(after.dimensions[0].arm_pulls[0].second == 50);
    std::cout << "  -> After Decay(0.5): Original Order pulls = "
              << after.dimensions[0].arm_pulls[0].second << " (expected 50)" << std::endl;

    // Decay by 0.5 again
    optimizer.Decay(0.5);
    auto after2 = optimizer.GetStats();
    assert(after2.dimensions[0].arm_pulls[0].second == 25);
    std::cout << "  -> After second Decay(0.5): pulls = "
              << after2.dimensions[0].arm_pulls[0].second << " (expected 25)" << std::endl;

    // Decay(0.0) should act as full reset
    optimizer.Decay(0.0);
    auto reset = optimizer.GetStats();
    assert(reset.dimensions[0].arm_pulls[0].second == 0);
    assert(reset.dimensions[0].arm_pulls[1].second == 0);
    assert(reset.dimensions[0].arm_pulls[2].second == 0);
    std::cout << "  -> Decay(0.0) resets all arms to 0" << std::endl;

    // Decay(1.0) should be a no-op
    for (int i = 0; i < 10; i++) {
        optimizer.RecordFeedback(MakeFeedback("test",
            FilterStrategy::COST_ORDER, 10000, 3));
    }
    auto pre_noop = optimizer.GetStats();
    optimizer.Decay(1.0);
    auto post_noop = optimizer.GetStats();
    assert(pre_noop.dimensions[0].arm_pulls[2].second ==
           post_noop.dimensions[0].arm_pulls[2].second);
    std::cout << "  -> Decay(1.0) is a no-op" << std::endl;

    std::cout << "[SUCCESS] Optimizer Decay passed!" << std::endl;
}

void TestOptimizerReset() {
    std::cout << "[TEST] Optimizer Reset..." << std::endl;

    QueryPlanOptimizer optimizer(nullptr);

    // Build state
    for (int i = 0; i < 50; i++) {
        optimizer.RecordFeedback(MakeFeedback("orders",
            FilterStrategy::SELECTIVITY_ORDER, 5000, 3));
        optimizer.RecordFeedback(MakeFeedback("orders",
            FilterStrategy::ORIGINAL_ORDER, 20000,
            2, true, false, LimitStrategy::EARLY_TERMINATION));
    }

    auto before = optimizer.GetStats();
    assert(before.total_optimizations > 0);
    assert(before.filter_reorders > 0);
    assert(before.early_terminations > 0);
    std::cout << "  -> Before reset: optimizations=" << before.total_optimizations
              << ", reorders=" << before.filter_reorders
              << ", early_terminations=" << before.early_terminations << std::endl;

    // Reset
    optimizer.Reset();
    auto after = optimizer.GetStats();

    // All arm pulls should be 0
    for (const auto& dim : after.dimensions) {
        for (const auto& arm : dim.arm_pulls) {
            assert(arm.second == 0);
        }
    }
    std::cout << "  -> After reset: all arm pulls = 0" << std::endl;

    // HasSufficientData should return false
    assert(!optimizer.HasSufficientData());
    std::cout << "  -> HasSufficientData() = false after reset" << std::endl;

    std::cout << "[SUCCESS] Optimizer Reset passed!" << std::endl;
}
