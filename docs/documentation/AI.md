ChronosDB AI Layer - Implementation Plan
Context
ChronosDB needs an integrated AI layer with three systems: Self-Learning Execution Engine, Immune System, and Intelligent Temporal Index Manager. These are not bolted-on features — they share infrastructure, feed each other data, and are embedded in the database kernel. The goal is to impress an AI-specialized professor and make ChronosDB a genuinely AI-native temporal database.

Architecture

ExecutionEngine
|
DMLExecutor ──[Observer Hook]──> DMLObserverRegistry
|                                |
|                    +-----------+-----------+
|                    |                       |
|              ImmuneSystem           LearningEngine
|              (Phase 2)              (Phase 1)
|                    |                       |
|                    +----------+------------+
|                               |
|                       MetricsStore (shared)
|                               |
|                    TemporalIndexManager
|                         (Phase 3)
|
+──[Strategy Hook]──> LearningEngine::RecommendScanStrategy()
+──[TimeTravel Hook]──> TemporalIndexManager::OnTimeTravelQuery()
Design: Observer pattern for DML hooks, Strategy pattern for scan selection, Singleton for shared infrastructure. AI never blocks the hot path unless Immune System escalates to BLOCK/RECOVER severity.

Phase 0: Shared Foundation
New Files
File	Class	Purpose
src/include/ai/ai_config.h	Constants	All AI constants (thresholds, intervals, sizes)
src/include/ai/metrics_store.h + src/ai/metrics_store.cpp	MetricsStore	Thread-safe ring buffer for operation metrics. Lock-free writes via atomic index. Shared by all 3 systems
src/include/ai/dml_observer.h + src/ai/dml_observer.cpp	IDMLObserver, DMLObserverRegistry	Observer interface + registry. DMLExecutor calls NotifyBefore()/NotifyAfter(). Both ImmuneSystem and LearningEngine implement IDMLObserver
src/include/ai/ai_scheduler.h + src/ai/ai_scheduler.cpp	AIScheduler	Periodic background task manager. Uses existing ThreadPool. Follows AdaptationLoop() pattern (sleep in 100ms ticks, check running_ atomic)
src/include/ai/ai_manager.h + src/ai/ai_manager.cpp	AIManager	Top-level singleton coordinator. Owns all 3 AI subsystems. Entry point for SHOW AI STATUS
Key Interfaces

// IDMLObserver - both ImmuneSystem and LearningEngine implement this
class IDMLObserver {
virtual bool OnBeforeDML(const DMLEvent& event) { return true; } // false = block
virtual void OnAfterDML(const DMLEvent& event) {}
};

// IQueryOptimizer - LearningEngine implements this
class IQueryOptimizer {
virtual bool RecommendScanStrategy(const SelectStatement* stmt,
const std::string& table_name,
ScanStrategy& out_strategy) = 0;
};
Existing File Changes (Phase 0)
src/execution/executors/dml_executor.cpp - Add #include "ai/dml_observer.h" and observer notification calls in Insert/Update/Delete/Select methods. ~40 lines added across 4 methods.

src/execution/execution_engine.cpp - Add ai::AIManager::Instance().Initialize(...) in constructor, .Shutdown() in destructor. ~6 lines.

Phase 1: Self-Learning Execution Engine
New Files
File	Class	Purpose
src/include/ai/learning/query_features.h + src/ai/learning/query_features.cpp	QueryFeatureExtractor	Extracts 8-dimensional feature vector from SelectStatement: table size (log), WHERE count, has equality predicate, has index available, selectivity estimate, column count, has ORDER BY, has LIMIT
src/include/ai/learning/bandit.h + src/ai/learning/bandit.cpp	UCB1Bandit	Multi-armed bandit for scan strategy selection. Two arms: SeqScan, IndexScan. Tracks per-arm pull counts and rewards using atomics. Per-table contextual tracking
src/include/ai/learning/learning_engine.h + src/ai/learning/learning_engine.cpp	LearningEngine	Orchestrator. Implements IDMLObserver (records feedback) and IQueryOptimizer (recommends strategy). Registers with DMLObserverRegistry
UCB1 Algorithm

Selection: argmax_a [ Q(a) + c * sqrt(ln(N) / N_a) ]
Q(a) = average reward for arm a
c = sqrt(2) = 1.414
N = total pulls, N_a = pulls for arm a

Reward: 1.0 / (1.0 + execution_time_ms / 100.0)
Normalizes to (0, 1]. 10ms query -> 0.91 reward, 100ms -> 0.5, 1000ms -> 0.09

Exploration: First 30 queries = no recommendation (existing behavior).
Per-arm minimum 5 pulls before exploitation.
Contextual: Per-table Q(a) overrides global Q(a) after 10+ pulls per arm for that table.
Integration Point
In DMLExecutor::Select(), before the existing index availability check (~line 248):


// Consult AI for scan strategy
ai::ScanStrategy recommended;
if (learning_engine->RecommendScanStrategy(stmt, table_name, recommended)) {
use_index = (recommended == ai::ScanStrategy::INDEX_SCAN && index_exists);
}
After SELECT completes, notify observers with execution duration and strategy used.

SQL Command: SHOW EXECUTION STATS;
Shows per-arm statistics: pull count, average time, UCB score.

Phase 2: Immune System
New Files
File	Class	Purpose
src/include/ai/immune/mutation_monitor.h + src/ai/immune/mutation_monitor.cpp	MutationMonitor	Rolling window per-table mutation tracking. Deque of (timestamp, row_count) pairs. Methods: GetMutationRate(), GetHistoricalRates()
src/include/ai/immune/user_profiler.h + src/ai/immune/user_profiler.cpp	UserBehaviorProfiler	Per-user behavioral baselines from SessionContext. Tracks mutation rate, query rate, table access distribution. Returns deviation score
src/include/ai/immune/anomaly_detector.h + src/ai/immune/anomaly_detector.cpp	AnomalyDetector	Z-score computation on mutation rates. Severity classification: NONE/LOW/MEDIUM/HIGH. Maintains anomaly history
src/include/ai/immune/response_engine.h + src/ai/immune/response_engine.cpp	ResponseEngine	Executes responses: LOW=LOG_WARN, MEDIUM=block table/user mutations, HIGH=auto-recover via TimeTravelEngine::RecoverTo() (timestamp = 60s before anomaly). Maintains blocked tables/users sets
src/include/ai/immune/immune_system.h + src/ai/immune/immune_system.cpp	ImmuneSystem	Orchestrator. Implements IDMLObserver. OnBeforeDML() checks blocked tables/users (returns false to block). OnAfterDML() records mutations. Periodic analysis via AIScheduler
Z-Score Anomaly Detection

z = (x - mu) / sigma
x = current mutation rate (mutations/sec in last check interval)
mu = mean of last 100 intervals
sigma = standard deviation

Severity:
z < 2.0  -> NONE (normal)
z >= 2.0 -> LOW (log warning)
z >= 3.0 -> MEDIUM (block table mutations)
z >= 4.0 -> HIGH (auto-recover via TimeTravelEngine)
Integration Points
OnBeforeDML() in Insert/Update/Delete: check if table/user is blocked. Return ExecutionResult::Error("[IMMUNE] Operation blocked...") if blocked.
OnAfterDML() in all DML: record mutation event.
Periodic analysis (every 1s via AIScheduler): run AnomalyDetector::Analyze(), feed results to ResponseEngine.
SQL Command: SHOW ANOMALIES;
Shows recent anomaly detections: table, user, severity, z-score, rates, timestamp.

Phase 3: Intelligent Temporal Index Manager
New Files
File	Class	Purpose
src/include/ai/temporal/access_tracker.h + src/ai/temporal/access_tracker.cpp	TemporalAccessTracker	Records which timestamps are queried in time-travel ops. Provides frequency histograms and top-K hot timestamps
src/include/ai/temporal/hotspot_detector.h + src/ai/temporal/hotspot_detector.cpp	HotspotDetector	Simplified DBSCAN clustering on queried timestamps (epsilon=60s, minPts=5). CUSUM change-point detection on mutation rate time series
src/include/ai/temporal/snapshot_scheduler.h + src/ai/temporal/snapshot_scheduler.cpp	SmartSnapshotScheduler	Decides when to trigger checkpoints based on hotspots and change points. Interfaces with CheckpointManager
src/include/ai/temporal/retention_manager.h + src/ai/temporal/retention_manager.cpp	WALRetentionManager	Adaptive WAL pruning: hot periods = full fidelity, cold periods = compressed/pruned
src/include/ai/temporal/temporal_index_manager.h + src/ai/temporal/temporal_index_manager.cpp	TemporalIndexManager	Orchestrator. Called on every time-travel query. Periodic analysis via AIScheduler. Triggers prefetching for predicted temporal queries
Algorithms
DBSCAN clustering: Sort queried timestamps, group those within 60s of each other if >= 5 points in group. Merge overlapping clusters. Each cluster = a temporal hotspot.

CUSUM change-point detection: Running cumulative sum of (rate[i] - mean - k) where k=sigma/2. When sum exceeds 4*sigma, declare change point. These are optimal snapshot moments.

Sequence prediction: Simple Markov chain — if user queries time T, predict next query is T-interval (same spacing). Prefetch that snapshot into buffer pool.

Integration Point
In DMLExecutor::Select(), inside the if (stmt->as_of_timestamp_ > 0) block:


ai_mgr->GetTemporalIndexManager()->OnTimeTravelQuery(table_name, as_of_timestamp, db_name);
Parser & SQL Extensions (All Phases)
Token additions (src/include/parser/token.h)

AI, ANOMALIES, EXECUTION, STATS  // 4 new enum values
Lexer additions (src/parser/lexer.cpp)

{"AI", TokenType::AI}, {"ZAKA2", TokenType::AI},
{"ANOMALIES", TokenType::ANOMALIES}, {"SHOZOOZ", TokenType::ANOMALIES},
{"EXECUTION", TokenType::EXECUTION}, {"TANFEEZ", TokenType::EXECUTION},
{"STATS", TokenType::STATS}, {"E7SA2EYAT", TokenType::STATS}
Statement additions (src/include/parser/statement.h)

// 3 new StatementType enum values: SHOW_AI_STATUS, SHOW_ANOMALIES, SHOW_EXECUTION_STATS
// 3 new Statement subclasses (trivial, no fields)
Parser additions (src/parser/parser.cpp)
3 new else if branches inside the SHOW parsing block. ~25 lines.

Dispatch map additions (src/execution/execution_engine.cpp)
3 new entries routing to system_executor_->ShowAIStatus/ShowAnomalies/ShowExecutionStats.

System executor additions (src/execution/executors/system_executor.cpp)
3 new methods querying AIManager::Instance() and formatting results into ResultSet.

File Inventory Summary
New files: 18 headers + 17 implementations + 5 test files = 40 files
Modified existing files: 9 files with minimal, focused changes
Estimated new code: ~4,200 lines
Estimated changes to existing code: ~150 lines across 9 files

Implementation Order
Phase 0 (Foundation): ai_config, MetricsStore, DMLObserverRegistry, AIScheduler, AIManager + DMLExecutor hooks
Phase 1 (Learning Engine): QueryFeatureExtractor, UCB1Bandit, LearningEngine + SHOW EXECUTION STATS
Phase 2 (Immune System): MutationMonitor, UserBehaviorProfiler, AnomalyDetector, ResponseEngine, ImmuneSystem + SHOW ANOMALIES
Phase 3 (Temporal Index): AccessTracker, HotspotDetector, SnapshotScheduler, RetentionManager, TemporalIndexManager
Integration: SHOW AI STATUS, integration tests, stress tests
Verification
Build: cmake -G Ninja -DCMAKE_BUILD_TYPE=Release .. && cmake --build . must succeed
Tests: ctest -R ComprehensiveTestSuite --output-on-failure must pass all existing + new AI tests
Manual testing:
Start server, connect with shell
Run SHOW AI STATUS; - all 3 systems show ACTIVE
Run many INSERT/SELECT queries, then SHOW EXECUTION STATS; - shows learned strategy preferences
Rapid-fire DELETE on a table, then SHOW ANOMALIES; - shows detected anomaly with z-score
Run several RECOVER TO queries, then SHOW AI STATUS; - shows detected temporal hotspots
Mass DELETE triggering HIGH severity - verify auto-recovery restores data