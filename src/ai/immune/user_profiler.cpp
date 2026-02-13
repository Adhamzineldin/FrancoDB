#include "ai/immune/user_profiler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

namespace chronosdb {
namespace ai {

UserBehaviorProfiler::UserBehaviorProfiler() = default;

void UserBehaviorProfiler::RecordEvent(const std::string& user, DMLOperation op,
                                        const std::string& table_name,
                                        uint64_t timestamp_us) {
    auto& history = GetOrCreate(user);
    std::lock_guard lock(history.mutex);

    if (op == DMLOperation::SELECT) {
        history.query_timestamps.push_back(timestamp_us);
    } else {
        history.mutation_timestamps.push_back(timestamp_us);
    }
    history.table_counts[table_name]++;
    history.total_events++;

    PruneHistory(history);
}

double UserBehaviorProfiler::GetDeviationScore(const std::string& user) const {
    std::shared_lock users_lock(users_mutex_);
    auto it = users_.find(user);
    if (it == users_.end()) return 0.0;

    std::lock_guard lock(it->second->mutex);
    const auto& h = *it->second;

    if (h.total_events < 20) return 0.0; // Not enough data for baseline

    uint64_t now = GetCurrentTimeUs();
    uint64_t recent_window = RATE_INTERVAL_US; // Last 1 minute

    // Recent mutation rate
    size_t recent_mutations = 0;
    for (auto rit = h.mutation_timestamps.rbegin();
         rit != h.mutation_timestamps.rend(); ++rit) {
        if (*rit >= now - recent_window) recent_mutations++;
        else break;
    }

    // Historical mutation rate (overall average)
    double total_span = 0.0;
    if (!h.mutation_timestamps.empty()) {
        total_span = static_cast<double>(h.mutation_timestamps.back() -
                                          h.mutation_timestamps.front());
    }
    double avg_mutations_per_minute = 0.0;
    if (total_span > 0) {
        avg_mutations_per_minute =
            static_cast<double>(h.mutation_timestamps.size()) /
            (total_span / 60000000.0);
    }

    double recent_mutations_per_minute =
        static_cast<double>(recent_mutations) /
        (static_cast<double>(recent_window) / 60000000.0);

    // Mutation rate deviation
    double mutation_deviation = 0.0;
    if (avg_mutations_per_minute > 0) {
        mutation_deviation =
            std::abs(recent_mutations_per_minute - avg_mutations_per_minute) /
            std::max(avg_mutations_per_minute, 1.0);
    }

    // Table access deviation: fraction of tables never accessed before
    // (not applicable with rolling window, simplified to 0)
    double table_deviation = 0.0;

    return USER_DEVIATION_MUTATION_WEIGHT * mutation_deviation +
           USER_DEVIATION_TABLE_WEIGHT * table_deviation;
}

UserProfile UserBehaviorProfiler::GetProfile(const std::string& user) const {
    std::shared_lock users_lock(users_mutex_);
    auto it = users_.find(user);
    if (it == users_.end()) {
        return UserProfile{user, 0.0, 0.0, {}, 0};
    }

    std::lock_guard lock(it->second->mutex);
    const auto& h = *it->second;

    UserProfile profile;
    profile.username = user;
    profile.total_events = h.total_events;
    profile.table_access_counts = h.table_counts;

    // Compute rates
    double span_minutes = 0.0;
    if (!h.mutation_timestamps.empty()) {
        span_minutes =
            static_cast<double>(h.mutation_timestamps.back() -
                                h.mutation_timestamps.front()) /
            60000000.0;
    }
    if (!h.query_timestamps.empty()) {
        double qspan =
            static_cast<double>(h.query_timestamps.back() -
                                h.query_timestamps.front()) /
            60000000.0;
        span_minutes = std::max(span_minutes, qspan);
    }

    if (span_minutes > 0) {
        profile.avg_mutations_per_minute =
            static_cast<double>(h.mutation_timestamps.size()) / span_minutes;
        profile.avg_queries_per_minute =
            static_cast<double>(h.query_timestamps.size()) / span_minutes;
    }

    return profile;
}

std::vector<UserProfile> UserBehaviorProfiler::GetAllProfiles() const {
    std::shared_lock lock(users_mutex_);
    std::vector<UserProfile> profiles;
    profiles.reserve(users_.size());
    for (const auto& [name, _] : users_) {
        // GetProfile acquires its own lock, so release users_mutex first
        // by collecting names then calling GetProfile
    }
    // Collect names first
    std::vector<std::string> names;
    names.reserve(users_.size());
    for (const auto& [name, _] : users_) {
        names.push_back(name);
    }
    lock.unlock();

    for (const auto& name : names) {
        profiles.push_back(GetProfile(name));
    }
    return profiles;
}

UserBehaviorProfiler::UserHistory& UserBehaviorProfiler::GetOrCreate(
    const std::string& user) {
    {
        std::shared_lock lock(users_mutex_);
        auto it = users_.find(user);
        if (it != users_.end()) return *it->second;
    }
    std::unique_lock lock(users_mutex_);
    auto [it, inserted] = users_.try_emplace(
        user, std::make_unique<UserHistory>());
    return *it->second;
}

void UserBehaviorProfiler::PruneHistory(UserHistory& history) const {
    while (history.mutation_timestamps.size() > USER_PROFILE_HISTORY_SIZE) {
        history.mutation_timestamps.pop_front();
    }
    while (history.query_timestamps.size() > USER_PROFILE_HISTORY_SIZE) {
        history.query_timestamps.pop_front();
    }
}

uint64_t UserBehaviorProfiler::GetCurrentTimeUs() const {
    auto now = std::chrono::system_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count());
}

} // namespace ai
} // namespace chronosdb
