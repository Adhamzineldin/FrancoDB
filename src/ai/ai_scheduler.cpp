#include "ai/ai_scheduler.h"
#include "common/thread_pool.h"
#include "common/logger.h"

#include <chrono>

namespace chronosdb {
namespace ai {

AIScheduler& AIScheduler::Instance() {
    static AIScheduler instance;
    return instance;
}

AIScheduler::AIScheduler() = default;

AIScheduler::~AIScheduler() {
    Stop();
}

TaskId AIScheduler::SchedulePeriodic(const std::string& name, uint32_t interval_ms,
                                      std::function<void()> task) {
    std::lock_guard lock(tasks_mutex_);
    TaskId id = next_task_id_.fetch_add(1, std::memory_order_relaxed);
    uint64_t now = GetCurrentTimeUs();
    tasks_.push_back({id, name, interval_ms, std::move(task),
                      now + static_cast<uint64_t>(interval_ms) * 1000,
                      0, 0, true, false});
    LOG_DEBUG("AIScheduler", "Registered periodic task: " + name +
              " (interval=" + std::to_string(interval_ms) + "ms)");
    return id;
}

TaskId AIScheduler::ScheduleOnce(const std::string& name, uint32_t delay_ms,
                                  std::function<void()> task) {
    std::lock_guard lock(tasks_mutex_);
    TaskId id = next_task_id_.fetch_add(1, std::memory_order_relaxed);
    uint64_t now = GetCurrentTimeUs();
    tasks_.push_back({id, name, 0, std::move(task),
                      now + static_cast<uint64_t>(delay_ms) * 1000,
                      0, 0, false, false});
    return id;
}

void AIScheduler::Cancel(TaskId id) {
    std::lock_guard lock(tasks_mutex_);
    for (auto& t : tasks_) {
        if (t.id == id) {
            t.cancelled = true;
            break;
        }
    }
}

void AIScheduler::Start() {
    if (running_.load()) return;
    running_ = true;
    thread_pool_ = std::make_unique<ThreadPool>(AI_THREAD_POOL_SIZE);
    scheduler_thread_ = std::thread(&AIScheduler::SchedulerLoop, this);
    LOG_INFO("AIScheduler", "Started with " +
             std::to_string(AI_THREAD_POOL_SIZE) + " worker threads");
}

void AIScheduler::Stop() {
    running_ = false;
    if (scheduler_thread_.joinable()) {
        scheduler_thread_.join();
    }
    thread_pool_.reset();
}

bool AIScheduler::IsRunning() const {
    return running_.load();
}

std::vector<AIScheduler::TaskInfo> AIScheduler::GetScheduledTasks() const {
    std::lock_guard lock(tasks_mutex_);
    std::vector<TaskInfo> result;
    result.reserve(tasks_.size());
    for (const auto& t : tasks_) {
        if (!t.cancelled) {
            result.push_back({t.id, t.name, t.interval_ms,
                              t.last_run_us, t.run_count, t.periodic});
        }
    }
    return result;
}

void AIScheduler::SchedulerLoop() {
    while (running_.load()) {
        // Sleep in small ticks for responsive shutdown
        for (uint32_t i = 0; i < AI_SCHEDULER_TICK_MS / 10 && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!running_.load()) break;

        uint64_t now = GetCurrentTimeUs();

        std::lock_guard lock(tasks_mutex_);
        for (auto& t : tasks_) {
            if (t.cancelled) continue;
            if (now < t.next_run_us) continue;

            // Task is due — dispatch to thread pool
            auto task_fn = t.task; // Copy for thread safety
            thread_pool_->Enqueue([task_fn]() {
                try {
                    task_fn();
                } catch (const std::exception& e) {
                    LOG_ERROR("AIScheduler",
                              std::string("Task exception: ") + e.what());
                }
            });

            t.last_run_us = now;
            t.run_count++;

            if (t.periodic) {
                t.next_run_us = now + static_cast<uint64_t>(t.interval_ms) * 1000;
            } else {
                t.cancelled = true; // One-shot: auto-cancel after execution
            }
        }

        // Prune cancelled one-shot tasks periodically
        tasks_.erase(
            std::remove_if(tasks_.begin(), tasks_.end(),
                           [](const ScheduledTask& t) {
                               return t.cancelled && !t.periodic;
                           }),
            tasks_.end());
    }
}

uint64_t AIScheduler::GetCurrentTimeUs() const {
    auto now = std::chrono::system_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch())
            .count());
}

} // namespace ai
} // namespace chronosdb
