#include "ai/dml_observer.h"

#include <algorithm>
#include <mutex>

namespace chronosdb {
namespace ai {

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
    std::shared_lock lock(mutex_);
    for (auto* observer : observers_) {
        if (!observer->OnBeforeDML(event)) {
            return false; // Operation blocked
        }
    }
    return true;
}

void DMLObserverRegistry::NotifyAfter(const DMLEvent& event) {
    std::shared_lock lock(mutex_);
    for (auto* observer : observers_) {
        observer->OnAfterDML(event);
    }
}

size_t DMLObserverRegistry::GetObserverCount() const {
    std::shared_lock lock(mutex_);
    return observers_.size();
}

} // namespace ai
} // namespace chronosdb
