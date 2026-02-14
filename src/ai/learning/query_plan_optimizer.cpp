#include "ai/learning/query_plan_optimizer.h"
#include "catalog/catalog.h"
#include "parser/statement.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>

namespace chronosdb {
namespace ai {

QueryPlanOptimizer::QueryPlanOptimizer(Catalog* catalog)
    : catalog_(catalog) {}

ExecutionPlan QueryPlanOptimizer::Optimize(
    const SelectStatement* stmt, const std::string& table_name) const {
    ExecutionPlan plan;
    plan.ai_generated = false;

    uint64_t total = total_optimizations_.load(std::memory_order_relaxed);

    // Need minimum observations before making recommendations
    if (total < MIN_SAMPLES_BEFORE_LEARNING) {
        // Return default plan with natural filter order
        for (size_t i = 0; i < stmt->where_clause_.size(); ++i) {
            plan.filter_order.push_back(i);
        }
        return plan;
    }

    plan.ai_generated = true;

    // ---- Decision 1: Filter Strategy ----
    if (stmt->where_clause_.size() > 1) {
        // Use UCB1 to pick filter strategy
        double best_score = -std::numeric_limits<double>::infinity();
        size_t best_arm = 0;
        for (size_t i = 0; i < FILTER_ARMS; ++i) {
            // Force exploration of under-sampled arms
            if (filter_arms_[i].pull_count.load(std::memory_order_relaxed) < MIN_ARM_PULLS) {
                best_arm = i;
                break;
            }
            double score = ComputeFilterUCB(i);
            if (score > best_score) {
                best_score = score;
                best_arm = i;
            }
        }
        plan.filter_strategy = static_cast<FilterStrategy>(best_arm);
    } else {
        plan.filter_strategy = FilterStrategy::ORIGINAL_ORDER;
    }

    // Generate filter order based on chosen strategy
    plan.filter_order = GetOptimalFilterOrder(stmt, table_name);

    // Override filter order based on strategy
    if (plan.filter_strategy == FilterStrategy::SELECTIVITY_ORDER &&
        stmt->where_clause_.size() > 1) {
        // Sort by learned selectivity (most selective first = lowest selectivity value)
        std::sort(plan.filter_order.begin(), plan.filter_order.end(),
            [&](size_t a, size_t b) {
                const auto& cond_a = stmt->where_clause_[a];
                const auto& cond_b = stmt->where_clause_[b];
                std::string key_a = MakeSelectivityKey(table_name, cond_a.column, cond_a.op);
                std::string key_b = MakeSelectivityKey(table_name, cond_b.column, cond_b.op);

                double sel_a, sel_b;
                {
                    std::lock_guard lock(selectivity_mutex_);
                    auto it_a = selectivity_model_.find(key_a);
                    auto it_b = selectivity_model_.find(key_b);
                    sel_a = (it_a != selectivity_model_.end())
                                ? it_a->second.GetAverageSelectivity() : 0.5;
                    sel_b = (it_b != selectivity_model_.end())
                                ? it_b->second.GetAverageSelectivity() : 0.5;
                }
                return sel_a < sel_b;  // Lower selectivity = filters more rows = evaluate first
            });
    } else if (plan.filter_strategy == FilterStrategy::COST_ORDER &&
               stmt->where_clause_.size() > 1) {
        // Sort by predicate evaluation cost (cheapest first)
        std::sort(plan.filter_order.begin(), plan.filter_order.end(),
            [&](size_t a, size_t b) {
                return EstimatePredicateCost(stmt->where_clause_[a].op) <
                       EstimatePredicateCost(stmt->where_clause_[b].op);
            });
    }

    // ---- Decision 2: Limit Strategy ----
    if (stmt->limit_ > 0 && stmt->order_by_.empty()) {
        // Only consider early termination when there's LIMIT but no ORDER BY
        // (ORDER BY + LIMIT requires full scan to sort first)
        double best_score = -std::numeric_limits<double>::infinity();
        size_t best_arm = 0;
        for (size_t i = 0; i < LIMIT_ARMS; ++i) {
            if (limit_arms_[i].pull_count.load(std::memory_order_relaxed) < MIN_ARM_PULLS) {
                best_arm = i;
                break;
            }
            double score = ComputeLimitUCB(i);
            if (score > best_score) {
                best_score = score;
                best_arm = i;
            }
        }
        plan.limit_strategy = static_cast<LimitStrategy>(best_arm);
    }

    return plan;
}

void QueryPlanOptimizer::RecordFeedback(const ExecutionFeedback& feedback) {
    total_optimizations_.fetch_add(1, std::memory_order_relaxed);

    double reward = ComputeReward(static_cast<double>(feedback.duration_us) / 1000.0);
    uint64_t reward_fixed = static_cast<uint64_t>(reward * 10000.0);

    // Record filter strategy reward
    if (feedback.where_clause_count > 1) {
        size_t filter_arm = static_cast<size_t>(feedback.plan_used.filter_strategy);
        if (filter_arm < FILTER_ARMS) {
            filter_arms_[filter_arm].pull_count.fetch_add(1, std::memory_order_relaxed);
            filter_arms_[filter_arm].total_reward_x10000.fetch_add(
                reward_fixed, std::memory_order_relaxed);
            filter_total_pulls_.fetch_add(1, std::memory_order_relaxed);

            if (feedback.plan_used.filter_strategy != FilterStrategy::ORIGINAL_ORDER) {
                filter_reorders_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    // Record limit strategy reward
    if (feedback.had_limit && !feedback.had_order_by) {
        size_t limit_arm = static_cast<size_t>(feedback.plan_used.limit_strategy);
        if (limit_arm < LIMIT_ARMS) {
            limit_arms_[limit_arm].pull_count.fetch_add(1, std::memory_order_relaxed);
            limit_arms_[limit_arm].total_reward_x10000.fetch_add(
                reward_fixed, std::memory_order_relaxed);
            limit_total_pulls_.fetch_add(1, std::memory_order_relaxed);

            if (feedback.plan_used.limit_strategy == LimitStrategy::EARLY_TERMINATION) {
                early_terminations_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    // Update selectivity model
    if (feedback.total_rows_scanned > 0 && feedback.where_clause_count > 0) {
        // We only have aggregate selectivity (not per-predicate), so record
        // the overall selectivity against the primary predicate operator
        double overall_selectivity =
            static_cast<double>(feedback.rows_after_filter) /
            static_cast<double>(feedback.total_rows_scanned);

        // For single-predicate queries, we know the exact selectivity
        // For multi-predicate, it's the combined selectivity (still useful)
        std::string key = MakeSelectivityKey(
            feedback.table_name,
            "*",  // Wildcard: table-level selectivity estimate
            std::to_string(feedback.where_clause_count) + "conds");

        std::lock_guard lock(selectivity_mutex_);
        auto& entry = selectivity_model_[key];
        entry.observations++;
        entry.cumulative_selectivity += overall_selectivity;
    }
}

std::vector<size_t> QueryPlanOptimizer::GetOptimalFilterOrder(
    const SelectStatement* stmt, const std::string& table_name) const {
    std::vector<size_t> order;
    order.reserve(stmt->where_clause_.size());
    for (size_t i = 0; i < stmt->where_clause_.size(); ++i) {
        order.push_back(i);
    }
    return order;
}

bool QueryPlanOptimizer::HasSufficientData() const {
    return total_optimizations_.load(std::memory_order_relaxed) >=
           MIN_SAMPLES_BEFORE_LEARNING;
}

QueryPlanOptimizer::OptimizerStats QueryPlanOptimizer::GetStats() const {
    OptimizerStats stats;
    stats.total_optimizations = total_optimizations_.load(std::memory_order_relaxed);
    stats.filter_reorders = filter_reorders_.load(std::memory_order_relaxed);
    stats.early_terminations = early_terminations_.load(std::memory_order_relaxed);
    stats.plans_generated = stats.total_optimizations;

    // Filter dimension stats
    OptimizerStats::DimensionStats filter_dim;
    filter_dim.dimension_name = "Filter Strategy";
    filter_dim.arm_pulls.push_back({"Original Order",
        filter_arms_[0].pull_count.load(std::memory_order_relaxed)});
    filter_dim.arm_pulls.push_back({"Selectivity Order",
        filter_arms_[1].pull_count.load(std::memory_order_relaxed)});
    filter_dim.arm_pulls.push_back({"Cost Order",
        filter_arms_[2].pull_count.load(std::memory_order_relaxed)});
    stats.dimensions.push_back(filter_dim);

    // Limit dimension stats
    OptimizerStats::DimensionStats limit_dim;
    limit_dim.dimension_name = "Limit Strategy";
    limit_dim.arm_pulls.push_back({"Full Scan",
        limit_arms_[0].pull_count.load(std::memory_order_relaxed)});
    limit_dim.arm_pulls.push_back({"Early Termination",
        limit_arms_[1].pull_count.load(std::memory_order_relaxed)});
    stats.dimensions.push_back(limit_dim);

    return stats;
}

void QueryPlanOptimizer::Reset() {
    for (auto& arm : filter_arms_) {
        arm.pull_count.store(0, std::memory_order_relaxed);
        arm.total_reward_x10000.store(0, std::memory_order_relaxed);
    }
    filter_total_pulls_.store(0, std::memory_order_relaxed);

    for (auto& arm : limit_arms_) {
        arm.pull_count.store(0, std::memory_order_relaxed);
        arm.total_reward_x10000.store(0, std::memory_order_relaxed);
    }
    limit_total_pulls_.store(0, std::memory_order_relaxed);

    {
        std::lock_guard lock(selectivity_mutex_);
        selectivity_model_.clear();
    }

    total_optimizations_.store(0, std::memory_order_relaxed);
    filter_reorders_.store(0, std::memory_order_relaxed);
    early_terminations_.store(0, std::memory_order_relaxed);
}

bool QueryPlanOptimizer::SaveState(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "CHRONOS_OPTIMIZER_V1\n";
    file << total_optimizations_.load(std::memory_order_relaxed) << "\n";
    file << filter_reorders_.load(std::memory_order_relaxed) << "\n";
    file << early_terminations_.load(std::memory_order_relaxed) << "\n";

    // Filter arms
    file << filter_total_pulls_.load(std::memory_order_relaxed) << "\n";
    for (size_t i = 0; i < FILTER_ARMS; ++i) {
        file << filter_arms_[i].pull_count.load(std::memory_order_relaxed) << " "
             << filter_arms_[i].total_reward_x10000.load(std::memory_order_relaxed) << "\n";
    }

    // Limit arms
    file << limit_total_pulls_.load(std::memory_order_relaxed) << "\n";
    for (size_t i = 0; i < LIMIT_ARMS; ++i) {
        file << limit_arms_[i].pull_count.load(std::memory_order_relaxed) << " "
             << limit_arms_[i].total_reward_x10000.load(std::memory_order_relaxed) << "\n";
    }

    // Selectivity model
    std::lock_guard lock(selectivity_mutex_);
    file << selectivity_model_.size() << "\n";
    for (const auto& [key, sel] : selectivity_model_) {
        file << key << " " << sel.observations << " " << sel.cumulative_selectivity << "\n";
    }

    return file.good();
}

bool QueryPlanOptimizer::LoadState(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string header;
    std::getline(file, header);
    if (header != "CHRONOS_OPTIMIZER_V1") return false;

    uint64_t total_opts, f_reorders, e_terms;
    file >> total_opts >> f_reorders >> e_terms;
    total_optimizations_.store(total_opts, std::memory_order_relaxed);
    filter_reorders_.store(f_reorders, std::memory_order_relaxed);
    early_terminations_.store(e_terms, std::memory_order_relaxed);

    uint64_t f_total;
    file >> f_total;
    filter_total_pulls_.store(f_total, std::memory_order_relaxed);
    for (size_t i = 0; i < FILTER_ARMS; ++i) {
        uint64_t pulls, reward;
        file >> pulls >> reward;
        filter_arms_[i].pull_count.store(pulls, std::memory_order_relaxed);
        filter_arms_[i].total_reward_x10000.store(reward, std::memory_order_relaxed);
    }

    uint64_t l_total;
    file >> l_total;
    limit_total_pulls_.store(l_total, std::memory_order_relaxed);
    for (size_t i = 0; i < LIMIT_ARMS; ++i) {
        uint64_t pulls, reward;
        file >> pulls >> reward;
        limit_arms_[i].pull_count.store(pulls, std::memory_order_relaxed);
        limit_arms_[i].total_reward_x10000.store(reward, std::memory_order_relaxed);
    }

    size_t sel_count;
    file >> sel_count;
    std::lock_guard lock(selectivity_mutex_);
    selectivity_model_.clear();
    for (size_t i = 0; i < sel_count; ++i) {
        std::string key;
        uint64_t obs;
        double cum_sel;
        file >> key >> obs >> cum_sel;
        selectivity_model_[key] = {obs, cum_sel};
    }

    return file.good();
}

// ---- Private helpers ----

std::string QueryPlanOptimizer::MakeSelectivityKey(
    const std::string& table, const std::string& column, const std::string& op) {
    return table + "::" + column + "::" + op;
}

double QueryPlanOptimizer::ComputeFilterUCB(size_t arm) const {
    uint64_t n_a = filter_arms_[arm].pull_count.load(std::memory_order_relaxed);
    uint64_t n = filter_total_pulls_.load(std::memory_order_relaxed);

    if (n_a == 0) return std::numeric_limits<double>::infinity();

    double reward_sum = static_cast<double>(
        filter_arms_[arm].total_reward_x10000.load(std::memory_order_relaxed)) / 10000.0;
    double q_a = reward_sum / static_cast<double>(n_a);
    double exploration = UCB1_EXPLORATION_CONSTANT *
                         std::sqrt(std::log(static_cast<double>(n)) /
                                   static_cast<double>(n_a));
    return q_a + exploration;
}

double QueryPlanOptimizer::ComputeLimitUCB(size_t arm) const {
    uint64_t n_a = limit_arms_[arm].pull_count.load(std::memory_order_relaxed);
    uint64_t n = limit_total_pulls_.load(std::memory_order_relaxed);

    if (n_a == 0) return std::numeric_limits<double>::infinity();

    double reward_sum = static_cast<double>(
        limit_arms_[arm].total_reward_x10000.load(std::memory_order_relaxed)) / 10000.0;
    double q_a = reward_sum / static_cast<double>(n_a);
    double exploration = UCB1_EXPLORATION_CONSTANT *
                         std::sqrt(std::log(static_cast<double>(n)) /
                                   static_cast<double>(n_a));
    return q_a + exploration;
}

double QueryPlanOptimizer::ComputeReward(double execution_time_ms) {
    return 1.0 / (1.0 + execution_time_ms / REWARD_SCALE_MS);
}

double QueryPlanOptimizer::EstimatePredicateCost(const std::string& op) {
    // Relative cost of evaluating different predicate types
    if (op == "=" || op == "!=" || op == "<>") return 1.0;   // Simple comparison
    if (op == "<" || op == ">" || op == "<=" || op == ">=") return 1.5; // Range comparison
    if (op == "IN") return 3.0;   // Set membership check
    if (op == "LIKE") return 5.0; // Pattern matching (most expensive)
    return 2.0;  // Unknown operator
}

} // namespace ai
} // namespace chronosdb
