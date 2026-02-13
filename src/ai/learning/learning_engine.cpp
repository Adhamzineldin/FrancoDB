#include "ai/learning/learning_engine.h"
#include "ai/metrics_store.h"
#include "common/logger.h"
#include "parser/statement.h"

#include <sstream>

namespace chronosdb {
namespace ai {

LearningEngine::LearningEngine(Catalog* catalog)
    : catalog_(catalog),
      feature_extractor_(std::make_unique<QueryFeatureExtractor>(catalog)),
      bandit_(std::make_unique<UCB1Bandit>()) {}

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
    return oss.str();
}

std::vector<UCB1Bandit::ArmStats> LearningEngine::GetArmStats() const {
    return bandit_->GetStats();
}

uint64_t LearningEngine::GetTotalQueriesObserved() const {
    return total_queries_.load(std::memory_order_relaxed);
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

} // namespace ai
} // namespace chronosdb
