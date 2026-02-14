#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ai/ai_config.h"
#include "ai/learning/query_features.h"

namespace chronosdb {
namespace ai {

enum class ScanStrategy : uint8_t {
    SEQUENTIAL_SCAN = 0,
    INDEX_SCAN = 1
};

/**
 * UCB1Bandit - Upper Confidence Bound algorithm for scan strategy selection.
 *
 * Two arms: SeqScan and IndexScan.
 * Selection: argmax_a [ Q(a) + c * sqrt(ln(N) / N_a) ]
 * Contextual: maintains per-table reward averages for better decisions.
 *
 * Thread-safe via atomic counters and per-table mutex.
 */
class UCB1Bandit {
public:
    UCB1Bandit();

    // Select the best strategy for a query with given features
    ScanStrategy SelectStrategy(const QueryFeatures& features,
                                const std::string& table_name) const;

    // Record the execution outcome for learning
    void RecordOutcome(ScanStrategy strategy, const std::string& table_name,
                       double execution_time_ms, uint32_t rows_scanned);

    // Statistics for SHOW EXECUTION STATS
    struct ArmStats {
        ScanStrategy strategy;
        uint64_t total_pulls;
        double average_reward;
        double ucb_score;
    };
    std::vector<ArmStats> GetStats() const;

    // Has enough data to start recommending?
    bool HasSufficientData() const;

    void Reset();

    // State persistence
    bool SaveState(const std::string& path) const;
    bool LoadState(const std::string& path);

private:
    static constexpr size_t NUM_ARMS = 2;

    struct ArmData {
        std::atomic<uint64_t> pull_count{0};
        std::atomic<uint64_t> total_reward_x10000{0}; // Fixed-point: reward * 10000

        // Per-table contextual tracking
        struct TableStats {
            uint64_t pulls = 0;
            double total_reward = 0.0;
        };
        std::unordered_map<std::string, TableStats> table_stats;
        mutable std::mutex table_mutex;
    };

    std::array<ArmData, NUM_ARMS> arms_;
    std::atomic<uint64_t> total_pulls_{0};

    double ComputeUCBScore(size_t arm_index) const;
    double ComputeTableUCBScore(size_t arm_index,
                                const std::string& table_name) const;
    static double ComputeReward(double execution_time_ms);
    double GetAverageReward(size_t arm_index) const;
};

} // namespace ai
} // namespace chronosdb
