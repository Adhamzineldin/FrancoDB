#include <iostream>
#include <cassert>
#include <atomic>
#include <chrono>
#include <thread>

#include "ai/ai_scheduler.h"

using namespace chronosdb;
using namespace chronosdb::ai;

/**
 * AIScheduler Tests
 *
 * Tests the background task scheduler that drives periodic analysis
 * for all AI subsystems. Covers: lifecycle, periodic tasks, one-shot
 * tasks, cancellation, and task listing.
 */

void TestAISchedulerLifecycle() {
    std::cout << "[TEST] AIScheduler Lifecycle..." << std::endl;

    auto& scheduler = AIScheduler::Instance();

    // Should be stopped initially or from previous tests
    scheduler.Stop();
    assert(!scheduler.IsRunning());
    std::cout << "  -> Scheduler is stopped" << std::endl;

    scheduler.Start();
    assert(scheduler.IsRunning());
    std::cout << "  -> Scheduler started" << std::endl;

    // Starting again should be safe (idempotent)
    scheduler.Start();
    assert(scheduler.IsRunning());
    std::cout << "  -> Double-start is safe" << std::endl;

    scheduler.Stop();
    assert(!scheduler.IsRunning());
    std::cout << "  -> Scheduler stopped" << std::endl;

    // Stopping again should be safe
    scheduler.Stop();
    assert(!scheduler.IsRunning());
    std::cout << "  -> Double-stop is safe" << std::endl;

    std::cout << "[SUCCESS] AIScheduler Lifecycle passed!" << std::endl;
}

void TestAISchedulerPeriodicTask() {
    std::cout << "[TEST] AIScheduler Periodic Task..." << std::endl;

    auto& scheduler = AIScheduler::Instance();
    scheduler.Start();

    std::atomic<int> counter{0};

    // Schedule a task that runs every 50ms
    TaskId task_id = scheduler.SchedulePeriodic("test_counter", 50, [&counter]() {
        counter++;
    });

    std::cout << "  -> Scheduled periodic task with id=" << task_id << std::endl;

    // Wait for a few executions
    std::this_thread::sleep_for(std::chrono::milliseconds(350));

    int count = counter.load();
    std::cout << "  -> Task executed " << count << " times in ~350ms (expected ~5-7)" << std::endl;
    assert(count >= 3); // At least a few executions

    // Cancel the task
    scheduler.Cancel(task_id);
    int count_after_cancel = counter.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    int count_after_wait = counter.load();

    // Should not increase much after cancellation
    std::cout << "  -> After cancel: " << count_after_cancel
              << " -> " << count_after_wait << " (should be close)" << std::endl;
    assert(count_after_wait - count_after_cancel <= 1); // At most 1 more (race)

    scheduler.Stop();
    std::cout << "[SUCCESS] AIScheduler Periodic Task passed!" << std::endl;
}

void TestAISchedulerOneShotTask() {
    std::cout << "[TEST] AIScheduler One-Shot Task..." << std::endl;

    auto& scheduler = AIScheduler::Instance();
    scheduler.Start();

    std::atomic<int> fired{0};

    TaskId id = scheduler.ScheduleOnce("one_shot_test", 100, [&fired]() {
        fired++;
    });
    std::cout << "  -> Scheduled one-shot task with id=" << id << ", delay=100ms" << std::endl;

    // Should not have fired yet
    assert(fired.load() == 0);
    std::cout << "  -> Not fired immediately" << std::endl;

    // Wait for it to fire
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    assert(fired.load() == 1);
    std::cout << "  -> Fired exactly once after delay" << std::endl;

    // Wait more to ensure it doesn't fire again
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    assert(fired.load() == 1);
    std::cout << "  -> Did not fire again (one-shot behavior confirmed)" << std::endl;

    scheduler.Stop();
    std::cout << "[SUCCESS] AIScheduler One-Shot Task passed!" << std::endl;
}

void TestAISchedulerTaskListing() {
    std::cout << "[TEST] AIScheduler Task Listing..." << std::endl;

    auto& scheduler = AIScheduler::Instance();
    scheduler.Start();

    TaskId id1 = scheduler.SchedulePeriodic("immune_check", 1000, []() {});
    TaskId id2 = scheduler.SchedulePeriodic("temporal_analysis", 30000, []() {});

    auto tasks = scheduler.GetScheduledTasks();
    std::cout << "  -> " << tasks.size() << " tasks scheduled" << std::endl;

    bool found_immune = false, found_temporal = false;
    for (const auto& t : tasks) {
        std::cout << "    Task[" << t.id << "]: " << t.name
                  << " interval=" << t.interval_ms << "ms"
                  << " periodic=" << t.periodic
                  << " runs=" << t.run_count << std::endl;
        if (t.name == "immune_check") found_immune = true;
        if (t.name == "temporal_analysis") found_temporal = true;
    }

    assert(found_immune);
    assert(found_temporal);
    std::cout << "  -> Both scheduled tasks found in listing" << std::endl;

    scheduler.Cancel(id1);
    scheduler.Cancel(id2);
    scheduler.Stop();
    std::cout << "[SUCCESS] AIScheduler Task Listing passed!" << std::endl;
}

void TestAISchedulerCancellation() {
    std::cout << "[TEST] AIScheduler Cancellation..." << std::endl;

    auto& scheduler = AIScheduler::Instance();
    scheduler.Start();

    std::atomic<int> counter{0};

    TaskId id = scheduler.SchedulePeriodic("cancel_test", 50, [&counter]() {
        counter++;
    });

    // Let it run briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    int before = counter.load();
    assert(before > 0);
    std::cout << "  -> Task ran " << before << " times before cancellation" << std::endl;

    scheduler.Cancel(id);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    int after = counter.load();

    assert(after - before <= 1);
    std::cout << "  -> After cancellation: " << after << " (no significant increase)" << std::endl;

    // Cancel non-existent task should be safe
    scheduler.Cancel(99999);
    std::cout << "  -> Cancelling non-existent task is safe" << std::endl;

    scheduler.Stop();
    std::cout << "[SUCCESS] AIScheduler Cancellation passed!" << std::endl;
}
