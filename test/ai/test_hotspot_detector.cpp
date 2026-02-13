#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>

#include "ai/temporal/hotspot_detector.h"
#include "ai/temporal/access_tracker.h"
#include "ai/ai_config.h"

using namespace chronosdb;
using namespace chronosdb::ai;

/**
 * Hotspot Detector & Temporal Access Tracker Tests
 *
 * Tests DBSCAN clustering for temporal hotspot detection
 * and CUSUM change-point detection for optimal snapshot scheduling.
 * Also tests the TemporalAccessTracker that feeds data to the detector.
 */

static uint64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void TestTemporalAccessTrackerBasic() {
    std::cout << "[TEST] TemporalAccessTracker Basic..." << std::endl;

    TemporalAccessTracker tracker;

    assert(tracker.GetTotalAccessCount() == 0);
    std::cout << "  -> Initially 0 accesses" << std::endl;

    uint64_t now = NowUs();
    uint64_t base_time = 1000000000ULL; // Some past timestamp

    // Record some time-travel accesses
    for (int i = 0; i < 20; i++) {
        TemporalAccessEvent event;
        event.queried_timestamp_us = base_time + i * 1000000ULL; // 1 second apart
        event.query_time_us = now + i * 1000;
        event.table_name = "orders";
        event.db_name = "testdb";
        tracker.RecordAccess(event);
    }

    assert(tracker.GetTotalAccessCount() == 20);
    std::cout << "  -> Recorded 20 accesses, total = " << tracker.GetTotalAccessCount() << std::endl;

    // Get all events
    auto all_events = tracker.GetAllEvents();
    assert(all_events.size() == 20);
    std::cout << "  -> GetAllEvents returns " << all_events.size() << " events" << std::endl;

    std::cout << "[SUCCESS] TemporalAccessTracker Basic passed!" << std::endl;
}

void TestTemporalAccessTrackerHotTimestamps() {
    std::cout << "[TEST] TemporalAccessTracker Hot Timestamps..." << std::endl;

    TemporalAccessTracker tracker;

    uint64_t now = NowUs();
    uint64_t hot_time = 5000000000ULL; // A popular timestamp

    // Query the same timestamp many times (hot)
    for (int i = 0; i < 50; i++) {
        TemporalAccessEvent event;
        event.queried_timestamp_us = hot_time;
        event.query_time_us = now + i * 1000;
        event.table_name = "audit_log";
        event.db_name = "testdb";
        tracker.RecordAccess(event);
    }

    // Query other timestamps less frequently
    for (int i = 0; i < 10; i++) {
        TemporalAccessEvent event;
        event.queried_timestamp_us = hot_time + (i + 1) * 3600000000ULL; // Hours apart
        event.query_time_us = now + (50 + i) * 1000;
        event.table_name = "audit_log";
        event.db_name = "testdb";
        tracker.RecordAccess(event);
    }

    auto hot = tracker.GetHotTimestamps(3);
    assert(!hot.empty());
    assert(hot[0] == hot_time); // Most popular should be first
    std::cout << "  -> Top hot timestamp: " << hot[0] << " (expected " << hot_time << ")" << std::endl;
    std::cout << "  -> Total hot timestamps returned: " << hot.size() << std::endl;

    std::cout << "[SUCCESS] TemporalAccessTracker Hot Timestamps passed!" << std::endl;
}

void TestTemporalAccessTrackerFrequencyHistogram() {
    std::cout << "[TEST] TemporalAccessTracker Frequency Histogram..." << std::endl;

    TemporalAccessTracker tracker;

    uint64_t now = NowUs();
    uint64_t base = 1000000000ULL;
    uint64_t bucket_width = 60000000ULL; // 60 seconds

    // Create events in 3 distinct time buckets
    // Bucket 0: 10 events
    for (int i = 0; i < 10; i++) {
        TemporalAccessEvent event;
        event.queried_timestamp_us = base + i * 1000000ULL;
        event.query_time_us = now + i;
        event.table_name = "data";
        event.db_name = "testdb";
        tracker.RecordAccess(event);
    }

    // Bucket 2 (skip bucket 1): 5 events
    for (int i = 0; i < 5; i++) {
        TemporalAccessEvent event;
        event.queried_timestamp_us = base + 2 * bucket_width + i * 1000000ULL;
        event.query_time_us = now + 10 + i;
        event.table_name = "data";
        event.db_name = "testdb";
        tracker.RecordAccess(event);
    }

    auto histogram = tracker.GetFrequencyHistogram(bucket_width, 100);
    std::cout << "  -> Histogram has " << histogram.size() << " buckets" << std::endl;
    for (const auto& bucket : histogram) {
        if (bucket.access_count > 0) {
            std::cout << "    [" << bucket.start_us << " - " << bucket.end_us
                      << "]: " << bucket.access_count << " accesses" << std::endl;
        }
    }

    std::cout << "[SUCCESS] TemporalAccessTracker Frequency Histogram passed!" << std::endl;
}

void TestHotspotDetectorDBSCAN() {
    std::cout << "[TEST] HotspotDetector DBSCAN Clustering..." << std::endl;

    HotspotDetector detector;

    // Create events with two clear clusters
    std::vector<TemporalAccessEvent> events;
    uint64_t now = NowUs();

    // Cluster 1: timestamps around 1000000000 (tight group, 10 events within 30s)
    for (int i = 0; i < 10; i++) {
        TemporalAccessEvent e;
        e.queried_timestamp_us = 1000000000ULL + i * 3000000ULL; // 3s apart
        e.query_time_us = now + i;
        e.table_name = "orders";
        e.db_name = "testdb";
        events.push_back(e);
    }

    // Cluster 2: timestamps around 5000000000 (tight group, 8 events within 40s)
    for (int i = 0; i < 8; i++) {
        TemporalAccessEvent e;
        e.queried_timestamp_us = 5000000000ULL + i * 5000000ULL; // 5s apart
        e.query_time_us = now + 10 + i;
        e.table_name = "orders";
        e.db_name = "testdb";
        events.push_back(e);
    }

    // Noise: 2 isolated points far from clusters
    {
        TemporalAccessEvent e;
        e.queried_timestamp_us = 9000000000ULL;
        e.query_time_us = now + 20;
        e.table_name = "orders";
        e.db_name = "testdb";
        events.push_back(e);
    }
    {
        TemporalAccessEvent e;
        e.queried_timestamp_us = 9500000000ULL;
        e.query_time_us = now + 21;
        e.table_name = "orders";
        e.db_name = "testdb";
        events.push_back(e);
    }

    auto hotspots = detector.DetectHotspots(events);
    std::cout << "  -> Detected " << hotspots.size() << " hotspots" << std::endl;

    for (size_t i = 0; i < hotspots.size(); i++) {
        std::cout << "    Hotspot " << i << ": center=" << hotspots[i].center_timestamp_us
                  << " range=[" << hotspots[i].range_start_us << ", " << hotspots[i].range_end_us << "]"
                  << " count=" << hotspots[i].access_count
                  << " density=" << hotspots[i].density << std::endl;
    }

    // Should detect exactly 2 clusters (noise points don't form a cluster < minPts)
    assert(hotspots.size() == 2);
    std::cout << "  -> Correctly identified 2 clusters" << std::endl;

    // First cluster should be around 1000000000
    assert(hotspots[0].access_count >= 5); // At least minPts
    assert(hotspots[1].access_count >= 5);
    std::cout << "  -> Both clusters have >= minPts access count" << std::endl;

    std::cout << "[SUCCESS] HotspotDetector DBSCAN Clustering passed!" << std::endl;
}

void TestHotspotDetectorNoHotspots() {
    std::cout << "[TEST] HotspotDetector No Hotspots..." << std::endl;

    HotspotDetector detector;

    // Only isolated points, no clusters
    std::vector<TemporalAccessEvent> events;
    uint64_t now = NowUs();

    for (int i = 0; i < 4; i++) {
        TemporalAccessEvent e;
        e.queried_timestamp_us = i * 1000000000ULL; // Very far apart (1000s)
        e.query_time_us = now + i;
        e.table_name = "sparse_table";
        e.db_name = "testdb";
        events.push_back(e);
    }

    auto hotspots = detector.DetectHotspots(events);
    assert(hotspots.empty());
    std::cout << "  -> No hotspots detected from sparse data (correct)" << std::endl;

    // Empty input
    auto empty_hotspots = detector.DetectHotspots({});
    assert(empty_hotspots.empty());
    std::cout << "  -> Empty input returns no hotspots" << std::endl;

    std::cout << "[SUCCESS] HotspotDetector No Hotspots passed!" << std::endl;
}

void TestHotspotDetectorCUSUM() {
    std::cout << "[TEST] HotspotDetector CUSUM Change-Point Detection..." << std::endl;

    HotspotDetector detector;

    // Simulate mutation rates: stable, then sudden spike
    std::vector<double> rates;
    std::vector<uint64_t> timestamps;
    uint64_t base = 1000000000ULL;

    // 50 intervals of stable rate ~10.0
    for (int i = 0; i < 50; i++) {
        rates.push_back(10.0 + (i % 3) * 0.5); // Small variation
        timestamps.push_back(base + i * 1000000ULL);
    }

    // Sudden spike to 100.0 for 20 intervals
    for (int i = 0; i < 20; i++) {
        rates.push_back(100.0 + i * 2.0);
        timestamps.push_back(base + (50 + i) * 1000000ULL);
    }

    // Return to normal
    for (int i = 0; i < 30; i++) {
        rates.push_back(10.0 + (i % 3) * 0.5);
        timestamps.push_back(base + (70 + i) * 1000000ULL);
    }

    auto change_points = detector.DetectChangePoints(rates, timestamps);
    std::cout << "  -> Detected " << change_points.size() << " change points" << std::endl;

    for (size_t i = 0; i < change_points.size(); i++) {
        std::cout << "    Change point " << i << " at timestamp " << change_points[i] << std::endl;
    }

    // Should detect at least one change point around the spike
    assert(!change_points.empty());
    std::cout << "  -> At least one change point detected at rate transition" << std::endl;

    // Empty input
    auto no_cp = detector.DetectChangePoints({}, {});
    assert(no_cp.empty());
    std::cout << "  -> Empty input returns no change points" << std::endl;

    std::cout << "[SUCCESS] HotspotDetector CUSUM Change-Point Detection passed!" << std::endl;
}

void TestHotspotDetectorSingleCluster() {
    std::cout << "[TEST] HotspotDetector Single Cluster..." << std::endl;

    HotspotDetector detector;

    // All events in one tight cluster
    std::vector<TemporalAccessEvent> events;
    uint64_t now = NowUs();

    for (int i = 0; i < 20; i++) {
        TemporalAccessEvent e;
        e.queried_timestamp_us = 2000000000ULL + i * 2000000ULL; // 2s apart
        e.query_time_us = now + i;
        e.table_name = "hot_table";
        e.db_name = "testdb";
        events.push_back(e);
    }

    auto hotspots = detector.DetectHotspots(events);
    assert(hotspots.size() == 1);
    std::cout << "  -> Single cluster correctly identified" << std::endl;

    assert(hotspots[0].access_count == 20);
    std::cout << "  -> Cluster contains all 20 events" << std::endl;

    // Range should encompass all events
    assert(hotspots[0].range_start_us <= 2000000000ULL);
    assert(hotspots[0].range_end_us >= 2000000000ULL + 19 * 2000000ULL);
    std::cout << "  -> Range correctly spans all events" << std::endl;

    std::cout << "[SUCCESS] HotspotDetector Single Cluster passed!" << std::endl;
}
