#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ai/ai_config.h"
#include "ai/dml_observer.h"

namespace chronosdb {
namespace ai {

struct UserProfile {
    std::string username;
    double avg_mutations_per_minute;
    double avg_queries_per_minute;
    std::unordered_map<std::string, uint64_t> table_access_counts;
    uint64_t total_events;
};

/**
 * UserBehaviorProfiler - Per-user behavioral baselines.
 *
 * Tracks mutation rate, query rate, and table access distribution per user.
 * Returns a deviation score indicating how anomalous current behavior is.
 */
class UserBehaviorProfiler {
public:
    UserBehaviorProfiler();

    void RecordEvent(const std::string& user, DMLOperation op,
                     const std::string& table_name, uint64_t timestamp_us);

    // Deviation score: 0.0 = normal, higher = more anomalous
    double GetDeviationScore(const std::string& user) const;

    UserProfile GetProfile(const std::string& user) const;
    std::vector<UserProfile> GetAllProfiles() const;

private:
    struct UserHistory {
        std::deque<uint64_t> mutation_timestamps;
        std::deque<uint64_t> query_timestamps;
        std::unordered_map<std::string, uint64_t> table_counts;
        uint64_t total_events = 0;
        mutable std::mutex mutex;
    };

    mutable std::shared_mutex users_mutex_;
    std::unordered_map<std::string, std::unique_ptr<UserHistory>> users_;

    UserHistory& GetOrCreate(const std::string& user);
    void PruneHistory(UserHistory& history) const;
    uint64_t GetCurrentTimeUs() const;
};

} // namespace ai
} // namespace chronosdb
