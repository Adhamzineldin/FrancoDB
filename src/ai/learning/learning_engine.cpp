#include "ai/learning/learning_engine.h"
#include "ai/learning/query_plan_optimizer.h"
#include "ai/ai_config.h"
#include "ai/metrics_store.h"
#include "common/logger.h"
#include "parser/statement.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace chronosdb {
namespace ai {

LearningEngine::LearningEngine(Catalog* catalog)
    : catalog_(catalog),
      feature_extractor_(std::make_unique<QueryFeatureExtractor>(catalog)),
      bandit_(std::make_unique<UCB1Bandit>()),
      plan_optimizer_(std::make_unique<QueryPlanOptimizer>(catalog)) {}

LearningEngine::~LearningEngine() {
    Stop();
}

void LearningEngine::OnAfterDML(const DMLEvent& event) {
    if (!active_.load()) return;

    // Only learn from SELECT queries
    if (event.operation != DMLOperation::SELECT) return;

    total_queries_.fetch_add(1, std::memory_order_relaxed);

    // Record outcome for the bandit
    ScanStrategy used = event.used_index_scan
                            ? ScanStrategy::INDEX_SCAN
                            : ScanStrategy::SEQUENTIAL_SCAN;
    double duration_ms = static_cast<double>(event.duration_us) / 1000.0;

    bandit_->RecordOutcome(used, event.table_name, duration_ms,
                           static_cast<uint32_t>(event.result_row_count));

    // Also record in shared metrics store
    MetricEvent metric{};
    metric.type = event.used_index_scan ? MetricType::SCAN_INDEX
                                        : MetricType::SCAN_SEQ;
    metric.timestamp_us = event.start_time_us;
    metric.duration_us = event.duration_us;
    metric.table_name = event.table_name;
    metric.rows_affected = static_cast<uint32_t>(event.result_row_count);
    metric.scan_strategy = event.used_index_scan ? 1 : 0;
    MetricsStore::Instance().Record(metric);
}

bool LearningEngine::RecommendScanStrategy(const SelectStatement* stmt,
                                            const std::string& table_name,
                                            ScanStrategy& out_strategy) {
    if (!active_.load()) return false;
    if (!bandit_->HasSufficientData()) return false;

    QueryFeatures features = feature_extractor_->Extract(stmt, table_name);
    out_strategy = bandit_->SelectStrategy(features, table_name);
    return true;
}

ExecutionPlan LearningEngine::OptimizeQuery(
    const SelectStatement* stmt, const std::string& table_name) const {
    if (!active_.load()) {
        ExecutionPlan default_plan;
        for (size_t i = 0; i < stmt->where_clause_.size(); ++i)
            default_plan.filter_order.push_back(i);
        return default_plan;
    }

    // Get multi-dimensional plan from optimizer
    ExecutionPlan plan = plan_optimizer_->Optimize(stmt, table_name);

    // Fill in scan strategy from the existing bandit (backward compatible)
    if (bandit_->HasSufficientData()) {
        QueryFeatures features = feature_extractor_->Extract(stmt, table_name);
        plan.scan_strategy = bandit_->SelectStrategy(features, table_name);
    }

    return plan;
}

void LearningEngine::RecordExecutionFeedback(const ExecutionFeedback& feedback) {
    if (!active_.load()) return;
    plan_optimizer_->RecordFeedback(feedback);
}

QueryPlanOptimizer* LearningEngine::GetPlanOptimizer() const {
    return plan_optimizer_.get();
}

std::string LearningEngine::GetSummary() const {
    uint64_t queries = total_queries_.load(std::memory_order_relaxed);
    bool ready = bandit_->HasSufficientData();

    std::ostringstream oss;
    oss << queries << " queries observed";
    if (ready) {
        auto stats = bandit_->GetStats();
        oss << ", UCB1 active";
        for (const auto& s : stats) {
            oss << " | "
                << (s.strategy == ScanStrategy::INDEX_SCAN ? "IDX" : "SEQ")
                << ": " << s.total_pulls << " pulls, avg_r="
                << static_cast<int>(s.average_reward * 100) << "%";
        }
    } else {
        oss << ", learning (need " << MIN_SAMPLES_BEFORE_LEARNING << ")";
    }

    // Add optimizer stats
    auto opt_stats = plan_optimizer_->GetStats();
    if (opt_stats.total_optimizations > 0) {
        oss << " | Optimizer: " << opt_stats.filter_reorders << " filter reorders, "
            << opt_stats.early_terminations << " early terminations";
    }
    return oss.str();
}

std::vector<UCB1Bandit::ArmStats> LearningEngine::GetArmStats() const {
    return bandit_->GetStats();
}

uint64_t LearningEngine::GetTotalQueriesObserved() const {
    return total_queries_.load(std::memory_order_relaxed);
}

void LearningEngine::Decay(double decay_factor) {
    if (!active_) return;

    LOG_INFO("LearningEngine", "Applying decay factor " +
             std::to_string(decay_factor) + " to adapt to workload changes");

    // Decay bandit statistics
    bandit_->Decay(decay_factor);

    // Decay query counter proportionally
    uint64_t old_queries = total_queries_.load(std::memory_order_relaxed);
    uint64_t new_queries = static_cast<uint64_t>(old_queries * decay_factor);
    total_queries_.store(new_queries, std::memory_order_relaxed);

    // Decay optimizer statistics as well
    plan_optimizer_->Decay(decay_factor);
}

void LearningEngine::PeriodicMaintenance() {
    if (!active_) return;

    // Apply periodic decay to allow adaptation to changing workloads
    Decay(AI_DECAY_FACTOR);

    LOG_INFO("LearningEngine", "Periodic maintenance complete. Current state: " + GetSummary());
}

void LearningEngine::Start() {
    active_ = true;
    LOG_INFO("LearningEngine", "Self-Learning Execution Engine started "
             "(UCB1 bandit, exploration=" +
             std::to_string(MIN_SAMPLES_BEFORE_LEARNING) + " queries)");
}

void LearningEngine::Stop() {
    active_ = false;
}

bool LearningEngine::SaveState(const std::string& dir) const {
    try {
        std::filesystem::create_directories(dir);
    } catch (...) {
        return false;
    }

    // Save bandit state
    if (!bandit_->SaveState(dir + "/bandit.dat")) return false;

    // Save plan optimizer state
    if (!plan_optimizer_->SaveState(dir + "/optimizer.dat")) return false;

    // Save learning engine metadata
    std::ofstream file(dir + "/learning_engine.dat");
    if (!file.is_open()) return false;

    file << "CHRONOS_LEARNING_V2\n";
    file << total_queries_.load(std::memory_order_relaxed) << "\n";

    return file.good();
}

bool LearningEngine::LoadState(const std::string& dir) {
    // Load bandit state
    if (!bandit_->LoadState(dir + "/bandit.dat")) return false;

    // Load plan optimizer state (optional - may not exist in older saves)
    std::string opt_path = dir + "/optimizer.dat";
    if (std::filesystem::exists(opt_path)) {
        plan_optimizer_->LoadState(opt_path);
    }

    // Load learning engine metadata
    std::ifstream file(dir + "/learning_engine.dat");
    if (!file.is_open()) return false;

    std::string header;
    std::getline(file, header);
    // Accept both V1 and V2 formats
    if (header != "CHRONOS_LEARNING_V1" && header != "CHRONOS_LEARNING_V2") return false;

    uint64_t queries;
    file >> queries;
    total_queries_.store(queries, std::memory_order_relaxed);

    return file.good();
}

} // namespace ai
} // namespace chronosdb
