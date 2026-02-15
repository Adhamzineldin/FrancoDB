#include "ai/dml_observer.h"
#include "ai/ai_scheduler.h"

#include <algorithm>
#include <memory>
#include <mutex>

namespace chronosdb {
namespace ai {

// Thread-local block reason: set by immune system, read by DML executor
static thread_local std::string tl_block_reason;

std::string DMLObserverRegistry::GetLastBlockReason() {
    return tl_block_reason;
}

void DMLObserverRegistry::SetBlockReason(const std::string& reason) {
    tl_block_reason = reason;
}

DMLObserverRegistry& DMLObserverRegistry::Instance() {
    static DMLObserverRegistry instance;
    return instance;
}

void DMLObserverRegistry::Register(IDMLObserver* observer) {
    if (!observer) return;
    std::unique_lock lock(mutex_);
    // Prevent duplicate registration
    auto it = std::find(observers_.begin(), observers_.end(), observer);
    if (it == observers_.end()) {
        observers_.push_back(observer);
    }
}

void DMLObserverRegistry::Unregister(IDMLObserver* observer) {
    if (!observer) return;
    std::unique_lock lock(mutex_);
    observers_.erase(
        std::remove(observers_.begin(), observers_.end(), observer),
        observers_.end());
}

bool DMLObserverRegistry::NotifyBefore(const DMLEvent& event) {
    tl_block_reason.clear();
    std::shared_lock lock(mutex_);
    for (auto* observer : observers_) {
        if (!observer->OnBeforeDML(event)) {
            return false; // Operation blocked - reason in tl_block_reason
        }
    }
    return true;
}

void DMLObserverRegistry::NotifyAfter(const DMLEvent& event) {
    // Make a copy for async processing
    auto event_copy = std::make_shared<DMLEvent>(event);

    // Get observers snapshot while holding lock
    std::vector<IDMLObserver*> observers_snapshot;
    {
        std::shared_lock lock(mutex_);
        observers_snapshot = observers_;
    }

    // Fire async - don't block query execution for AI processing
    AIScheduler::Instance().ScheduleOnce(
        "DMLObserver::NotifyAfter",
        0, // Execute immediately on worker thread
        [observers_snapshot, event_copy]() {
            for (auto* observer : observers_snapshot) {
                if (observer) {
                    observer->OnAfterDML(*event_copy);
                }
            }
        });
}

size_t DMLObserverRegistry::GetObserverCount() const {
    std::shared_lock lock(mutex_);
    return observers_.size();
}

} // namespace ai
} // namespace chronosdb
