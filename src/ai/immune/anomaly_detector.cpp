#include "ai/immune/anomaly_detector.h"
#include "ai/immune/mutation_monitor.h"
#include "ai/immune/user_profiler.h"

#include <cmath>
#include <numeric>
#include <sstream>

namespace chronosdb {
namespace ai {

AnomalyDetector::AnomalyDetector() = default;

std::vector<AnomalyReport> AnomalyDetector::Analyze(
    const MutationMonitor& monitor,
    const UserBehaviorProfiler& profiler) const {
    std::vector<AnomalyReport> reports;

    auto tables = monitor.GetMonitoredTables();
    for (const auto& table : tables) {
        double current_rate = monitor.GetMutationRate(table);

        // Get historical rates (last MUTATION_WINDOW_SIZE intervals of RATE_INTERVAL_US each)
        auto historical = monitor.GetHistoricalRates(
            table, MUTATION_WINDOW_SIZE, RATE_INTERVAL_US);

        // =================================================================
        // BASELINE REQUIREMENT: Need sufficient history for meaningful analysis
        // =================================================================
        // New tables or tables with insufficient history should NOT be flagged
        // based on Z-score (which would be meaningless with near-zero baseline).
        // Only flag if the ABSOLUTE rate is extremely high.

        // Count non-zero intervals to see if table has real activity history
        size_t active_intervals = 0;
        double total_historical = 0.0;
        for (double rate : historical) {
            if (rate > 0.001) {  // Non-trivial activity
                active_intervals++;
            }
            total_historical += rate;
        }

        // Need at least 10 intervals with actual activity to establish baseline
        if (active_intervals < 10) {
            // No established baseline - only flag on EXTREME absolute rates
            // (e.g., 50+ mutations per second is clearly abnormal)
            if (current_rate >= ABSOLUTE_RATE_THRESHOLD * 5) {
                AnomalyReport report;
                report.table_name = table;
                report.severity = AnomalySeverity::MEDIUM;  // Not HIGH - no auto-recover without baseline
                report.z_score = current_rate / ABSOLUTE_RATE_THRESHOLD;
                report.current_rate = current_rate;
                report.mean_rate = 0.0;
                report.std_dev = 0.0;
                report.timestamp_us = std::chrono::duration_cast<
                    std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                std::ostringstream desc;
                desc << "Table '" << table << "' extreme mutation rate "
                     << current_rate << "/s (no baseline yet, absolute threshold)";
                report.description = desc.str();
                reports.push_back(report);
            }
            // Skip Z-score analysis for tables without established baseline
            continue;
        }

        // =================================================================
        // Z-SCORE ANALYSIS: Only for tables with established baseline
        // =================================================================
        double z = ComputeZScore(current_rate, historical);
        AnomalySeverity severity = Classify(z);

        if (severity != AnomalySeverity::NONE) {
            // Compute mean and stddev for the report
            double sum = std::accumulate(historical.begin(),
                                          historical.end(), 0.0);
            double mean = sum / static_cast<double>(historical.size());
            double sq_sum = 0.0;
            for (double v : historical) {
                sq_sum += (v - mean) * (v - mean);
            }
            double stddev = std::sqrt(
                sq_sum / static_cast<double>(historical.size()));

            AnomalyReport report;
            report.table_name = table;
            report.severity = severity;
            report.z_score = z;
            report.current_rate = current_rate;
            report.mean_rate = mean;
            report.std_dev = stddev;
            report.timestamp_us = std::chrono::duration_cast<
                std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            std::ostringstream desc;
            desc << "Table '" << table << "' mutation rate "
                 << current_rate << "/s (mean=" << mean
                 << ", z=" << z << ", severity=" << SeverityToString(severity) << ")";
            report.description = desc.str();

            reports.push_back(report);
        }
    }

    return reports;
}

AnomalySeverity AnomalyDetector::Classify(double z_score) {
    double abs_z = std::abs(z_score);
    if (abs_z >= ZSCORE_HIGH_THRESHOLD) return AnomalySeverity::HIGH;
    if (abs_z >= ZSCORE_MEDIUM_THRESHOLD) return AnomalySeverity::MEDIUM;
    if (abs_z >= ZSCORE_LOW_THRESHOLD) return AnomalySeverity::LOW;
    return AnomalySeverity::NONE;
}

std::string AnomalyDetector::SeverityToString(AnomalySeverity severity) {
    switch (severity) {
        case AnomalySeverity::NONE:   return "NONE";
        case AnomalySeverity::LOW:    return "LOW";
        case AnomalySeverity::MEDIUM: return "MEDIUM";
        case AnomalySeverity::HIGH:   return "HIGH";
        default: return "UNKNOWN";
    }
}

std::vector<AnomalyReport> AnomalyDetector::GetRecentAnomalies(
    size_t max_count) const {
    std::lock_guard lock(history_mutex_);
    size_t count = std::min(max_count, anomaly_history_.size());
    // Return most recent first
    std::vector<AnomalyReport> result;
    result.reserve(count);
    auto it = anomaly_history_.rbegin();
    for (size_t i = 0; i < count && it != anomaly_history_.rend(); ++i, ++it) {
        result.push_back(*it);
    }
    return result;
}

void AnomalyDetector::RecordAnomaly(const AnomalyReport& report) {
    std::lock_guard lock(history_mutex_);
    anomaly_history_.push_back(report);
    while (anomaly_history_.size() > MAX_ANOMALY_HISTORY) {
        anomaly_history_.pop_front();
    }
}

size_t AnomalyDetector::GetTotalAnomalies() const {
    std::lock_guard lock(history_mutex_);
    return anomaly_history_.size();
}

double AnomalyDetector::ComputeZScore(
    double current_value, const std::vector<double>& historical_values) {
    if (historical_values.empty()) return 0.0;

    double sum = std::accumulate(historical_values.begin(),
                                  historical_values.end(), 0.0);
    double mean = sum / static_cast<double>(historical_values.size());

    double sq_sum = 0.0;
    for (double v : historical_values) {
        sq_sum += (v - mean) * (v - mean);
    }
    double variance = sq_sum / static_cast<double>(historical_values.size());
    double stddev = std::sqrt(variance);

    // Avoid division by zero when stddev is near zero.
    // When both mean and stddev are tiny, the system is in a quiet/idle state.
    // Only flag as anomalous if the current rate is meaningfully high in
    // absolute terms, not just relatively different from a near-zero baseline.
    if (stddev < 0.01) {
        // With near-zero variance, Z-score is meaningless
        // Require absolute rate > 5/sec AND at least 10x the mean to flag
        if (current_value < 5.0 || (mean > 0.001 && current_value < mean * 10)) {
            return 0.0; // Not a meaningful spike
        }
        // Return a moderate Z-score, not automatic HIGH severity
        return ZSCORE_MEDIUM_THRESHOLD + 0.5;
    }

    return (current_value - mean) / stddev;
}

} // namespace ai
} // namespace chronosdb
