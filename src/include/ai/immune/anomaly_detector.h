#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "ai/ai_config.h"

namespace chronosdb {
namespace ai {

class MutationMonitor;
class UserBehaviorProfiler;

enum class AnomalySeverity : uint8_t {
    NONE = 0,
    LOW = 1,     // z >= 2.0 -- Log warning
    MEDIUM = 2,  // z >= 3.0 -- Block table mutations
    HIGH = 3     // z >= 4.0 -- Auto-recover via TimeTravelEngine
};

struct AnomalyReport {
    std::string table_name;
    std::string user;
    AnomalySeverity severity;
    double z_score;
    double current_rate;
    double mean_rate;
    double std_dev;
    uint64_t timestamp_us;
    std::string description;
};

/**
 * AnomalyDetector - Z-score based anomaly detection on mutation rates.
 *
 * Analyzes per-table mutation rates against historical baselines.
 * Classifies anomalies by severity: NONE, LOW, MEDIUM, HIGH.
 */
class AnomalyDetector {
public:
    AnomalyDetector();

    // Analyze current mutation rates and detect anomalies
    std::vector<AnomalyReport> Analyze(const MutationMonitor& monitor,
                                        const UserBehaviorProfiler& profiler) const;

    // Classify a z-score into severity
    static AnomalySeverity Classify(double z_score);
    static std::string SeverityToString(AnomalySeverity severity);

    // Recent anomaly history for SHOW ANOMALIES
    std::vector<AnomalyReport> GetRecentAnomalies(size_t max_count = 50) const;

    // Record an anomaly
    void RecordAnomaly(const AnomalyReport& report);

    size_t GetTotalAnomalies() const;

private:
    // Z-score: (x - mu) / sigma
    static double ComputeZScore(double current_value,
                                const std::vector<double>& historical_values);

    mutable std::mutex history_mutex_;
    std::deque<AnomalyReport> anomaly_history_;
};

} // namespace ai
} // namespace chronosdb
