#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ai/ai_config.h"

namespace chronosdb {

class ThreadPool; // Forward declaration

namespace ai {

using TaskId = uint32_t;

/**
 * AIScheduler - Manages periodic background tasks for all AI systems.
 *
 * Uses the existing ThreadPool for task execution.
 * Follows the AdaptationLoop pattern: sleep in small ticks, check running_ flag.
 */
class AIScheduler {
public:
    static AIScheduler& Instance();

    // Register a periodic task. Returns TaskId for cancellation.
    TaskId SchedulePeriodic(const std::string& name, uint32_t interval_ms,
                            std::function<void()> task);

    // Register a one-shot delayed task
    TaskId ScheduleOnce(const std::string& name, uint32_t delay_ms,
                        std::function<void()> task);

    // Cancel a scheduled task
    void Cancel(TaskId id);

    // Lifecycle
    void Start();
    void Stop();
    bool IsRunning() const;

    // Status reporting for SHOW AI STATUS
    struct TaskInfo {
        TaskId id;
        std::string name;
        uint32_t interval_ms;
        uint64_t last_run_us;
        uint64_t run_count;
        bool periodic;
    };
    std::vector<TaskInfo> GetScheduledTasks() const;

private:
    AIScheduler();
    ~AIScheduler();
    AIScheduler(const AIScheduler&) = delete;
    AIScheduler& operator=(const AIScheduler&) = delete;

    void SchedulerLoop();
    uint64_t GetCurrentTimeUs() const;

    struct ScheduledTask {
        TaskId id;
        std::string name;
        uint32_t interval_ms;
        std::function<void()> task;
        uint64_t next_run_us;
        uint64_t last_run_us;
        uint64_t run_count;
        bool periodic;
        bool cancelled;
    };

    std::unique_ptr<ThreadPool> thread_pool_;
    std::thread scheduler_thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex tasks_mutex_;
    std::vector<ScheduledTask> tasks_;
    std::atomic<TaskId> next_task_id_{1};
};

} // namespace ai
} // namespace chronosdb
