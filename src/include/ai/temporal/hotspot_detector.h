#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ai/ai_config.h"
#include "ai/temporal/access_tracker.h"

namespace chronosdb {
namespace ai {

struct TemporalHotspot {
    uint64_t center_timestamp_us;
    uint64_t range_start_us;
    uint64_t range_end_us;
    uint64_t access_count;
    double density; // Accesses per second in this cluster
};

/**
 * HotspotDetector - Clusters temporal access patterns to find hotspots.
 *
 * Uses simplified DBSCAN for temporal clustering.
 * Uses CUSUM for change-point detection in mutation rate time series.
 */
class HotspotDetector {
public:
    HotspotDetector();

    // Detect hotspots from access events
    std::vector<TemporalHotspot> DetectHotspots(
        const std::vector<TemporalAccessEvent>& events) const;

    // Detect change points in mutation rate time series
    // Returns timestamps where significant state changes occurred
    std::vector<uint64_t> DetectChangePoints(
        const std::vector<double>& mutation_rates,
        const std::vector<uint64_t>& timestamps) const;

private:
    // Simplified DBSCAN: group timestamps within epsilon of each other
    std::vector<std::vector<size_t>> ClusterTimestamps(
        const std::vector<uint64_t>& timestamps,
        double epsilon_us, size_t min_points) const;

    // CUSUM (Cumulative Sum) change-point detection
    std::vector<size_t> CUSUMChangePoints(
        const std::vector<double>& values, double threshold,
        double drift) const;
};

} // namespace ai
} // namespace chronosdb
