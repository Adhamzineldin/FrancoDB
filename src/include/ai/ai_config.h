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
// AI RELEARNING / DECAY (Activity-Aware Adaptive Decay)
// ========================================================================

// Decay is DYNAMIC: computed from actual query activity each interval.
// When users are idle (sleeping, off-hours), decay approaches 1.0 (no decay).
// When activity is normal, decay is ~0.8. Under heavy load, decay is stronger (~0.6).

// Interval for periodic decay check (milliseconds) = 10 minutes
static constexpr uint32_t AI_DECAY_INTERVAL_MS = 10 * 60 * 1000;

// --- Activity-based decay curve parameters ---

// Minimum decay factor (applied at very high activity)
static constexpr double AI_DECAY_MIN = 0.6;

// Maximum decay factor (applied at zero/near-zero activity = essentially no decay)
static constexpr double AI_DECAY_MAX = 1.0;

// Default/baseline decay factor at "normal" activity level
static constexpr double AI_DECAY_BASELINE = 0.8;

// Number of queries per decay interval considered "normal" activity.
// Below this → decay weakens toward 1.0. Above this → decay strengthens toward 0.6.
static constexpr uint64_t AI_DECAY_NORMAL_QUERY_COUNT = 100;

// Activity ratio above which decay is at its strongest (AI_DECAY_MIN)
// e.g., 3.0 means 3x normal activity = maximum decay
static constexpr double AI_DECAY_HIGH_ACTIVITY_RATIO = 3.0;

// Minimum query count to trigger ANY decay at all.
// Below this threshold, decay factor = 1.0 (no decay, system is idle).
static constexpr uint64_t AI_DECAY_IDLE_THRESHOLD = 5;

// Interval for full reset if workload changes dramatically (milliseconds) = 1 hour
static constexpr uint32_t AI_FULL_RESET_INTERVAL_MS = 60 * 60 * 1000;

// Threshold for detecting workload change (ratio of current to historical performance)
// If performance differs by more than this factor, trigger faster relearning
static constexpr double WORKLOAD_CHANGE_THRESHOLD = 2.0;

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
