#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <atomic>

#include "ai/dml_observer.h"

using namespace chronosdb;
using namespace chronosdb::ai;

/**
 * DMLObserverRegistry Tests
 *
 * Tests the Observer pattern infrastructure that connects the execution
 * engine to the AI layer. Covers: register/unregister, notification
 * dispatch, blocking via OnBeforeDML, and observer count tracking.
 */

// Test observer that records what it received
class TestObserver : public IDMLObserver {
public:
    std::vector<DMLEvent> before_events;
    std::vector<DMLEvent> after_events;
    bool block_operations = false;

    bool OnBeforeDML(const DMLEvent& event) override {
        before_events.push_back(event);
        return !block_operations; // Return false to block
    }

    void OnAfterDML(const DMLEvent& event) override {
        after_events.push_back(event);
    }
};

// Observer that always blocks
class BlockingObserver : public IDMLObserver {
public:
    std::atomic<int> block_count{0};

    bool OnBeforeDML(const DMLEvent& event) override {
        (void)event;
        block_count++;
        return false; // Always block
    }
};

void TestDMLObserverRegistration() {
    std::cout << "[TEST] DMLObserverRegistry Registration..." << std::endl;

    auto& registry = DMLObserverRegistry::Instance();

    // Clean state
    TestObserver obs1, obs2;

    size_t initial_count = registry.GetObserverCount();
    registry.Register(&obs1);
    assert(registry.GetObserverCount() == initial_count + 1);
    std::cout << "  -> Registered observer 1, count = " << registry.GetObserverCount() << std::endl;

    registry.Register(&obs2);
    assert(registry.GetObserverCount() == initial_count + 2);
    std::cout << "  -> Registered observer 2, count = " << registry.GetObserverCount() << std::endl;

    registry.Unregister(&obs1);
    assert(registry.GetObserverCount() == initial_count + 1);
    std::cout << "  -> Unregistered observer 1, count = " << registry.GetObserverCount() << std::endl;

    registry.Unregister(&obs2);
    assert(registry.GetObserverCount() == initial_count);
    std::cout << "  -> Unregistered observer 2, count = " << registry.GetObserverCount() << std::endl;

    // Unregistering a non-registered observer should be safe
    registry.Unregister(&obs1);
    std::cout << "  -> Double unregister is safe" << std::endl;

    std::cout << "[SUCCESS] DMLObserverRegistry Registration passed!" << std::endl;
}

void TestDMLObserverNotification() {
    std::cout << "[TEST] DMLObserverRegistry Notification..." << std::endl;

    auto& registry = DMLObserverRegistry::Instance();
    TestObserver obs;
    registry.Register(&obs);

    DMLEvent event;
    event.operation = DMLOperation::INSERT;
    event.table_name = "users";
    event.db_name = "testdb";
    event.user = "admin";
    event.session_id = 42;
    event.rows_affected = 5;

    // NotifyBefore should return true (no blocking)
    bool allowed = registry.NotifyBefore(event);
    assert(allowed == true);
    assert(obs.before_events.size() == 1);
    assert(obs.before_events[0].table_name == "users");
    assert(obs.before_events[0].rows_affected == 5);
    std::cout << "  -> NotifyBefore dispatched correctly, allowed = true" << std::endl;

    // NotifyAfter
    event.duration_us = 1500;
    registry.NotifyAfter(event);
    assert(obs.after_events.size() == 1);
    assert(obs.after_events[0].duration_us == 1500);
    std::cout << "  -> NotifyAfter dispatched correctly" << std::endl;

    // Multiple operations
    for (int i = 0; i < 10; i++) {
        DMLEvent e;
        e.operation = DMLOperation::SELECT;
        e.table_name = "products";
        e.db_name = "testdb";
        e.user = "reader";
        registry.NotifyAfter(e);
    }
    assert(obs.after_events.size() == 11);
    std::cout << "  -> 10 more notifications dispatched, total after_events = 11" << std::endl;

    registry.Unregister(&obs);
    std::cout << "[SUCCESS] DMLObserverRegistry Notification passed!" << std::endl;
}

void TestDMLObserverBlocking() {
    std::cout << "[TEST] DMLObserverRegistry Blocking..." << std::endl;

    auto& registry = DMLObserverRegistry::Instance();
    TestObserver normal_obs;
    BlockingObserver blocking_obs;

    registry.Register(&normal_obs);
    registry.Register(&blocking_obs);

    DMLEvent event;
    event.operation = DMLOperation::DELETE_OP;
    event.table_name = "critical_data";
    event.db_name = "production";
    event.user = "suspicious_user";

    // Should return false because BlockingObserver blocks
    bool allowed = registry.NotifyBefore(event);
    assert(allowed == false);
    assert(blocking_obs.block_count == 1);
    std::cout << "  -> Operation blocked by blocking observer" << std::endl;

    // Test with only normal observer
    registry.Unregister(&blocking_obs);
    allowed = registry.NotifyBefore(event);
    assert(allowed == true);
    std::cout << "  -> Without blocking observer, operation is allowed" << std::endl;

    registry.Unregister(&normal_obs);
    std::cout << "[SUCCESS] DMLObserverRegistry Blocking passed!" << std::endl;
}

void TestDMLObserverMultipleObservers() {
    std::cout << "[TEST] DMLObserverRegistry Multiple Observers..." << std::endl;

    auto& registry = DMLObserverRegistry::Instance();
    TestObserver obs1, obs2, obs3;

    registry.Register(&obs1);
    registry.Register(&obs2);
    registry.Register(&obs3);

    DMLEvent event;
    event.operation = DMLOperation::UPDATE;
    event.table_name = "inventory";
    event.db_name = "warehouse";
    event.user = "worker";
    event.rows_affected = 100;

    registry.NotifyAfter(event);

    // All three should have received the event
    assert(obs1.after_events.size() == 1);
    assert(obs2.after_events.size() == 1);
    assert(obs3.after_events.size() == 1);
    assert(obs1.after_events[0].rows_affected == 100);
    assert(obs2.after_events[0].table_name == "inventory");
    assert(obs3.after_events[0].user == "worker");
    std::cout << "  -> All 3 observers received the event" << std::endl;

    registry.Unregister(&obs1);
    registry.Unregister(&obs2);
    registry.Unregister(&obs3);
    std::cout << "[SUCCESS] DMLObserverRegistry Multiple Observers passed!" << std::endl;
}
