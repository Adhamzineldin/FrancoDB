#include "ai/immune/threat_detector.h"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace chronosdb {
namespace ai {

// Pattern definition: text to search for + severity level
struct ThreatPattern {
    const char* text;
    AnomalySeverity severity;
};

// ========================================================================
// SQL INJECTION PATTERNS (searched against lowercased query text)
// ========================================================================
static const ThreatPattern SQL_INJECTION_PATTERNS[] = {
    // HIGH severity - destructive / data exfiltration
    {"union select",         AnomalySeverity::HIGH},
    {"union all select",     AnomalySeverity::HIGH},
    {"; drop ",              AnomalySeverity::HIGH},
    {"; delete ",            AnomalySeverity::HIGH},
    {"; truncate ",          AnomalySeverity::HIGH},
    {"; update ",            AnomalySeverity::HIGH},
    {"; insert ",            AnomalySeverity::HIGH},
    {"into outfile",         AnomalySeverity::HIGH},
    {"into dumpfile",        AnomalySeverity::HIGH},
    {"load_file(",           AnomalySeverity::HIGH},

    // MEDIUM severity - authentication bypass / timing attacks
    {"or 1=1",              AnomalySeverity::MEDIUM},
    {"or '1'='1",           AnomalySeverity::MEDIUM},
    {"' or '",              AnomalySeverity::MEDIUM},
    {"or true--",           AnomalySeverity::MEDIUM},
    {"or true;",            AnomalySeverity::MEDIUM},
    {"' or true",           AnomalySeverity::MEDIUM},
    {"sleep(",              AnomalySeverity::MEDIUM},
    {"benchmark(",          AnomalySeverity::MEDIUM},
    {"waitfor delay",       AnomalySeverity::MEDIUM},
    {"'; --",               AnomalySeverity::MEDIUM},
    {"' --",                AnomalySeverity::MEDIUM},
    {"'/*",                 AnomalySeverity::MEDIUM},
    {"*/or/*",              AnomalySeverity::MEDIUM},
    {"char(0x",             AnomalySeverity::MEDIUM},
    {"exec(",               AnomalySeverity::MEDIUM},
    {"execute(",            AnomalySeverity::MEDIUM},
    {"xp_cmdshell",         AnomalySeverity::MEDIUM},
    {"information_schema",  AnomalySeverity::MEDIUM},

    // LOW severity - suspicious patterns
    {"' or 1",              AnomalySeverity::LOW},
    {"'a'='a",              AnomalySeverity::LOW},
    {"1' or '1",            AnomalySeverity::LOW},
    {"admin'--",            AnomalySeverity::LOW},
};

static constexpr size_t SQL_INJECTION_PATTERN_COUNT =
    sizeof(SQL_INJECTION_PATTERNS) / sizeof(SQL_INJECTION_PATTERNS[0]);

// ========================================================================
// XSS PATTERNS (searched against lowercased query text)
// ========================================================================
static const ThreatPattern XSS_PATTERNS[] = {
    // HIGH severity - active script execution
    {"<script",             AnomalySeverity::HIGH},
    {"javascript:",         AnomalySeverity::HIGH},
    {"eval(",               AnomalySeverity::HIGH},
    {"document.cookie",     AnomalySeverity::HIGH},
    {"document.write(",     AnomalySeverity::HIGH},
    {"document.location",   AnomalySeverity::HIGH},
    {"window.location",     AnomalySeverity::HIGH},
    {".innerhtml",          AnomalySeverity::HIGH},

    // MEDIUM severity - event handler injection
    {"onerror=",            AnomalySeverity::MEDIUM},
    {"onload=",             AnomalySeverity::MEDIUM},
    {"onclick=",            AnomalySeverity::MEDIUM},
    {"onmouseover=",        AnomalySeverity::MEDIUM},
    {"onfocus=",            AnomalySeverity::MEDIUM},
    {"onsubmit=",           AnomalySeverity::MEDIUM},
    {"<iframe",             AnomalySeverity::MEDIUM},
    {"<object",             AnomalySeverity::MEDIUM},
    {"<embed",              AnomalySeverity::MEDIUM},
    {"<svg onload",         AnomalySeverity::MEDIUM},
    {"<img src=",           AnomalySeverity::MEDIUM},
    {"<body onload",        AnomalySeverity::MEDIUM},

    // LOW severity - potentially dangerous functions
    {"alert(",              AnomalySeverity::LOW},
    {"prompt(",             AnomalySeverity::LOW},
    {"confirm(",            AnomalySeverity::LOW},
    {"<marquee",            AnomalySeverity::LOW},
};

static constexpr size_t XSS_PATTERN_COUNT =
    sizeof(XSS_PATTERNS) / sizeof(XSS_PATTERNS[0]);

// ========================================================================
// Implementation
// ========================================================================

ThreatDetector::ThreatDetector() = default;

std::string ThreatDetector::ToLower(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        result.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

bool ThreatDetector::ContainsPattern(const std::string& lower_text,
                                      const std::string& pattern) {
    return lower_text.find(pattern) != std::string::npos;
}

ThreatResult ThreatDetector::DetectSQLInjection(const std::string& query_text) const {
    if (query_text.empty()) return {};

    std::string lower = ToLower(query_text);
    ThreatResult best;

    for (size_t i = 0; i < SQL_INJECTION_PATTERN_COUNT; ++i) {
        const auto& p = SQL_INJECTION_PATTERNS[i];
        if (ContainsPattern(lower, p.text)) {
            if (static_cast<uint8_t>(p.severity) >
                static_cast<uint8_t>(best.severity)) {
                best.type = ThreatType::SQL_INJECTION;
                best.severity = p.severity;
                best.pattern_matched = p.text;
                best.description = "SQL injection pattern detected: '" +
                    std::string(p.text) + "'";
            }
            // If we found HIGH, no need to keep searching
            if (best.severity == AnomalySeverity::HIGH) break;
        }
    }

    if (best.type != ThreatType::NONE) {
        sql_injection_count_.fetch_add(1, std::memory_order_relaxed);
        total_threats_.fetch_add(1, std::memory_order_relaxed);
    }

    return best;
}

ThreatResult ThreatDetector::DetectXSS(const std::string& query_text) const {
    if (query_text.empty()) return {};

    std::string lower = ToLower(query_text);
    ThreatResult best;

    for (size_t i = 0; i < XSS_PATTERN_COUNT; ++i) {
        const auto& p = XSS_PATTERNS[i];
        if (ContainsPattern(lower, p.text)) {
            if (static_cast<uint8_t>(p.severity) >
                static_cast<uint8_t>(best.severity)) {
                best.type = ThreatType::XSS;
                best.severity = p.severity;
                best.pattern_matched = p.text;
                best.description = "XSS attack pattern detected: '" +
                    std::string(p.text) + "'";
            }
            if (best.severity == AnomalySeverity::HIGH) break;
        }
    }

    if (best.type != ThreatType::NONE) {
        xss_count_.fetch_add(1, std::memory_order_relaxed);
        total_threats_.fetch_add(1, std::memory_order_relaxed);
    }

    return best;
}

ThreatResult ThreatDetector::Analyze(const std::string& query_text) const {
    if (query_text.empty()) return {};

    auto sqli = DetectSQLInjection(query_text);
    auto xss = DetectXSS(query_text);

    // Return the higher-severity threat
    if (static_cast<uint8_t>(sqli.severity) >=
        static_cast<uint8_t>(xss.severity)) {
        return sqli;
    }
    return xss;
}

AnomalyReport ThreatDetector::ToAnomalyReport(
    const ThreatResult& threat,
    const std::string& table_name,
    const std::string& user) {
    AnomalyReport report;
    report.table_name = table_name;
    report.user = user;
    report.severity = threat.severity;
    // Use severity as a pseudo z-score for display
    report.z_score = static_cast<double>(static_cast<uint8_t>(threat.severity)) * 2.0;
    report.current_rate = 0.0;
    report.mean_rate = 0.0;
    report.std_dev = 0.0;
    report.timestamp_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    report.description = "[" + ThreatTypeToString(threat.type) + "] " +
                          threat.description +
                          " (table='" + table_name + "', user='" + user + "')";
    return report;
}

uint64_t ThreatDetector::GetTotalThreatsDetected() const {
    return total_threats_.load(std::memory_order_relaxed);
}

uint64_t ThreatDetector::GetSQLInjectionCount() const {
    return sql_injection_count_.load(std::memory_order_relaxed);
}

uint64_t ThreatDetector::GetXSSCount() const {
    return xss_count_.load(std::memory_order_relaxed);
}

std::string ThreatDetector::ThreatTypeToString(ThreatType type) {
    switch (type) {
        case ThreatType::NONE:          return "NONE";
        case ThreatType::SQL_INJECTION: return "SQL_INJECTION";
        case ThreatType::XSS:           return "XSS";
        default:                         return "UNKNOWN";
    }
}

} // namespace ai
} // namespace chronosdb
