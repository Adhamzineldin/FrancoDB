# ChronosDB AI Layer - Technical Documentation

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [System Architecture](#2-system-architecture)
3. [AI Layer Overview](#3-ai-layer-overview)
4. [Phase 0: Shared Foundation](#4-phase-0-shared-foundation)
5. [Phase 1: Self-Learning Execution Engine](#5-phase-1-self-learning-execution-engine)
6. [Phase 2: Immune System](#6-phase-2-immune-system)
7. [Phase 3: Intelligent Temporal Index Manager](#7-phase-3-intelligent-temporal-index-manager)
8. [Integration Architecture](#8-integration-architecture)
9. [SQL Commands](#9-sql-commands)
10. [PlantUML Diagrams](#10-plantuml-diagrams)
11. [File Inventory](#11-file-inventory)
12. [Algorithm Details](#12-algorithm-details)

---

## 1. Project Overview

**ChronosDB** is a high-performance, multi-protocol database management system written in C++20. It features:

- **Role-Based Access Control (RBAC)** with 5 roles: SUPERADMIN, ADMIN, USER, READONLY, DENIED
- **Persistent Storage** with crash recovery via Write-Ahead Logging (WAL)
- **Time Travel** capability — query data at any historical point using `SELECT ... AS OF`
- **CQL (Chronos Query Language)** — SQL with Franco-Arabic keyword alternatives
- **B+ Tree Indexes** for fast lookups
- **Buffer Pool Management** with adaptive partitioned strategy
- **Transaction Support** with auto-commit and explicit transactions

### Core Architecture

```
Client (Shell/Network)
    |
    v
ConnectionHandler (Multi-protocol: TEXT, JSON, BINARY)
    |
    v
Parser (Lexer -> Token Stream -> AST)
    |
    v
ExecutionEngine (Dispatch Map Pattern)
    |
    +---> DDLExecutor     (CREATE, DROP, ALTER)
    +---> DMLExecutor     (INSERT, SELECT, UPDATE, DELETE)
    +---> SystemExecutor  (SHOW, WHOAMI, STATUS)
    +---> UserExecutor    (CREATE USER, ALTER USER)
    +---> DatabaseExecutor(CREATE DATABASE, USE)
    +---> TransactionExecutor (BEGIN, COMMIT, ROLLBACK)
    |
    v
Storage Layer (BufferPoolManager -> DiskManager -> Pages)
    |
    v
Recovery Layer (LogManager -> WAL -> CheckpointManager)
```

### Key Technologies

| Component | Technology |
|-----------|-----------|
| Language | C++20 |
| Build System | CMake + Ninja |
| Page Size | 4KB |
| Default Buffer Pool | 65536 pages (256MB) |
| Network Port | 2501 |
| Protocol | `chronos://user:pass@host:port/db` |

---

## 2. System Architecture

### Design Patterns Used

| Pattern | Where Used | Purpose |
|---------|-----------|---------|
| **Factory** | `ExecutorFactory` | Creates executors based on statement type |
| **Dispatch Map** | `ExecutionEngine` | Routes statements to handlers without switch/if-else |
| **Observer** | `DMLObserverRegistry` | Decouples AI from execution engine |
| **Strategy** | `IQueryOptimizer` | Swappable scan strategy selection |
| **Singleton** | `AIManager`, `MetricsStore`, `AIScheduler` | Global AI coordination |
| **RAII** | Throughout | Resource management via smart pointers |
| **Iterator** | `TableHeap::Iterator` | Tuple-at-a-time processing |

### Thread Safety Model

- **Global Reader-Writer Lock**: `ExecutionEngine::global_lock_` protects concurrent DML (shared) vs. exclusive recovery operations
- **Per-component Mutexes**: Each AI subsystem uses `std::shared_mutex` for read-heavy access patterns
- **Lock-Free Ring Buffer**: `MetricsStore` uses `std::atomic` write index for zero-contention metric recording
- **Atomic Flags**: All AI subsystems use `std::atomic<bool>` for safe start/stop lifecycle

---

## 3. AI Layer Overview

The AI layer consists of three integrated subsystems that share infrastructure and feed each other data:

```
┌─────────────────────────────────────────────────────────────────┐
│                        AI Layer                                 │
│                                                                 │
│  ┌──────────────────┐  ┌─────────────────┐  ┌───────────────┐  │
│  │ Learning Engine   │  │ Immune System   │  │ Temporal Index │  │
│  │ (Phase 1)        │  │ (Phase 2)       │  │ (Phase 3)     │  │
│  │                  │  │                 │  │               │  │
│  │ UCB1 Bandit      │  │ Z-Score Anomaly │  │ DBSCAN        │  │
│  │ Scan Strategy    │  │ Auto-Recovery   │  │ CUSUM         │  │
│  │ Per-table Learn  │  │ User Profiling  │  │ Smart Snapshot│  │
│  └────────┬─────────┘  └────────┬────────┘  └──────┬────────┘  │
│           │                     │                   │           │
│           └─────────────────────┼───────────────────┘           │
│                                 │                               │
│  ┌──────────────────────────────┴────────────────────────────┐  │
│  │              Shared Foundation (Phase 0)                   │  │
│  │  MetricsStore | DMLObserverRegistry | AIScheduler          │  │
│  └───────────────────────────────────────────────────────────┘  │
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                   AIManager (Singleton)                    │  │
│  │  Initialize() | Shutdown() | GetStatus()                   │  │
│  └───────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### How the Three Systems Feed Each Other

1. **MetricsStore** is the shared data backbone — all three systems record events here
2. **DMLObserverRegistry** broadcasts every INSERT/UPDATE/DELETE/SELECT to both the Learning Engine and Immune System
3. **Learning Engine** learns optimal scan strategies from SELECT feedback → feeds back into DMLExecutor
4. **Immune System** monitors mutation patterns → can trigger auto-recovery via TimeTravelEngine
5. **Temporal Index Manager** monitors time-travel query patterns → optimizes snapshot placement for faster recovery

---

## 4. Phase 0: Shared Foundation

### 4.1 AIConfig (`ai_config.h`)

All AI constants are centralized in a single header. No magic numbers in implementation files.

**Key Constants:**

| Constant | Value | Purpose |
|----------|-------|---------|
| `UCB1_EXPLORATION_CONSTANT` | 1.414 (√2) | UCB1 exploration-exploitation tradeoff |
| `MIN_SAMPLES_BEFORE_LEARNING` | 30 | Queries before AI starts recommending |
| `ZSCORE_LOW_THRESHOLD` | 2.0 | Anomaly detection: warning level |
| `ZSCORE_MEDIUM_THRESHOLD` | 3.0 | Anomaly detection: block mutations |
| `ZSCORE_HIGH_THRESHOLD` | 4.0 | Anomaly detection: auto-recover |
| `IMMUNE_CHECK_INTERVAL_MS` | 1000 | Immune system check frequency |
| `HOTSPOT_CLUSTER_EPSILON_US` | 60,000,000 | DBSCAN: 60s neighborhood |
| `HOTSPOT_CLUSTER_MIN_POINTS` | 5 | DBSCAN: minimum cluster size |
| `CUSUM_THRESHOLD_SIGMA_MULT` | 4.0 | CUSUM: change detection threshold |
| `METRICS_RING_BUFFER_SIZE` | 8192 | Max metrics in ring buffer |
| `ACCESS_PATTERN_WINDOW_SIZE` | 1000 | Temporal access history size |

### 4.2 MetricsStore (`metrics_store.h` / `metrics_store.cpp`)

**Purpose:** Thread-safe ring buffer for operation metrics. Shared by all 3 AI subsystems.

**Class: `MetricsStore` (Singleton)**

```cpp
class MetricsStore {
public:
    static MetricsStore& Instance();
    void Record(const MetricEvent& event);          // Lock-free write
    std::vector<MetricEvent> Query(MetricType type, uint64_t since_us) const;
    size_t CountEvents(MetricType type, uint64_t since_us) const;
    uint64_t GetMutationCount(const std::string& table, uint64_t since_us) const;
    size_t GetTotalRecorded() const;
    void Reset();
};
```

**MetricEvent struct:**

| Field | Type | Description |
|-------|------|-------------|
| `type` | `MetricType` | INSERT, UPDATE, DELETE, SELECT, ANOMALY_DETECTED, etc. |
| `timestamp_us` | `uint64_t` | Microsecond timestamp |
| `duration_us` | `uint64_t` | Operation duration |
| `user` | `string` | User who executed |
| `table_name` | `string` | Target table |
| `db_name` | `string` | Target database |
| `rows_affected` | `uint32_t` | Number of rows changed |
| `scan_strategy` | `uint8_t` | 0=SeqScan, 1=IndexScan |
| `target_timestamp` | `uint64_t` | For time-travel queries |

**Implementation Details:**
- Fixed-size array of `METRICS_RING_BUFFER_SIZE` (8192) entries
- Atomic write index for lock-free writes
- `std::shared_mutex` only for bulk reads (Query/Count operations)
- Oldest entries are overwritten when buffer wraps

### 4.3 DMLObserverRegistry (`dml_observer.h` / `dml_observer.cpp`)

**Purpose:** Observer pattern for decoupling AI from the execution engine.

**Interface: `IDMLObserver`**

```cpp
class IDMLObserver {
    virtual bool OnBeforeDML(const DMLEvent& event) { return true; }  // false = block
    virtual void OnAfterDML(const DMLEvent& event) {}
};
```

**Class: `DMLObserverRegistry` (Singleton)**

```cpp
class DMLObserverRegistry {
    void Register(IDMLObserver* observer);
    void Unregister(IDMLObserver* observer);
    bool NotifyBefore(const DMLEvent& event);  // Returns false if ANY observer blocks
    void NotifyAfter(const DMLEvent& event);
};
```

**DMLEvent struct:**

| Field | Type | Description |
|-------|------|-------------|
| `operation` | `DMLOperation` | INSERT, UPDATE, DELETE_OP, SELECT |
| `table_name` | `string` | Target table |
| `db_name` | `string` | Current database |
| `user` | `string` | Current user |
| `session_id` | `uint32_t` | Session identifier |
| `rows_affected` | `uint32_t` | Rows changed (after) |
| `start_time_us` | `uint64_t` | Operation start time |
| `duration_us` | `uint64_t` | Elapsed time (after) |
| `used_index_scan` | `bool` | SELECT-specific |
| `where_clause_count` | `size_t` | SELECT-specific |
| `has_order_by` | `bool` | SELECT-specific |
| `has_limit` | `bool` | SELECT-specific |
| `result_row_count` | `int32_t` | SELECT-specific |

**How It Works in DMLExecutor:**

1. Before each DML operation, `DMLExecutor` creates a `DMLEvent` and calls `NotifyBefore()`
2. If any observer returns `false`, the operation is blocked with an error message
3. After the operation completes, `NotifyAfter()` is called with duration and row count filled in
4. Both `LearningEngine` and `ImmuneSystem` are registered as observers

### 4.4 AIScheduler (`ai_scheduler.h` / `ai_scheduler.cpp`)

**Purpose:** Background task manager for periodic AI analysis.

**Class: `AIScheduler` (Singleton)**

```cpp
class AIScheduler {
    TaskId SchedulePeriodic(const std::string& name, uint32_t interval_ms,
                           std::function<void()> task);
    TaskId ScheduleOnce(const std::string& name, uint32_t delay_ms,
                       std::function<void()> task);
    void Cancel(TaskId id);
    size_t GetActiveTaskCount() const;
};
```

**Implementation:**
- Dedicated scheduler thread with 10ms sleep ticks
- Checks `running_` atomic flag every tick for clean shutdown
- Uses existing `ThreadPool` for task execution (thread-safe delegation)
- Tasks identified by `TaskId` (uint64_t)

### 4.5 AIManager (`ai_manager.h` / `ai_manager.cpp`)

**Purpose:** Top-level singleton coordinator for all AI subsystems.

**Class: `AIManager` (Singleton)**

```cpp
class AIManager {
    static AIManager& Instance();
    void Initialize(Catalog*, IBufferManager*, LogManager*, CheckpointManager*);
    void Shutdown();
    bool IsInitialized() const;
    LearningEngine* GetLearningEngine();
    ImmuneSystem* GetImmuneSystem();
    TemporalIndexManager* GetTemporalIndexManager();
    AIStatus GetStatus() const;
};
```

**Lifecycle:**
1. `Initialize()` is called in `ExecutionEngine` constructor
2. Creates all 3 subsystems in order: LearningEngine → ImmuneSystem → TemporalIndexManager
3. Registers `LearningEngine` and `ImmuneSystem` as DML observers
4. Starts all 3 subsystems (schedules periodic tasks)
5. `Shutdown()` stops all subsystems, unregisters observers, destroys in reverse order

---

## 5. Phase 1: Self-Learning Execution Engine

### 5.1 Overview

The Learning Engine uses a **UCB1 Multi-Armed Bandit** algorithm to learn which scan strategy (Sequential Scan vs Index Scan) is optimal for each query pattern. It learns from real execution feedback — no manual tuning required.

### 5.2 QueryFeatureExtractor (`query_features.h` / `query_features.cpp`)

**Purpose:** Extracts an 8-dimensional feature vector from a `SelectStatement`.

**Class: `QueryFeatureExtractor`**

| Feature | Type | Description |
|---------|------|-------------|
| `table_row_count_log` | double | log2(estimated row count) |
| `where_clause_count` | double | Number of WHERE conditions |
| `has_equality_predicate` | double | 1.0 if any WHERE uses `=` |
| `has_index_available` | double | 1.0 if index exists on first WHERE column |
| `selectivity_estimate` | double | Estimated fraction of rows matching |
| `column_count` | double | Number of selected columns |
| `has_order_by` | double | 1.0 if ORDER BY present |
| `has_limit` | double | 1.0 if LIMIT present |

**Methods:**
- `Extract(stmt, table_name, catalog)` → `QueryFeatures`
- `EstimateSelectivity(stmt)` → `double` (heuristic: 0.1 for equality, 0.33 for range)
- `EstimateRowCount(table_name, catalog)` → `uint64_t`
- `HasIndexForFirstPredicate(stmt, table_name, catalog)` → `bool`

### 5.3 UCB1Bandit (`bandit.h` / `bandit.cpp`)

**Purpose:** Multi-armed bandit for scan strategy selection with per-table contextual learning.

**Algorithm:**

```
UCB1 Selection:
    score(a) = Q(a) + c * sqrt(ln(N) / N_a)

    Where:
        Q(a) = average reward for arm a
        c = sqrt(2) = 1.414 (exploration constant)
        N = total pulls across all arms
        N_a = pulls for arm a

    Select: argmax_a [ score(a) ]

Reward Computation:
    reward = 1.0 / (1.0 + execution_time_ms / 100.0)

    Examples:
        10ms query  → 0.91 reward (very good)
        100ms query → 0.50 reward (average)
        1000ms query → 0.09 reward (poor)
```

**Class: `UCB1Bandit`**

```cpp
class UCB1Bandit {
    ScanStrategy SelectStrategy(const std::string& table_name);
    void RecordOutcome(ScanStrategy strategy, double execution_time_ms,
                       const std::string& table_name);
    std::vector<ArmStats> GetStats() const;
    bool HasSufficientData() const;
};
```

**Per-Table Contextual Learning:**
- Global statistics track overall arm performance
- Per-table statistics override globals after 10+ pulls per arm for that table
- Each table learns its own optimal strategy independently
- Fixed-point arithmetic (`total_reward_x10000`) avoids floating-point atomic issues

**Exploration Strategy:**
- First `MIN_SAMPLES_BEFORE_LEARNING` (30) queries: no recommendation (use existing behavior)
- Per-arm minimum `MIN_ARM_PULLS` (5) pulls before exploitation
- After sufficient data: UCB1 formula selects the best arm

### 5.4 LearningEngine (`learning_engine.h` / `learning_engine.cpp`)

**Purpose:** Orchestrator that connects the bandit algorithm to the DML execution path.

**Implements:** `IDMLObserver` (records feedback) + `IQueryOptimizer` (recommends strategy)

**Class: `LearningEngine`**

```cpp
class LearningEngine : public IDMLObserver, public IQueryOptimizer {
    // IDMLObserver — called after every SELECT
    void OnAfterDML(const DMLEvent& event) override;

    // IQueryOptimizer — consulted before scan strategy decision
    bool RecommendScanStrategy(const SelectStatement* stmt,
                               const std::string& table_name,
                               ScanStrategy& out_strategy) override;

    std::string GetSummary() const;
    std::vector<ArmStats> GetArmStats() const;
    uint64_t GetTotalQueriesObserved() const;
};
```

**Flow:**

```
DMLExecutor::Select()
    |
    +---> LearningEngine::RecommendScanStrategy()
    |         |
    |         +---> QueryFeatureExtractor::Extract()
    |         +---> UCB1Bandit::SelectStrategy(table_name)
    |         +---> Returns INDEX_SCAN or SEQUENTIAL_SCAN
    |
    +---> Execute query with recommended strategy
    |
    +---> DMLObserverRegistry::NotifyAfter(event)
              |
              +---> LearningEngine::OnAfterDML()
                        |
                        +---> UCB1Bandit::RecordOutcome(strategy, time)
                        +---> MetricsStore::Record(metric)
```

---

## 6. Phase 2: Immune System

### 6.1 Overview

The Immune System provides autonomous anomaly detection and self-healing. It monitors mutation patterns across all tables, detects anomalous behavior using Z-score analysis, and can automatically block operations or recover data using the Time Travel Engine.

### 6.2 MutationMonitor (`mutation_monitor.h` / `mutation_monitor.cpp`)

**Purpose:** Per-table rolling window of mutation events for rate calculation.

**Class: `MutationMonitor`**

```cpp
class MutationMonitor {
    void RecordMutation(const std::string& table_name, uint32_t row_count);
    double GetMutationRate(const std::string& table_name) const;      // mutations/sec
    std::vector<double> GetHistoricalRates(const std::string& table_name) const;
    std::vector<std::string> GetMonitoredTables() const;
};
```

**Implementation:**
- Per-table `std::deque<MutationEntry>` with `{timestamp_us, row_count}`
- Rolling window of `MUTATION_ROLLING_WINDOW_US` (10 minutes)
- `GetMutationRate()` counts mutations in last `MUTATION_RATE_WINDOW_US` (5 seconds)
- `GetHistoricalRates()` divides history into `IMMUNE_CHECK_INTERVAL_MS` intervals for Z-score computation
- Auto-prunes entries older than the rolling window

### 6.3 UserBehaviorProfiler (`user_profiler.h` / `user_profiler.cpp`)

**Purpose:** Per-user behavioral baselines for detecting compromised accounts.

**Class: `UserBehaviorProfiler`**

```cpp
class UserBehaviorProfiler {
    void RecordActivity(const std::string& user, const std::string& table_name,
                        bool is_mutation);
    double GetDeviationScore(const std::string& user) const;
    std::vector<UserProfile> GetAllProfiles() const;
};
```

**Deviation Score Computation:**
```
deviation = 0.6 * mutation_rate_deviation + 0.4 * table_access_deviation

mutation_rate_deviation:
    current_rate = mutations in last 5s
    history = rates over last 100 intervals
    z = (current_rate - mean(history)) / stddev(history)

table_access_deviation:
    unique_tables_now = tables accessed in current window
    if unique_tables_now > historical_max * 2 → high deviation
```

### 6.4 AnomalyDetector (`anomaly_detector.h` / `anomaly_detector.cpp`)

**Purpose:** Z-score computation on mutation rates with severity classification.

**Class: `AnomalyDetector`**

```cpp
class AnomalyDetector {
    std::vector<AnomalyReport> Analyze(const MutationMonitor& monitor) const;
    static double ComputeZScore(double value, const std::vector<double>& history);
};
```

**Z-Score Algorithm:**

```
z = (x - μ) / σ

Where:
    x = current mutation rate (mutations/sec in last check interval)
    μ = mean of last 100 intervals
    σ = standard deviation

Severity Classification:
    z < 2.0  → NONE (normal behavior)
    z ≥ 2.0  → LOW (log warning)
    z ≥ 3.0  → MEDIUM (block table mutations)
    z ≥ 4.0  → HIGH (auto-recover via TimeTravelEngine)
```

**AnomalyReport struct:**

| Field | Type | Description |
|-------|------|-------------|
| `table_name` | string | Affected table |
| `user` | string | Most active user |
| `severity` | AnomalySeverity | NONE/LOW/MEDIUM/HIGH |
| `z_score` | double | Computed Z-score |
| `current_rate` | double | Current mutations/sec |
| `mean_rate` | double | Historical mean |
| `std_dev` | double | Historical standard deviation |
| `timestamp_us` | uint64_t | When detected |
| `description` | string | Human-readable description |

### 6.5 ResponseEngine (`response_engine.h` / `response_engine.cpp`)

**Purpose:** Executes graduated responses based on anomaly severity.

**Class: `ResponseEngine`**

```cpp
class ResponseEngine {
    void Respond(const AnomalyReport& report);
    bool IsTableBlocked(const std::string& table_name) const;
    bool IsUserBlocked(const std::string& user) const;
    void UnblockTable(const std::string& table_name);
    void UnblockUser(const std::string& user);
};
```

**Response Ladder:**

| Severity | Action | Description |
|----------|--------|-------------|
| **LOW** | LOG_WARN | Log warning with Z-score and rates |
| **MEDIUM** | BLOCK | Block all mutations on the affected table + user |
| **HIGH** | AUTO-RECOVER | Block + trigger `TimeTravelEngine::RecoverTo()` to 60 seconds before the anomaly |

**Auto-Recovery Flow (HIGH severity):**
1. Block the table to prevent further damage
2. Calculate recovery timestamp: `anomaly_time - 60 seconds`
3. Call `TimeTravelEngine::RecoverTo(timestamp, db_name)`
4. If recovery succeeds, unblock the table
5. If recovery fails, keep the table blocked and log error

### 6.6 ImmuneSystem (`immune_system.h` / `immune_system.cpp`)

**Purpose:** Orchestrator that ties together all immune components.

**Implements:** `IDMLObserver`

**Class: `ImmuneSystem`**

```cpp
class ImmuneSystem : public IDMLObserver {
    // Called BEFORE every mutation — can block!
    bool OnBeforeDML(const DMLEvent& event) override;

    // Called AFTER every operation — records mutation data
    void OnAfterDML(const DMLEvent& event) override;

    void PeriodicAnalysis();  // Runs every IMMUNE_CHECK_INTERVAL_MS
    std::string GetSummary() const;
    std::vector<AnomalyReport> GetRecentAnomalies(size_t max) const;
};
```

**Periodic Analysis Pipeline:**

```
Every 1 second:
    1. AnomalyDetector::Analyze(mutation_monitor)
       → Returns list of AnomalyReport

    2. For each report with severity > NONE:
       → ResponseEngine::Respond(report)
       → Store in anomaly history

    3. Record anomaly metrics in MetricsStore
```

---

## 7. Phase 3: Intelligent Temporal Index Manager

### 7.1 Overview

The Temporal Index Manager makes time-travel queries faster by intelligently scheduling snapshots. It tracks which historical timestamps are queried most frequently, detects temporal hotspots using DBSCAN clustering, and identifies optimal snapshot placement using CUSUM change-point detection.

### 7.2 TemporalAccessTracker (`access_tracker.h` / `access_tracker.cpp`)

**Purpose:** Records which timestamps are queried in time-travel operations.

**Class: `TemporalAccessTracker`**

```cpp
class TemporalAccessTracker {
    void RecordAccess(const TemporalAccessEvent& event);
    std::vector<FrequencyBucket> GetFrequencyHistogram(uint64_t bucket_width_us) const;
    std::vector<uint64_t> GetHotTimestamps(size_t k) const;
    std::vector<TemporalAccessEvent> GetAllEvents() const;
    size_t GetTotalAccessCount() const;
};
```

**TemporalAccessEvent:**

| Field | Type | Description |
|-------|------|-------------|
| `queried_timestamp_us` | uint64_t | The historical timestamp being queried |
| `query_time_us` | uint64_t | When the query happened |
| `table_name` | string | Which table was queried |
| `db_name` | string | Which database |

**Implementation:**
- `std::deque` with `ACCESS_PATTERN_WINDOW_SIZE` (1000) max entries
- `GetFrequencyHistogram()` buckets queried timestamps into fixed-width bins
- `GetHotTimestamps(k)` returns top-K most frequently queried timestamps (rounded to nearest second)

### 7.3 HotspotDetector (`hotspot_detector.h` / `hotspot_detector.cpp`)

**Purpose:** Detects temporal hotspots using DBSCAN clustering and change points using CUSUM.

**Class: `HotspotDetector`**

```cpp
class HotspotDetector {
    std::vector<TemporalHotspot> DetectHotspots(
        const std::vector<TemporalAccessEvent>& events) const;
    std::vector<uint64_t> DetectChangePoints(
        const std::vector<double>& mutation_rates,
        const std::vector<uint64_t>& timestamps) const;
};
```

#### DBSCAN Clustering Algorithm (Simplified 1D)

```
Input: Sorted timestamps, epsilon (60s), min_points (5)

1. Walk through sorted timestamps
2. Group consecutive timestamps within epsilon distance
3. If group size >= min_points, it's a cluster
4. For each cluster, compute:
   - center = mean of timestamps
   - range = [min, max]
   - density = access_count / range_seconds
5. Sort clusters by density (highest first)
```

**TemporalHotspot struct:**

| Field | Type | Description |
|-------|------|-------------|
| `center_timestamp_us` | uint64_t | Center of the hotspot |
| `range_start_us` | uint64_t | Earliest timestamp in cluster |
| `range_end_us` | uint64_t | Latest timestamp in cluster |
| `access_count` | size_t | Number of queries in this hotspot |
| `density` | double | Queries per second in this range |

#### CUSUM Change-Point Detection Algorithm

```
Input: Time series of mutation rates

Parameters:
    threshold = 4.0 * sigma
    drift = 0.5 * sigma

Algorithm:
    s_pos = 0, s_neg = 0
    mean = average(values)

    For each value[i]:
        s_pos = max(0, s_pos + (value[i] - mean - drift))
        s_neg = max(0, s_neg + (mean - value[i] - drift))

        if s_pos > threshold OR s_neg > threshold:
            → Declare change point at index i
            → Reset s_pos = s_neg = 0
```

**Why CUSUM?** It's sensitive to sustained shifts in mean level, not just individual spikes. This makes it ideal for detecting when the pattern of time-travel queries fundamentally changes (e.g., users start querying a different time period).

### 7.4 SmartSnapshotScheduler (`snapshot_scheduler.h` / `snapshot_scheduler.cpp`)

**Purpose:** Decides when to trigger checkpoints based on hotspots and change points.

**Class: `SmartSnapshotScheduler`**

```cpp
class SmartSnapshotScheduler {
    void Evaluate(const std::vector<TemporalHotspot>& hotspots,
                  const std::vector<uint64_t>& change_points);
    std::vector<uint64_t> GetScheduledSnapshots() const;
    size_t GetTotalSnapshotsTriggered() const;
};
```

**Snapshot Decision Logic:**

```
Trigger a checkpoint if:
    1. Minimum 30 seconds since last snapshot (rate limiting)
    AND either:
    2a. A change point was detected within the last 5 minutes
    2b. A hotspot with density > 1.0 and access_count >= 10 exists

On trigger:
    → Call CheckpointManager::BeginCheckpoint()
    → Record SNAPSHOT_TRIGGERED in MetricsStore
    → Update scheduled_snapshots list from hotspot centers
```

### 7.5 WALRetentionManager (`retention_manager.h` / `retention_manager.cpp`)

**Purpose:** Adaptive WAL retention policy based on access patterns.

**Class: `WALRetentionManager`**

```cpp
class WALRetentionManager {
    RetentionPolicy ComputePolicy(const TemporalAccessTracker& tracker) const;
    void UpdatePolicy(const RetentionPolicy& policy);
    RetentionStats GetStats() const;
};
```

**Retention Policy:**
- **Hot zone** (24 hours): Full WAL fidelity — every operation preserved
- **Cold cutoff** (7 days default): After this, WAL can be pruned
- **Adaptive extension**: If users query data older than hot zone, cold cutoff extends to cover those timestamps plus a buffer

### 7.6 TemporalIndexManager (`temporal_index_manager.h` / `temporal_index_manager.cpp`)

**Purpose:** Orchestrator for the temporal intelligence pipeline.

**Class: `TemporalIndexManager`**

```cpp
class TemporalIndexManager {
    void OnTimeTravelQuery(const std::string& table_name,
                           uint64_t target_timestamp,
                           const std::string& db_name);
    void PeriodicAnalysis();
    std::string GetSummary() const;
    std::vector<TemporalHotspot> GetCurrentHotspots() const;
};
```

**Periodic Analysis Pipeline (every 30 seconds):**

```
1. Get all temporal access events from tracker
2. Detect hotspots via DBSCAN clustering
3. Get frequency histogram (1-minute buckets)
4. Detect change points via CUSUM
5. Update cached hotspots
6. Evaluate snapshot scheduling
7. Update WAL retention policy
```

---

## 8. Integration Architecture

### 8.1 DMLExecutor Integration

Every DML operation in `dml_executor.cpp` follows this pattern:

```cpp
// BEFORE operation
ai::DMLEvent ai_event;
ai_event.operation = ai::DMLOperation::INSERT; // or UPDATE, DELETE, SELECT
ai_event.table_name = stmt->table_name_;
ai_event.start_time_us = now();

// Check if Immune System blocks this operation
if (!ai::DMLObserverRegistry::Instance().NotifyBefore(ai_event)) {
    return ExecutionResult::Error("[IMMUNE] Operation blocked");
}

// ... execute the actual DML operation ...

// AFTER operation
ai_event.duration_us = now() - ai_event.start_time_us;
ai_event.rows_affected = count;
ai::DMLObserverRegistry::Instance().NotifyAfter(ai_event);
```

### 8.2 SELECT-Specific Integration

```cpp
// 1. Notify Temporal Index on time-travel queries
if (stmt->as_of_timestamp_ > 0) {
    ai_mgr.GetTemporalIndexManager()->OnTimeTravelQuery(
        table_name, as_of_timestamp, db_name);
}

// 2. Consult Learning Engine for scan strategy
ScanStrategy recommended;
if (learning_engine->RecommendScanStrategy(stmt, table_name, recommended)) {
    // Use AI recommendation
    use_index = (recommended == INDEX_SCAN && index_exists);
} else {
    // Fall back to original logic
}

// 3. After SELECT, report feedback
ai_event.used_index_scan = use_index;
ai_event.result_row_count = row_count;
DMLObserverRegistry::Instance().NotifyAfter(ai_event);
```

### 8.3 ExecutionEngine Lifecycle

```cpp
// Constructor
ExecutionEngine::ExecutionEngine(...) {
    // ... create executors ...
    ai::AIManager::Instance().Initialize(catalog_, bpm_, log_manager_, nullptr);
}

// Destructor
ExecutionEngine::~ExecutionEngine() {
    ai::AIManager::Instance().Shutdown();
    delete exec_ctx_;
}
```

---

## 9. SQL Commands

### 9.1 SHOW AI STATUS

Shows the status of all three AI subsystems.

```sql
SHOW AI STATUS;
-- Franco: WARENY ZAKA2 7ALAH;
```

**Output:**

| Component | Status | Details |
|-----------|--------|---------|
| Learning Engine | ACTIVE | 150 queries observed, 2 arms, SeqScan preferred |
| Immune System | ACTIVE | 3 anomalies detected, 0 blocked, 0 recoveries |
| Temporal Index | ACTIVE | 2 hotspots, 45 time-travel queries, 1 smart snapshot |
| Metrics Recorded | INFO | 1234 events |
| Scheduled Tasks | INFO | 3 tasks |

### 9.2 SHOW ANOMALIES

Shows recent anomaly detections from the Immune System.

```sql
SHOW ANOMALIES;
-- Franco: WARENY SHOZOOZ;
```

**Output:**

| Table | Severity | Z-Score | Current Rate | Mean Rate | Detected At |
|-------|----------|---------|-------------|-----------|------------|
| users | HIGH | 5.23 | 150.00 | 12.50 | 1706000000000 |
| orders | MEDIUM | 3.45 | 80.00 | 20.00 | 1706000001000 |

### 9.3 SHOW EXECUTION STATS

Shows the Learning Engine's bandit arm statistics.

```sql
SHOW EXECUTION STATS;
-- Franco: WARENY TANFEEZ E7SA2EYAT;
```

**Output:**

| Strategy | Pulls | Avg Reward | UCB Score |
|----------|-------|-----------|-----------|
| Sequential Scan | 85 | 0.7234 | 0.8901 |
| Index Scan | 65 | 0.8567 | 0.9234 |
| Total Queries Observed | 150 | | |

---

## 10. PlantUML Diagrams

### 10.1 System Component Diagram

```plantuml
@startuml ChronosDB_AI_Components
!theme plain

package "ChronosDB Core" {
    [ExecutionEngine] as EE
    [DMLExecutor] as DML
    [SystemExecutor] as SYS
    [Parser] as PARSER
    [BufferPoolManager] as BPM
    [LogManager] as LOG
    [CheckpointManager] as CP
    [TimeTravelEngine] as TT
    [Catalog] as CAT
}

package "AI Layer" {
    package "Phase 0: Foundation" {
        [AIManager] as AIM
        [MetricsStore] as MS
        [DMLObserverRegistry] as DOR
        [AIScheduler] as SCHED
    }

    package "Phase 1: Learning Engine" {
        [LearningEngine] as LE
        [UCB1Bandit] as UCB
        [QueryFeatureExtractor] as QFE
    }

    package "Phase 2: Immune System" {
        [ImmuneSystem] as IS
        [MutationMonitor] as MM
        [UserBehaviorProfiler] as UBP
        [AnomalyDetector] as AD
        [ResponseEngine] as RE
    }

    package "Phase 3: Temporal Index" {
        [TemporalIndexManager] as TIM
        [TemporalAccessTracker] as TAT
        [HotspotDetector] as HD
        [SmartSnapshotScheduler] as SSS
        [WALRetentionManager] as WRM
    }
}

EE --> DML : dispatches DML
EE --> SYS : dispatches SHOW
EE --> AIM : Initialize/Shutdown
DML --> DOR : NotifyBefore/After
DML --> LE : RecommendScanStrategy
DML --> TIM : OnTimeTravelQuery

AIM --> LE : owns
AIM --> IS : owns
AIM --> TIM : owns

LE --> UCB : selects strategy
LE --> QFE : extracts features
LE --> MS : records metrics
LE ..|> DOR : implements IDMLObserver

IS --> MM : monitors mutations
IS --> UBP : profiles users
IS --> AD : detects anomalies
IS --> RE : executes responses
IS --> MS : records metrics
IS ..|> DOR : implements IDMLObserver
RE --> TT : auto-recover (HIGH)

TIM --> TAT : tracks access
TIM --> HD : detects hotspots
TIM --> SSS : schedules snapshots
TIM --> WRM : manages retention
TIM --> MS : records metrics
SSS --> CP : BeginCheckpoint

SCHED --> IS : periodic analysis
SCHED --> TIM : periodic analysis

SYS --> AIM : SHOW AI STATUS
SYS --> IS : SHOW ANOMALIES
SYS --> LE : SHOW EXECUTION STATS

@enduml
```

### 10.2 Class Diagram - AI Foundation

```plantuml
@startuml AI_Foundation_Classes

abstract class IDMLObserver {
    + OnBeforeDML(event: DMLEvent): bool
    + OnAfterDML(event: DMLEvent): void
}

class DMLObserverRegistry {
    - observers_: vector<IDMLObserver*>
    - mutex_: shared_mutex
    + {static} Instance(): DMLObserverRegistry&
    + Register(observer: IDMLObserver*): void
    + Unregister(observer: IDMLObserver*): void
    + NotifyBefore(event: DMLEvent): bool
    + NotifyAfter(event: DMLEvent): void
}

class MetricsStore {
    - buffer_: array<MetricEvent, 8192>
    - write_index_: atomic<uint64_t>
    - read_mutex_: shared_mutex
    + {static} Instance(): MetricsStore&
    + Record(event: MetricEvent): void
    + Query(type, since_us): vector<MetricEvent>
    + CountEvents(type, since_us): size_t
    + GetMutationCount(table, since_us): uint64_t
}

class AIScheduler {
    - tasks_: vector<ScheduledTask>
    - running_: atomic<bool>
    - thread_pool_: ThreadPool
    + {static} Instance(): AIScheduler&
    + SchedulePeriodic(name, interval, task): TaskId
    + Cancel(id: TaskId): void
}

class AIManager {
    - learning_engine_: unique_ptr<LearningEngine>
    - immune_system_: unique_ptr<ImmuneSystem>
    - temporal_index_mgr_: unique_ptr<TemporalIndexManager>
    - initialized_: atomic<bool>
    + {static} Instance(): AIManager&
    + Initialize(...): void
    + Shutdown(): void
    + GetStatus(): AIStatus
}

DMLObserverRegistry "1" o-- "*" IDMLObserver

@enduml
```

### 10.3 Class Diagram - Learning Engine

```plantuml
@startuml Learning_Engine_Classes

abstract class IQueryOptimizer {
    + RecommendScanStrategy(stmt, table, out_strategy): bool
}

class LearningEngine {
    - catalog_: Catalog*
    - feature_extractor_: unique_ptr<QueryFeatureExtractor>
    - bandit_: unique_ptr<UCB1Bandit>
    - total_queries_: atomic<uint64_t>
    - active_: atomic<bool>
    + OnAfterDML(event: DMLEvent): void
    + RecommendScanStrategy(stmt, table, out): bool
    + GetSummary(): string
    + GetArmStats(): vector<ArmStats>
    + Start(): void
    + Stop(): void
}

class QueryFeatureExtractor {
    + Extract(stmt, table, catalog): QueryFeatures
    + EstimateSelectivity(stmt): double
    + HasIndexForFirstPredicate(stmt, table, catalog): bool
}

class UCB1Bandit {
    - global_pulls_: atomic<uint64_t>[2]
    - global_reward_x10000_: atomic<uint64_t>[2]
    - table_stats_: map<string, TableContext>
    - table_mutex_: mutex
    + SelectStrategy(table_name): ScanStrategy
    + RecordOutcome(strategy, time_ms, table): void
    + GetStats(): vector<ArmStats>
    + HasSufficientData(): bool
    - ComputeUCBScore(arm): double
    - ComputeReward(time_ms): double
}

struct QueryFeatures {
    + table_row_count_log: double
    + where_clause_count: double
    + has_equality_predicate: double
    + has_index_available: double
    + selectivity_estimate: double
    + column_count: double
    + has_order_by: double
    + has_limit: double
}

enum ScanStrategy {
    SEQUENTIAL_SCAN
    INDEX_SCAN
}

LearningEngine --|> IDMLObserver
LearningEngine --|> IQueryOptimizer
LearningEngine --> QueryFeatureExtractor
LearningEngine --> UCB1Bandit
UCB1Bandit --> ScanStrategy

@enduml
```

### 10.4 Class Diagram - Immune System

```plantuml
@startuml Immune_System_Classes

class ImmuneSystem {
    - mutation_monitor_: unique_ptr<MutationMonitor>
    - user_profiler_: unique_ptr<UserBehaviorProfiler>
    - anomaly_detector_: unique_ptr<AnomalyDetector>
    - response_engine_: unique_ptr<ResponseEngine>
    - periodic_task_id_: TaskId
    - active_: atomic<bool>
    + OnBeforeDML(event): bool
    + OnAfterDML(event): void
    + PeriodicAnalysis(): void
    + GetSummary(): string
    + GetRecentAnomalies(max): vector<AnomalyReport>
}

class MutationMonitor {
    - tables_: map<string, deque<MutationEntry>>
    - mutex_: shared_mutex
    + RecordMutation(table, row_count): void
    + GetMutationRate(table): double
    + GetHistoricalRates(table): vector<double>
    + GetMonitoredTables(): vector<string>
}

class UserBehaviorProfiler {
    - users_: map<string, UserHistory>
    - mutex_: shared_mutex
    + RecordActivity(user, table, is_mutation): void
    + GetDeviationScore(user): double
    + GetAllProfiles(): vector<UserProfile>
}

class AnomalyDetector {
    + Analyze(monitor: MutationMonitor): vector<AnomalyReport>
    + {static} ComputeZScore(value, history): double
}

class ResponseEngine {
    - blocked_tables_: unordered_set<string>
    - blocked_users_: unordered_set<string>
    - log_manager_: LogManager*
    - catalog_: Catalog*
    - bpm_: IBufferManager*
    - checkpoint_mgr_: CheckpointManager*
    + Respond(report: AnomalyReport): void
    + IsTableBlocked(table): bool
    + IsUserBlocked(user): bool
    + UnblockTable(table): void
}

enum AnomalySeverity {
    NONE
    LOW
    MEDIUM
    HIGH
}

ImmuneSystem --|> IDMLObserver
ImmuneSystem --> MutationMonitor
ImmuneSystem --> UserBehaviorProfiler
ImmuneSystem --> AnomalyDetector
ImmuneSystem --> ResponseEngine
AnomalyDetector --> AnomalySeverity

@enduml
```

### 10.5 Class Diagram - Temporal Index Manager

```plantuml
@startuml Temporal_Index_Classes

class TemporalIndexManager {
    - log_manager_: LogManager*
    - catalog_: Catalog*
    - bpm_: IBufferManager*
    - checkpoint_mgr_: CheckpointManager*
    - access_tracker_: unique_ptr<TemporalAccessTracker>
    - hotspot_detector_: unique_ptr<HotspotDetector>
    - snapshot_scheduler_: unique_ptr<SmartSnapshotScheduler>
    - retention_manager_: unique_ptr<WALRetentionManager>
    - current_hotspots_: vector<TemporalHotspot>
    - periodic_task_id_: TaskId
    + OnTimeTravelQuery(table, timestamp, db): void
    + PeriodicAnalysis(): void
    + GetSummary(): string
    + GetCurrentHotspots(): vector<TemporalHotspot>
}

class TemporalAccessTracker {
    - events_: deque<TemporalAccessEvent>
    - mutex_: shared_mutex
    + RecordAccess(event): void
    + GetFrequencyHistogram(bucket_width): vector<FrequencyBucket>
    + GetHotTimestamps(k): vector<uint64_t>
    + GetAllEvents(): vector<TemporalAccessEvent>
}

class HotspotDetector {
    + DetectHotspots(events): vector<TemporalHotspot>
    + DetectChangePoints(rates, timestamps): vector<uint64_t>
    - ClusterTimestamps(timestamps, epsilon, min_pts): vector<vector<size_t>>
    - CUSUMChangePoints(values, threshold, drift): vector<size_t>
}

class SmartSnapshotScheduler {
    - checkpoint_mgr_: CheckpointManager*
    - last_snapshot_time_us_: uint64_t
    - total_snapshots_: size_t
    - scheduled_snapshots_: vector<uint64_t>
    + Evaluate(hotspots, change_points): void
    + GetScheduledSnapshots(): vector<uint64_t>
    - ShouldSnapshot(now_us): bool
}

class WALRetentionManager {
    - log_manager_: LogManager*
    - current_stats_: RetentionStats
    + ComputePolicy(tracker): RetentionPolicy
    + UpdatePolicy(policy): void
    + GetStats(): RetentionStats
}

TemporalIndexManager --> TemporalAccessTracker
TemporalIndexManager --> HotspotDetector
TemporalIndexManager --> SmartSnapshotScheduler
TemporalIndexManager --> WALRetentionManager

@enduml
```

### 10.6 Sequence Diagram - SELECT with AI

```plantuml
@startuml SELECT_AI_Sequence

actor User
participant DMLExecutor
participant LearningEngine
participant UCB1Bandit
participant DMLObserverRegistry
participant MetricsStore

User -> DMLExecutor: SELECT * FROM users WHERE id = 5;

DMLExecutor -> LearningEngine: RecommendScanStrategy(stmt, "users")
LearningEngine -> UCB1Bandit: SelectStrategy("users")
UCB1Bandit --> LearningEngine: INDEX_SCAN
LearningEngine --> DMLExecutor: true, INDEX_SCAN

DMLExecutor -> DMLExecutor: Execute with IndexScanExecutor

DMLExecutor -> DMLObserverRegistry: NotifyAfter(event)
DMLObserverRegistry -> LearningEngine: OnAfterDML(event)
LearningEngine -> UCB1Bandit: RecordOutcome(INDEX_SCAN, 5ms, "users")
UCB1Bandit -> UCB1Bandit: Update reward: 1/(1+5/100) = 0.95
LearningEngine -> MetricsStore: Record(SELECT metric)
DMLObserverRegistry --> DMLExecutor: done

DMLExecutor --> User: ResultSet (1 row)

@enduml
```

### 10.7 Sequence Diagram - Anomaly Detection & Auto-Recovery

```plantuml
@startuml Anomaly_Recovery_Sequence

actor Attacker
participant DMLExecutor
participant DMLObserverRegistry
participant ImmuneSystem
participant MutationMonitor
participant AnomalyDetector
participant ResponseEngine
participant TimeTravelEngine

== Phase 1: Normal Writes ==
Attacker -> DMLExecutor: DELETE FROM users WHERE id > 0;
DMLExecutor -> DMLObserverRegistry: NotifyBefore(DELETE)
DMLObserverRegistry -> ImmuneSystem: OnBeforeDML(event)
ImmuneSystem --> DMLObserverRegistry: true (allowed)
DMLExecutor -> DMLExecutor: Execute DELETE
DMLExecutor -> DMLObserverRegistry: NotifyAfter(DELETE, 500 rows)
DMLObserverRegistry -> ImmuneSystem: OnAfterDML(event)
ImmuneSystem -> MutationMonitor: RecordMutation("users", 500)

== Phase 2: Periodic Analysis Detects Anomaly ==
ImmuneSystem -> AnomalyDetector: Analyze(mutation_monitor)
AnomalyDetector -> MutationMonitor: GetMutationRate("users") = 500/s
AnomalyDetector -> MutationMonitor: GetHistoricalRates("users") = [2,3,1,2]
AnomalyDetector -> AnomalyDetector: Z-score = (500 - 2) / 0.8 = 622.5
AnomalyDetector --> ImmuneSystem: AnomalyReport(HIGH, z=622.5)

ImmuneSystem -> ResponseEngine: Respond(report)
ResponseEngine -> ResponseEngine: BlockTable("users")
ResponseEngine -> TimeTravelEngine: RecoverTo(now - 60s, db)
TimeTravelEngine --> ResponseEngine: success
ResponseEngine -> ResponseEngine: UnblockTable("users")

== Phase 3: Subsequent Writes Blocked (if still blocked) ==
Attacker -> DMLExecutor: DELETE FROM users WHERE id > 0;
DMLExecutor -> DMLObserverRegistry: NotifyBefore(DELETE)
DMLObserverRegistry -> ImmuneSystem: OnBeforeDML(event)
ImmuneSystem -> ResponseEngine: IsTableBlocked("users")
ResponseEngine --> ImmuneSystem: true
ImmuneSystem --> DMLObserverRegistry: false (BLOCKED)
DMLExecutor --> Attacker: ERROR: [IMMUNE] DELETE blocked

@enduml
```

---

## 11. File Inventory

### New Files (35 total)

#### Phase 0: Shared Foundation (10 files)

| File | Lines | Purpose |
|------|-------|---------|
| `src/include/ai/ai_config.h` | ~60 | All AI constants |
| `src/include/ai/metrics_store.h` | ~80 | MetricsStore header |
| `src/ai/metrics_store.cpp` | ~110 | MetricsStore implementation |
| `src/include/ai/dml_observer.h` | ~90 | Observer interface + registry |
| `src/ai/dml_observer.cpp` | ~55 | Observer registry implementation |
| `src/include/ai/ai_scheduler.h` | ~65 | Background task scheduler |
| `src/ai/ai_scheduler.cpp` | ~100 | Scheduler implementation |
| `src/include/ai/ai_manager.h` | ~75 | Top-level coordinator |
| `src/ai/ai_manager.cpp` | ~100 | Manager implementation |

#### Phase 1: Learning Engine (6 files)

| File | Lines | Purpose |
|------|-------|---------|
| `src/include/ai/learning/query_features.h` | ~50 | Feature extractor |
| `src/ai/learning/query_features.cpp` | ~80 | Feature extraction |
| `src/include/ai/learning/bandit.h` | ~70 | UCB1 Bandit |
| `src/ai/learning/bandit.cpp` | ~130 | Bandit implementation |
| `src/include/ai/learning/learning_engine.h` | ~80 | Learning orchestrator |
| `src/ai/learning/learning_engine.cpp` | ~90 | Learning implementation |

#### Phase 2: Immune System (10 files)

| File | Lines | Purpose |
|------|-------|---------|
| `src/include/ai/immune/mutation_monitor.h` | ~60 | Mutation tracking |
| `src/ai/immune/mutation_monitor.cpp` | ~100 | Monitor implementation |
| `src/include/ai/immune/user_profiler.h` | ~60 | User profiling |
| `src/ai/immune/user_profiler.cpp` | ~140 | Profiler implementation |
| `src/include/ai/immune/anomaly_detector.h` | ~55 | Anomaly detection |
| `src/ai/immune/anomaly_detector.cpp` | ~80 | Detector implementation |
| `src/include/ai/immune/response_engine.h` | ~60 | Response execution |
| `src/ai/immune/response_engine.cpp` | ~130 | Response implementation |
| `src/include/ai/immune/immune_system.h` | ~65 | Immune orchestrator |
| `src/ai/immune/immune_system.cpp` | ~120 | Immune implementation |

#### Phase 3: Temporal Index (10 files)

| File | Lines | Purpose |
|------|-------|---------|
| `src/include/ai/temporal/access_tracker.h` | ~55 | Access tracking |
| `src/ai/temporal/access_tracker.cpp` | ~105 | Tracker implementation |
| `src/include/ai/temporal/hotspot_detector.h` | ~55 | Hotspot detection |
| `src/ai/temporal/hotspot_detector.cpp` | ~170 | Detector implementation |
| `src/include/ai/temporal/snapshot_scheduler.h` | ~50 | Snapshot scheduling |
| `src/ai/temporal/snapshot_scheduler.cpp` | ~100 | Scheduler implementation |
| `src/include/ai/temporal/retention_manager.h` | ~50 | WAL retention |
| `src/ai/temporal/retention_manager.cpp` | ~65 | Retention implementation |
| `src/include/ai/temporal/temporal_index_manager.h` | ~70 | Temporal orchestrator |
| `src/ai/temporal/temporal_index_manager.cpp` | ~140 | Manager implementation |

### Modified Files (9 total)

| File | Changes | Purpose |
|------|---------|---------|
| `src/include/parser/token.h` | +4 tokens | AI, ANOMALIES, EXECUTION, STATS |
| `src/parser/lexer.cpp` | +12 keywords | Franco-Arabic + English mappings |
| `src/include/parser/statement.h` | +3 types, +3 classes | SHOW_AI_STATUS, SHOW_ANOMALIES, SHOW_EXECUTION_STATS |
| `src/parser/parser.cpp` | +18 lines | SHOW AI STATUS / ANOMALIES / EXECUTION STATS parsing |
| `src/execution/execution_engine.cpp` | +15 lines | AI init/shutdown + 3 dispatch entries |
| `src/include/execution/system_executor.h` | +6 lines | 3 new method declarations |
| `src/execution/executors/system_executor.cpp` | +100 lines | SHOW AI STATUS/ANOMALIES/EXECUTION STATS handlers |
| `src/execution/executors/dml_executor.cpp` | +80 lines | Observer hooks in all 4 DML methods + AI consultation |
| `src/include/common/logger.h` | +1 line | Added missing `<iostream>` include |

---

## 12. Algorithm Details

### 12.1 UCB1 Multi-Armed Bandit

**Problem:** Choose between Sequential Scan and Index Scan for each query without knowing in advance which is faster.

**Solution:** Treat each scan strategy as an "arm" of a slot machine. Use UCB1 to balance:
- **Exploitation**: Use the strategy that has performed best so far
- **Exploration**: Occasionally try the other strategy to gather more data

**Mathematical Formulation:**

```
Select arm a* = argmax_a [ Q̂(a) + c · √(ln N / N_a) ]

Where:
    Q̂(a) = estimated value of arm a = total_reward(a) / N_a
    c = exploration constant = √2 ≈ 1.414
    N = total number of pulls across all arms
    N_a = number of times arm a has been pulled

The term c · √(ln N / N_a) is the "confidence bonus" — it's large for
rarely-tried arms, encouraging exploration.
```

**Reward Function:**

```
reward(time_ms) = 1.0 / (1.0 + time_ms / 100.0)

Properties:
    - Always in range (0, 1]
    - 0ms → reward = 1.0 (perfect)
    - 100ms → reward = 0.5 (average)
    - ∞ms → reward → 0 (terrible)
    - Smooth, differentiable, bounded
```

### 12.2 Z-Score Anomaly Detection

**Problem:** Detect when a table's mutation rate is abnormally high (potential attack or bug).

**Solution:** Compare current rate against historical baseline using Z-score.

```
z = (x - μ) / σ

Statistical interpretation:
    z < 2.0: Within normal variation (95.4% of data)
    z ≥ 2.0: Outside 2σ — unusual (4.6% chance of false positive)
    z ≥ 3.0: Outside 3σ — very unusual (0.3% chance)
    z ≥ 4.0: Outside 4σ — almost certainly anomalous (0.006% chance)
```

### 12.3 DBSCAN Clustering (Simplified 1D)

**Problem:** Find temporal hotspots — time ranges that are frequently queried.

**Solution:** Simplified DBSCAN on sorted 1D timestamp data.

```
Standard DBSCAN: O(n²) in general, but for sorted 1D data: O(n)

Algorithm:
    1. Sort queried timestamps
    2. Initialize current_cluster = [first_point]
    3. For each subsequent point:
       - If distance to previous point ≤ epsilon (60s):
         Add to current cluster
       - Else:
         If current_cluster.size ≥ min_points (5):
           Save cluster as hotspot
         Start new cluster
    4. Check last cluster
```

### 12.4 CUSUM Change-Point Detection

**Problem:** Detect when the pattern of time-travel queries fundamentally shifts.

**Solution:** Cumulative Sum (CUSUM) algorithm — sensitive to sustained mean shifts.

```
CUSUM Algorithm:
    Initialize: S⁺ = 0, S⁻ = 0
    For each observation x_i:
        S⁺ = max(0, S⁺ + (x_i - μ - k))    // Detect upward shift
        S⁻ = max(0, S⁻ + (μ - x_i - k))    // Detect downward shift

        If S⁺ > h or S⁻ > h:
            Declare change point
            Reset S⁺ = S⁻ = 0

Parameters:
    μ = mean of the time series
    k = drift parameter = 0.5σ (allowable slack)
    h = threshold = 4.0σ (detection sensitivity)
```

---

*Documentation generated for ChronosDB AI Layer v1.0*
*ChronosDB Team - 2026*
