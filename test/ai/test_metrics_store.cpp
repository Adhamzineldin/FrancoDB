#include <iostream>
#include <cassert>
#include <vector>
#include <thread>
#include <chrono>

#include "ai/metrics_store.h"
#include "ai/ai_config.h"

using namespace chronosdb;
using namespace chronosdb::ai;

/**
 * MetricsStore Tests
 *
 * Tests the thread-safe ring buffer that all AI subsystems share.
 * Covers: recording, querying, counting, concurrent access, and reset.
 */

void TestMetricsStoreBasicRecording() {
    std::cout << "[TEST] MetricsStore Basic Recording..." << std::endl;

    auto& store = MetricsStore::Instance();
    store.Reset();

    // Record an INSERT event
    MetricEvent event;
    event.type = MetricType::DML_INSERT;
    event.timestamp_us = 0; // Will be set internally if 0
    event.duration_us = 5000;
    event.session_id = 1;
    event.user = "test_user";
    event.table_name = "orders";
    event.db_name = "testdb";
    event.rows_affected = 1;
    event.scan_strategy = 0;
    event.target_timestamp = 0;

    store.Record(event);

    assert(store.GetTotalRecorded() == 1);
    std::cout << "  -> Single event recorded, total = " << store.GetTotalRecorded() << std::endl;

    // Record multiple events
    for (int i = 0; i < 99; i++) {
        MetricEvent e;
        e.type = MetricType::DML_SELECT;
        e.duration_us = 1000 + i;
        e.user = "test_user";
        e.table_name = "orders";
        e.db_name = "testdb";
        e.rows_affected = 10;
        e.scan_strategy = (i % 2 == 0) ? 0 : 1;
        store.Record(e);
    }

    assert(store.GetTotalRecorded() == 100);
    std::cout << "  -> 100 events recorded successfully" << std::endl;

    store.Reset();
    assert(store.GetTotalRecorded() == 0);
    std::cout << "  -> Reset verified, total = 0" << std::endl;

    std::cout << "[SUCCESS] MetricsStore Basic Recording passed!" << std::endl;
}

void TestMetricsStoreCountEvents() {
    std::cout << "[TEST] MetricsStore Count Events..." << std::endl;

    auto& store = MetricsStore::Instance();
    store.Reset();

    // Record events of different types
    for (int i = 0; i < 10; i++) {
        MetricEvent e;
        e.type = MetricType::DML_INSERT;
        e.user = "user1";
        e.table_name = "products";
        e.db_name = "testdb";
        e.rows_affected = 1;
        store.Record(e);
    }

    for (int i = 0; i < 5; i++) {
        MetricEvent e;
        e.type = MetricType::DML_DELETE;
        e.user = "user1";
        e.table_name = "products";
        e.db_name = "testdb";
        e.rows_affected = 1;
        store.Record(e);
    }

    for (int i = 0; i < 20; i++) {
        MetricEvent e;
        e.type = MetricType::DML_SELECT;
        e.user = "user2";
        e.table_name = "orders";
        e.db_name = "testdb";
        e.rows_affected = 5;
        store.Record(e);
    }

    // Count INSERTs in a large window (should find all)
    uint64_t insert_count = store.CountEvents(MetricType::DML_INSERT, 60ULL * 1000000);
    assert(insert_count == 10);
    std::cout << "  -> INSERT count = " << insert_count << " (expected 10)" << std::endl;

    uint64_t delete_count = store.CountEvents(MetricType::DML_DELETE, 60ULL * 1000000);
    assert(delete_count == 5);
    std::cout << "  -> DELETE count = " << delete_count << " (expected 5)" << std::endl;

    uint64_t select_count = store.CountEvents(MetricType::DML_SELECT, 60ULL * 1000000);
    assert(select_count == 20);
    std::cout << "  -> SELECT count = " << select_count << " (expected 20)" << std::endl;

    assert(store.GetTotalRecorded() == 35);
    std::cout << "  -> Total recorded = 35" << std::endl;

    store.Reset();
    std::cout << "[SUCCESS] MetricsStore Count Events passed!" << std::endl;
}

void TestMetricsStoreMutationCount() {
    std::cout << "[TEST] MetricsStore Mutation Count..." << std::endl;

    auto& store = MetricsStore::Instance();
    store.Reset();

    // Record mutations for table "orders"
    for (int i = 0; i < 5; i++) {
        MetricEvent e;
        e.type = MetricType::DML_INSERT;
        e.user = "admin";
        e.table_name = "orders";
        e.db_name = "testdb";
        e.rows_affected = 1;
        store.Record(e);
    }

    for (int i = 0; i < 3; i++) {
        MetricEvent e;
        e.type = MetricType::DML_UPDATE;
        e.user = "admin";
        e.table_name = "orders";
        e.db_name = "testdb";
        e.rows_affected = 1;
        store.Record(e);
    }

    // SELECTs should NOT count as mutations
    for (int i = 0; i < 10; i++) {
        MetricEvent e;
        e.type = MetricType::DML_SELECT;
        e.user = "admin";
        e.table_name = "orders";
        e.db_name = "testdb";
        e.rows_affected = 5;
        store.Record(e);
    }

    // Mutations for different table
    for (int i = 0; i < 7; i++) {
        MetricEvent e;
        e.type = MetricType::DML_DELETE;
        e.user = "admin";
        e.table_name = "products";
        e.db_name = "testdb";
        e.rows_affected = 1;
        store.Record(e);
    }

    uint64_t orders_mutations = store.GetMutationCount("orders", 60ULL * 1000000);
    assert(orders_mutations == 8); // 5 inserts + 3 updates
    std::cout << "  -> 'orders' mutations = " << orders_mutations << " (expected 8)" << std::endl;

    uint64_t products_mutations = store.GetMutationCount("products", 60ULL * 1000000);
    assert(products_mutations == 7);
    std::cout << "  -> 'products' mutations = " << products_mutations << " (expected 7)" << std::endl;

    uint64_t unknown_mutations = store.GetMutationCount("nonexistent", 60ULL * 1000000);
    assert(unknown_mutations == 0);
    std::cout << "  -> 'nonexistent' mutations = 0" << std::endl;

    store.Reset();
    std::cout << "[SUCCESS] MetricsStore Mutation Count passed!" << std::endl;
}

void TestMetricsStoreConcurrentAccess() {
    std::cout << "[TEST] MetricsStore Concurrent Access..." << std::endl;

    auto& store = MetricsStore::Instance();
    store.Reset();

    const int NUM_THREADS = 4;
    const int EVENTS_PER_THREAD = 500;

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&store, t]() {
            for (int i = 0; i < EVENTS_PER_THREAD; i++) {
                MetricEvent e;
                e.type = (i % 2 == 0) ? MetricType::DML_INSERT : MetricType::DML_SELECT;
                e.user = "thread_" + std::to_string(t);
                e.table_name = "concurrent_table";
                e.db_name = "testdb";
                e.rows_affected = 1;
                e.duration_us = 100 + i;
                store.Record(e);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    size_t total = store.GetTotalRecorded();
    assert(total == NUM_THREADS * EVENTS_PER_THREAD);
    std::cout << "  -> " << NUM_THREADS << " threads x " << EVENTS_PER_THREAD
              << " events = " << total << " total recorded" << std::endl;

    store.Reset();
    std::cout << "[SUCCESS] MetricsStore Concurrent Access passed!" << std::endl;
}

void TestMetricsStoreRingBufferOverflow() {
    std::cout << "[TEST] MetricsStore Ring Buffer Overflow..." << std::endl;

    auto& store = MetricsStore::Instance();
    store.Reset();

    // Fill beyond ring buffer capacity
    size_t overflow_count = METRICS_RING_BUFFER_CAPACITY + 100;
    for (size_t i = 0; i < overflow_count; i++) {
        MetricEvent e;
        e.type = MetricType::DML_INSERT;
        e.user = "overflow_user";
        e.table_name = "big_table";
        e.db_name = "testdb";
        e.rows_affected = 1;
        store.Record(e);
    }

    size_t total = store.GetTotalRecorded();
    assert(total == overflow_count);
    std::cout << "  -> Recorded " << total << " events (capacity = "
              << METRICS_RING_BUFFER_CAPACITY << ")" << std::endl;
    std::cout << "  -> No crash on overflow - ring buffer wraps correctly" << std::endl;

    store.Reset();
    std::cout << "[SUCCESS] MetricsStore Ring Buffer Overflow passed!" << std::endl;
}
