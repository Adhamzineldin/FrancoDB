#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ai/ai_config.h"
#include "ai/learning/execution_plan.h"

namespace chronosdb {

class Catalog;
class SelectStatement;

namespace ai {

/**
 * PredicateSelectivity - Learned selectivity statistics for predicates.
 *
 * Tracks the empirical selectivity (fraction of rows that pass) for each
 * predicate operator type on each table+column combination.
 * This enables intelligent filter reordering.
 */
struct PredicateSelectivity {
    uint64_t observations = 0;
    double cumulative_selectivity = 0.0;  // Sum of (rows_passed / rows_total) per observation

    double GetAverageSelectivity() const {
        if (observations == 0) return 0.5; // Unknown: assume 50%
        return cumulative_selectivity / static_cast<double>(observations);
    }
};

/**
 * QueryPlanOptimizer - Multi-dimensional execution plan optimizer.
 *
 * Goes far beyond simple index-vs-sequential selection.
 * Uses separate multi-armed bandits for each decision dimension:
 *
 * 1. Scan Strategy:    Index scan vs Sequential scan (existing UCB1Bandit)
 * 2. Filter Ordering:  Original vs Selectivity-based vs Cost-based
 * 3. Projection:       Late vs Early materialization
 * 4. Limit:            Full scan vs Early termination
 *
 * Each dimension learns independently from execution feedback.
 * Combined, they produce a complete ExecutionPlan for each query.
 *
 * Also maintains a learned selectivity model per table+column+operator,
 * enabling data-driven filter reordering.
 */
class QueryPlanOptimizer {
public:
    explicit QueryPlanOptimizer(Catalog* catalog);

    // Generate an optimized execution plan for a SELECT query
    ExecutionPlan Optimize(const SelectStatement* stmt,
                           const std::string& table_name) const;

    // Record execution feedback to improve future plans
    void RecordFeedback(const ExecutionFeedback& feedback);

    // Get the recommended filter evaluation order based on learned selectivity
    std::vector<size_t> GetOptimalFilterOrder(
        const SelectStatement* stmt,
        const std::string& table_name) const;

    // Check if optimizer has enough data to make recommendations
    bool HasSufficientData() const;

    // Statistics
    struct OptimizerStats {
        uint64_t total_optimizations;
        uint64_t filter_reorders;
        uint64_t early_terminations;
        uint64_t plans_generated;

        // Per-strategy arm stats
        struct DimensionStats {
            std::string dimension_name;
            std::vector<std::pair<std::string, uint64_t>> arm_pulls; // name -> count
        };
        std::vector<DimensionStats> dimensions;
    };
    OptimizerStats GetStats() const;

    // State persistence
    bool SaveState(const std::string& path) const;
    bool LoadState(const std::string& path);

    void Reset();

    // Decay historical data for adaptation to changing workloads
    void Decay(double decay_factor);

private:
    Catalog* catalog_;

    // Multi-armed bandits for each decision dimension
    // Filter strategy: 3 arms (ORIGINAL, SELECTIVITY, COST)
    static constexpr size_t FILTER_ARMS = 3;
    struct FilterArm {
        std::atomic<uint64_t> pull_count{0};
        std::atomic<uint64_t> total_reward_x10000{0};
    };
    std::array<FilterArm, FILTER_ARMS> filter_arms_;
    std::atomic<uint64_t> filter_total_pulls_{0};

    // Limit strategy: 2 arms (FULL_SCAN, EARLY_TERMINATION)
    static constexpr size_t LIMIT_ARMS = 2;
    struct LimitArm {
        std::atomic<uint64_t> pull_count{0};
        std::atomic<uint64_t> total_reward_x10000{0};
    };
    std::array<LimitArm, LIMIT_ARMS> limit_arms_;
    std::atomic<uint64_t> limit_total_pulls_{0};

    // Learned selectivity model: table::column::op -> PredicateSelectivity
    mutable std::mutex selectivity_mutex_;
    std::unordered_map<std::string, PredicateSelectivity> selectivity_model_;

    // Counters
    std::atomic<uint64_t> total_optimizations_{0};
    std::atomic<uint64_t> filter_reorders_{0};
    std::atomic<uint64_t> early_terminations_{0};

    // Helper: make selectivity key from table+column+operator
    static std::string MakeSelectivityKey(const std::string& table,
                                           const std::string& column,
                                           const std::string& op);

    // UCB1 helpers for each dimension
    double ComputeFilterUCB(size_t arm) const;
    double ComputeLimitUCB(size_t arm) const;
    static double ComputeReward(double execution_time_ms);

    // Predicate cost estimation (for COST_ORDER strategy)
    static double EstimatePredicateCost(const std::string& op);
};

} // namespace ai
} // namespace chronosdb
