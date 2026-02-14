#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "ai/dml_observer.h"
#include "ai/learning/bandit.h"
#include "ai/learning/execution_plan.h"
#include "ai/learning/query_features.h"

namespace chronosdb {

class Catalog;
class SelectStatement;

namespace ai {

class QueryPlanOptimizer;

/**
 * IQueryOptimizer - Interface for AI-powered scan strategy recommendation.
 *
 * The DMLExecutor consults this before choosing SeqScan vs IndexScan.
 * Dependency Inversion: DMLExecutor depends on this abstraction, not on LearningEngine.
 */
class IQueryOptimizer {
public:
    virtual ~IQueryOptimizer() = default;

    /**
     * Recommend a scan strategy for a SELECT query.
     * Returns true if AI has a recommendation (fills out_strategy).
     * Returns false if AI has insufficient data (use existing behavior).
     */
    virtual bool RecommendScanStrategy(const SelectStatement* stmt,
                                       const std::string& table_name,
                                       ScanStrategy& out_strategy) = 0;
};

/**
 * LearningEngine - Self-learning query execution optimizer.
 *
 * Implements IDMLObserver (records feedback after each query) and
 * IQueryOptimizer (recommends scan strategies).
 *
 * Uses UCB1 multi-armed bandit to balance exploration vs exploitation.
 * Registered as a DML observer via DMLObserverRegistry.
 */
class LearningEngine : public IDMLObserver, public IQueryOptimizer {
public:
    explicit LearningEngine(Catalog* catalog);
    ~LearningEngine() override;

    // IDMLObserver
    void OnAfterDML(const DMLEvent& event) override;

    // IQueryOptimizer (scan strategy - backward compatible)
    bool RecommendScanStrategy(const SelectStatement* stmt,
                               const std::string& table_name,
                               ScanStrategy& out_strategy) override;

    // Full execution plan optimization (new - multi-dimensional)
    ExecutionPlan OptimizeQuery(const SelectStatement* stmt,
                                const std::string& table_name) const;

    // Record rich execution feedback for learning
    void RecordExecutionFeedback(const ExecutionFeedback& feedback);

    // Get the query plan optimizer for direct access
    QueryPlanOptimizer* GetPlanOptimizer() const;

    // Status for SHOW EXECUTION STATS
    std::string GetSummary() const;
    std::vector<UCB1Bandit::ArmStats> GetArmStats() const;
    uint64_t GetTotalQueriesObserved() const;

    // State persistence
    bool SaveState(const std::string& dir) const;
    bool LoadState(const std::string& dir);

    // Lifecycle
    void Start();
    void Stop();

private:
    Catalog* catalog_;
    std::unique_ptr<QueryFeatureExtractor> feature_extractor_;
    std::unique_ptr<UCB1Bandit> bandit_;
    std::unique_ptr<QueryPlanOptimizer> plan_optimizer_;

    std::atomic<uint64_t> total_queries_{0};
    std::atomic<bool> active_{false};
};

} // namespace ai
} // namespace chronosdb
