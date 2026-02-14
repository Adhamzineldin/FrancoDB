#include "ai/learning/bandit.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

namespace chronosdb {
namespace ai {

UCB1Bandit::UCB1Bandit() = default;

ScanStrategy UCB1Bandit::SelectStrategy(const QueryFeatures& features,
                                         const std::string& table_name) const {
    uint64_t total = total_pulls_.load(std::memory_order_relaxed);

    // Exploration phase: not enough data yet
    if (total < MIN_SAMPLES_BEFORE_LEARNING) {
        return ScanStrategy::SEQUENTIAL_SCAN; // Default behavior
    }

    // Force exploration of under-sampled arms
    for (size_t i = 0; i < NUM_ARMS; ++i) {
        if (arms_[i].pull_count.load(std::memory_order_relaxed) < MIN_ARM_PULLS) {
            return static_cast<ScanStrategy>(i);
        }
    }

    // If no index available, always use sequential scan
    if (features.has_index_available < 0.5) {
        return ScanStrategy::SEQUENTIAL_SCAN;
    }

    // Try table-specific scores first (if sufficient data)
    bool has_table_context = true;
    for (size_t i = 0; i < NUM_ARMS; ++i) {
        std::lock_guard lock(arms_[i].table_mutex);
        auto it = arms_[i].table_stats.find(table_name);
        if (it == arms_[i].table_stats.end() ||
            it->second.pulls < MIN_TABLE_PULLS_FOR_CONTEXT) {
            has_table_context = false;
            break;
        }
    }

    // UCB1 selection: argmax_a [ Q(a) + c * sqrt(ln(N) / N_a) ]
    double best_score = -std::numeric_limits<double>::infinity();
    size_t best_arm = 0;

    for (size_t i = 0; i < NUM_ARMS; ++i) {
        double score = has_table_context
                           ? ComputeTableUCBScore(i, table_name)
                           : ComputeUCBScore(i);
        if (score > best_score) {
            best_score = score;
            best_arm = i;
        }
    }

    return static_cast<ScanStrategy>(best_arm);
}

void UCB1Bandit::RecordOutcome(ScanStrategy strategy,
                                const std::string& table_name,
                                double execution_time_ms,
                                uint32_t rows_scanned) {
    (void)rows_scanned; // Reserved for future weighted reward
    size_t arm = static_cast<size_t>(strategy);
    if (arm >= NUM_ARMS) return;

    double reward = ComputeReward(execution_time_ms);
    uint64_t reward_fixed = static_cast<uint64_t>(reward * 10000.0);

    arms_[arm].pull_count.fetch_add(1, std::memory_order_relaxed);
    arms_[arm].total_reward_x10000.fetch_add(reward_fixed,
                                              std::memory_order_relaxed);
    total_pulls_.fetch_add(1, std::memory_order_relaxed);

    // Update per-table stats
    {
        std::lock_guard lock(arms_[arm].table_mutex);
        auto& ts = arms_[arm].table_stats[table_name];
        ts.pulls++;
        ts.total_reward += reward;
    }
}

std::vector<UCB1Bandit::ArmStats> UCB1Bandit::GetStats() const {
    std::vector<ArmStats> result;
    result.reserve(NUM_ARMS);
    for (size_t i = 0; i < NUM_ARMS; ++i) {
        ArmStats s;
        s.strategy = static_cast<ScanStrategy>(i);
        s.total_pulls = arms_[i].pull_count.load(std::memory_order_relaxed);
        s.average_reward = GetAverageReward(i);
        s.ucb_score = ComputeUCBScore(i);
        result.push_back(s);
    }
    return result;
}

bool UCB1Bandit::HasSufficientData() const {
    return total_pulls_.load(std::memory_order_relaxed) >=
           MIN_SAMPLES_BEFORE_LEARNING;
}

void UCB1Bandit::Reset() {
    for (auto& arm : arms_) {
        arm.pull_count.store(0, std::memory_order_relaxed);
        arm.total_reward_x10000.store(0, std::memory_order_relaxed);
        std::lock_guard lock(arm.table_mutex);
        arm.table_stats.clear();
    }
    total_pulls_.store(0, std::memory_order_relaxed);
}

double UCB1Bandit::ComputeUCBScore(size_t arm_index) const {
    uint64_t n_a = arms_[arm_index].pull_count.load(std::memory_order_relaxed);
    uint64_t n = total_pulls_.load(std::memory_order_relaxed);

    if (n_a == 0) return std::numeric_limits<double>::infinity();

    double q_a = GetAverageReward(arm_index);
    double exploration = UCB1_EXPLORATION_CONSTANT *
                         std::sqrt(std::log(static_cast<double>(n)) /
                                   static_cast<double>(n_a));
    return q_a + exploration;
}

double UCB1Bandit::ComputeTableUCBScore(size_t arm_index,
                                          const std::string& table_name) const {
    std::lock_guard lock(arms_[arm_index].table_mutex);
    auto it = arms_[arm_index].table_stats.find(table_name);
    if (it == arms_[arm_index].table_stats.end() || it->second.pulls == 0) {
        return std::numeric_limits<double>::infinity();
    }

    uint64_t n = total_pulls_.load(std::memory_order_relaxed);
    double q_a = it->second.total_reward / static_cast<double>(it->second.pulls);
    double exploration = UCB1_EXPLORATION_CONSTANT *
                         std::sqrt(std::log(static_cast<double>(n)) /
                                   static_cast<double>(it->second.pulls));
    return q_a + exploration;
}

double UCB1Bandit::ComputeReward(double execution_time_ms) {
    // Normalize to (0, 1]: faster queries get higher rewards
    return 1.0 / (1.0 + execution_time_ms / REWARD_SCALE_MS);
}

double UCB1Bandit::GetAverageReward(size_t arm_index) const {
    uint64_t pulls = arms_[arm_index].pull_count.load(std::memory_order_relaxed);
    if (pulls == 0) return 0.0;
    uint64_t total_reward =
        arms_[arm_index].total_reward_x10000.load(std::memory_order_relaxed);
    return (static_cast<double>(total_reward) / 10000.0) /
           static_cast<double>(pulls);
}

bool UCB1Bandit::SaveState(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    // Header
    file << "CHRONOS_BANDIT_V1\n";
    file << total_pulls_.load(std::memory_order_relaxed) << "\n";
    file << NUM_ARMS << "\n";

    // Per-arm global stats
    for (size_t i = 0; i < NUM_ARMS; ++i) {
        uint64_t pulls = arms_[i].pull_count.load(std::memory_order_relaxed);
        uint64_t reward = arms_[i].total_reward_x10000.load(std::memory_order_relaxed);
        file << pulls << " " << reward << "\n";

        // Per-table stats for this arm
        std::lock_guard lock(arms_[i].table_mutex);
        file << arms_[i].table_stats.size() << "\n";
        for (const auto& [table, stats] : arms_[i].table_stats) {
            file << table << " " << stats.pulls << " " << stats.total_reward << "\n";
        }
    }

    return file.good();
}

bool UCB1Bandit::LoadState(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string header;
    std::getline(file, header);
    if (header != "CHRONOS_BANDIT_V1") return false;

    uint64_t total_pulls;
    size_t num_arms;
    file >> total_pulls >> num_arms;
    if (num_arms != NUM_ARMS) return false;

    total_pulls_.store(total_pulls, std::memory_order_relaxed);

    for (size_t i = 0; i < NUM_ARMS; ++i) {
        uint64_t pulls, reward;
        file >> pulls >> reward;
        arms_[i].pull_count.store(pulls, std::memory_order_relaxed);
        arms_[i].total_reward_x10000.store(reward, std::memory_order_relaxed);

        size_t table_count;
        file >> table_count;

        std::lock_guard lock(arms_[i].table_mutex);
        arms_[i].table_stats.clear();
        for (size_t t = 0; t < table_count; ++t) {
            std::string table_name;
            uint64_t t_pulls;
            double t_reward;
            file >> table_name >> t_pulls >> t_reward;
            arms_[i].table_stats[table_name] = {t_pulls, t_reward};
        }
    }

    return file.good();
}

} // namespace ai
} // namespace chronosdb
