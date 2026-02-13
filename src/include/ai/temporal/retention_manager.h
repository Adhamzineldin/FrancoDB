#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "ai/temporal/access_tracker.h"

namespace chronosdb {

class LogManager;

namespace ai {

/**
 * WALRetentionManager - Manages adaptive WAL retention policy.
 *
 * Hot periods (frequently queried via time travel) retain full WAL fidelity.
 * Cold periods (rarely/never queried) can be pruned to save storage.
 */
class WALRetentionManager {
public:
    explicit WALRetentionManager(LogManager* log_manager);

    struct RetentionPolicy {
        uint64_t hot_retention_us;  // Full fidelity for records newer than this
        uint64_t cold_cutoff_us;    // Prune records older than this
    };

    // Compute retention policy based on access patterns
    RetentionPolicy ComputePolicy(const TemporalAccessTracker& tracker) const;

    // Get current retention stats
    struct RetentionStats {
        uint64_t hot_zone_start_us;
        uint64_t cold_cutoff_us;
        size_t policy_updates;
    };
    RetentionStats GetStats() const;

    void UpdatePolicy(const RetentionPolicy& policy);

private:
    LogManager* log_manager_;
    mutable std::mutex mutex_;
    RetentionStats current_stats_{};
};

} // namespace ai
} // namespace chronosdb
