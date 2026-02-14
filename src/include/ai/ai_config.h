#pragma once

#include <cstdint>
#include <cstddef>

namespace chronosdb {
namespace ai {

// ========================================================================
// METRICS STORE
// ========================================================================

// Ring buffer capacity for metric events
static constexpr size_t METRICS_RING_BUFFER_CAPACITY = 65536;

// ========================================================================
// SELF-LEARNING ENGINE (UCB1 Bandit)
// ========================================================================

// UCB1 exploration constant: sqrt(2) balances exploration vs exploitation
static constexpr double UCB1_EXPLORATION_CONSTANT = 1.414;

// Minimum total query observations before the engine starts recommending
static constexpr size_t MIN_SAMPLES_BEFORE_LEARNING = 30;

// Minimum per-arm pulls before that arm participates in UCB1 scoring
static constexpr size_t MIN_ARM_PULLS = 5;

// Per-table contextual override requires this many pulls per arm
static constexpr size_t MIN_TABLE_PULLS_FOR_CONTEXT = 10;

// Number of feature dimensions for query characterization
static constexpr size_t QUERY_FEATURE_DIMENSIONS = 8;

// Reward normalization: reward = 1.0 / (1.0 + time_ms / REWARD_SCALE_MS)
static constexpr double REWARD_SCALE_MS = 100.0;

// ========================================================================
// IMMUNE SYSTEM (Anomaly Detection)
// ========================================================================

// Number of historical rate intervals used for z-score baseline
static constexpr size_t MUTATION_WINDOW_SIZE = 100;

// Z-score thresholds for anomaly severity classification
static constexpr double ZSCORE_LOW_THRESHOLD = 2.0;
static constexpr double ZSCORE_MEDIUM_THRESHOLD = 3.0;
static constexpr double ZSCORE_HIGH_THRESHOLD = 4.0;

// Interval between periodic anomaly analysis checks (milliseconds)
static constexpr uint32_t IMMUNE_CHECK_INTERVAL_MS = 1000;

// Maximum events retained per user for behavioral profiling
static constexpr size_t USER_PROFILE_HISTORY_SIZE = 500;

// Duration of the rolling mutation window (microseconds) = 10 minutes
static constexpr uint64_t MUTATION_ROLLING_WINDOW_US = 10ULL * 60 * 1000000;

// Duration for rate calculation interval (microseconds) = 1 minute
static constexpr uint64_t RATE_INTERVAL_US = 60ULL * 1000000;

// Auto-recovery lookback: recover to this many microseconds before anomaly
static constexpr uint64_t RECOVERY_LOOKBACK_US = 60ULL * 1000000;  // 60 seconds

// Maximum anomaly history entries retained
static constexpr size_t MAX_ANOMALY_HISTORY = 200;

// Absolute threshold: a single DML affecting >= this many rows triggers immediate detection
static constexpr uint32_t MASS_OPERATION_ROW_THRESHOLD = 50;

// Absolute rate threshold: mutations/sec above this is anomalous even without baseline history
static constexpr double ABSOLUTE_RATE_THRESHOLD = 10.0;

// User deviation weights
static constexpr double USER_DEVIATION_MUTATION_WEIGHT = 0.7;
static constexpr double USER_DEVIATION_TABLE_WEIGHT = 0.3;

// ========================================================================
// TEMPORAL INDEX MANAGER
// ========================================================================

// Maximum temporal access events tracked in the access tracker
static constexpr size_t ACCESS_PATTERN_WINDOW_SIZE = 1000;

// DBSCAN clustering parameters
static constexpr size_t HOTSPOT_CLUSTER_MIN_POINTS = 5;
static constexpr double HOTSPOT_CLUSTER_EPSILON_US = 60000000.0;  // 60 seconds

// Interval between periodic temporal analysis (milliseconds)
static constexpr uint32_t TEMPORAL_ANALYSIS_INTERVAL_MS = 30000;

// CUSUM change-point detection threshold multiplier (times sigma)
static constexpr double CUSUM_THRESHOLD_SIGMA_MULT = 4.0;

// CUSUM drift parameter multiplier (times sigma)
static constexpr double CUSUM_DRIFT_SIGMA_MULT = 0.5;

// Markov chain prefetch: number of future timestamps to predict
static constexpr size_t PREFETCH_LOOKAHEAD_COUNT = 4;

// ========================================================================
// AI SCHEDULER
// ========================================================================

// Number of worker threads for AI background tasks
static constexpr size_t AI_THREAD_POOL_SIZE = 2;

// Scheduler loop tick interval (milliseconds)
static constexpr uint32_t AI_SCHEDULER_TICK_MS = 100;

} // namespace ai
} // namespace chronosdb
