#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "ai/immune/anomaly_detector.h"

namespace chronosdb {
namespace ai {

enum class ThreatType : uint8_t {
    NONE = 0,
    SQL_INJECTION = 1,
    XSS = 2
};

struct ThreatResult {
    ThreatType type = ThreatType::NONE;
    AnomalySeverity severity = AnomalySeverity::NONE;
    std::string pattern_matched;
    std::string description;
};

/**
 * ThreatDetector - Content-based security threat detection.
 *
 * Analyzes query text for SQL injection and XSS attack patterns
 * using fast case-insensitive string matching (no regex).
 *
 * Integrates with the Immune System to block malicious queries
 * and record threats as anomalies for dashboard visibility.
 */
class ThreatDetector {
public:
    ThreatDetector();

    // Analyze query text for SQL injection patterns
    ThreatResult DetectSQLInjection(const std::string& query_text) const;

    // Analyze query text for XSS patterns
    ThreatResult DetectXSS(const std::string& query_text) const;

    // Combined analysis - returns highest severity threat found
    ThreatResult Analyze(const std::string& query_text) const;

    // Convert ThreatResult to AnomalyReport for dashboard integration
    static AnomalyReport ToAnomalyReport(const ThreatResult& threat,
                                          const std::string& table_name,
                                          const std::string& user);

    // Stats
    uint64_t GetTotalThreatsDetected() const;
    uint64_t GetSQLInjectionCount() const;
    uint64_t GetXSSCount() const;

    static std::string ThreatTypeToString(ThreatType type);

private:
    // Fast case-insensitive string search
    static std::string ToLower(const std::string& s);
    static bool ContainsPattern(const std::string& lower_text,
                                const std::string& pattern);

    // Mutable because Detect methods update stats but are logically const
    mutable std::atomic<uint64_t> total_threats_{0};
    mutable std::atomic<uint64_t> sql_injection_count_{0};
    mutable std::atomic<uint64_t> xss_count_{0};
};

} // namespace ai
} // namespace chronosdb
