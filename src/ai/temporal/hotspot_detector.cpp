#include "ai/temporal/hotspot_detector.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace chronosdb {
namespace ai {

HotspotDetector::HotspotDetector() = default;

std::vector<TemporalHotspot> HotspotDetector::DetectHotspots(
    const std::vector<TemporalAccessEvent>& events) const {
    if (events.empty()) return {};

    // Extract and sort queried timestamps
    std::vector<uint64_t> timestamps;
    timestamps.reserve(events.size());
    for (const auto& e : events) {
        timestamps.push_back(e.queried_timestamp_us);
    }
    std::sort(timestamps.begin(), timestamps.end());

    // Cluster using simplified DBSCAN
    auto clusters = ClusterTimestamps(
        timestamps, HOTSPOT_CLUSTER_EPSILON_US, HOTSPOT_CLUSTER_MIN_POINTS);

    // Convert clusters to hotspots
    std::vector<TemporalHotspot> hotspots;
    hotspots.reserve(clusters.size());

    for (const auto& cluster : clusters) {
        if (cluster.size() < HOTSPOT_CLUSTER_MIN_POINTS) continue;

        TemporalHotspot hs;
        hs.access_count = cluster.size();

        // Compute center, range
        uint64_t min_ts = timestamps[cluster[0]];
        uint64_t max_ts = timestamps[cluster[0]];
        double sum = 0.0;
        for (size_t idx : cluster) {
            min_ts = std::min(min_ts, timestamps[idx]);
            max_ts = std::max(max_ts, timestamps[idx]);
            sum += static_cast<double>(timestamps[idx]);
        }

        hs.center_timestamp_us = static_cast<uint64_t>(
            sum / static_cast<double>(cluster.size()));
        hs.range_start_us = min_ts;
        hs.range_end_us = max_ts;

        // Density: accesses per second in this range
        double range_seconds =
            static_cast<double>(max_ts - min_ts) / 1000000.0;
        hs.density = range_seconds > 0
                         ? static_cast<double>(hs.access_count) / range_seconds
                         : static_cast<double>(hs.access_count);

        hotspots.push_back(hs);
    }

    // Sort by density (highest first)
    std::sort(hotspots.begin(), hotspots.end(),
              [](const auto& a, const auto& b) {
                  return a.density > b.density;
              });

    return hotspots;
}

std::vector<uint64_t> HotspotDetector::DetectChangePoints(
    const std::vector<double>& mutation_rates,
    const std::vector<uint64_t>& timestamps) const {
    if (mutation_rates.size() < 3 || mutation_rates.size() != timestamps.size()) {
        return {};
    }

    // Compute statistics for CUSUM parameters
    double sum = std::accumulate(mutation_rates.begin(),
                                  mutation_rates.end(), 0.0);
    double mean = sum / static_cast<double>(mutation_rates.size());

    double sq_sum = 0.0;
    for (double v : mutation_rates) {
        sq_sum += (v - mean) * (v - mean);
    }
    double sigma = std::sqrt(
        sq_sum / static_cast<double>(mutation_rates.size()));

    if (sigma < 0.001) return {}; // No variance = no change points

    double threshold = CUSUM_THRESHOLD_SIGMA_MULT * sigma;
    double drift = CUSUM_DRIFT_SIGMA_MULT * sigma;

    auto change_indices = CUSUMChangePoints(mutation_rates, threshold, drift);

    std::vector<uint64_t> change_timestamps;
    change_timestamps.reserve(change_indices.size());
    for (size_t idx : change_indices) {
        if (idx < timestamps.size()) {
            change_timestamps.push_back(timestamps[idx]);
        }
    }
    return change_timestamps;
}

std::vector<std::vector<size_t>> HotspotDetector::ClusterTimestamps(
    const std::vector<uint64_t>& timestamps, double epsilon_us,
    size_t min_points) const {
    // Simplified DBSCAN on sorted 1D data:
    // Walk through sorted timestamps, group consecutive ones within epsilon
    std::vector<std::vector<size_t>> clusters;
    if (timestamps.empty()) return clusters;

    std::vector<size_t> current_cluster;
    current_cluster.push_back(0);

    for (size_t i = 1; i < timestamps.size(); ++i) {
        double diff = static_cast<double>(timestamps[i] - timestamps[i - 1]);
        if (diff <= epsilon_us) {
            current_cluster.push_back(i);
        } else {
            // End of cluster
            if (current_cluster.size() >= min_points) {
                clusters.push_back(std::move(current_cluster));
            }
            current_cluster.clear();
            current_cluster.push_back(i);
        }
    }

    // Don't forget the last cluster
    if (current_cluster.size() >= min_points) {
        clusters.push_back(std::move(current_cluster));
    }

    return clusters;
}

std::vector<size_t> HotspotDetector::CUSUMChangePoints(
    const std::vector<double>& values, double threshold,
    double drift) const {
    std::vector<size_t> change_points;

    // Positive CUSUM: detects upward shifts
    double s_pos = 0.0;
    // Negative CUSUM: detects downward shifts
    double s_neg = 0.0;

    double mean = std::accumulate(values.begin(), values.end(), 0.0) /
                  static_cast<double>(values.size());

    for (size_t i = 0; i < values.size(); ++i) {
        s_pos = std::max(0.0, s_pos + (values[i] - mean - drift));
        s_neg = std::max(0.0, s_neg + (mean - values[i] - drift));

        if (s_pos > threshold || s_neg > threshold) {
            change_points.push_back(i);
            s_pos = 0.0;
            s_neg = 0.0;
        }
    }

    return change_points;
}

} // namespace ai
} // namespace chronosdb
