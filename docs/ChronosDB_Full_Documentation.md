# ChronosDB - Complete System Technical Documentation

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [System Architecture](#2-system-architecture)
3. [Storage Layer](#3-storage-layer)
4. [Buffer Pool Management](#4-buffer-pool-management)
5. [Catalog & Metadata](#5-catalog--metadata)
6. [Parser & Lexer](#6-parser--lexer)
7. [Execution Engine](#7-execution-engine)
8. [Concurrency Control](#8-concurrency-control)
9. [Recovery & Write-Ahead Logging](#9-recovery--write-ahead-logging)
10. [Time Travel Engine](#10-time-travel-engine)
11. [Network Layer](#11-network-layer)
12. [Authentication & RBAC](#12-authentication--rbac)
13. [AI Layer - Self-Learning Execution Engine](#13-ai-layer---self-learning-execution-engine)
14. [AI Layer - Immune System](#14-ai-layer---immune-system)
15. [AI Layer - Temporal Index Manager](#15-ai-layer---temporal-index-manager)
16. [AI Shared Foundation](#16-ai-shared-foundation)
17. [Command-Line Interfaces](#17-command-line-interfaces)
18. [SQL Commands Reference](#18-sql-commands-reference)
19. [UML Diagrams](#19-uml-diagrams)
20. [File Inventory](#20-file-inventory)
21. [Algorithm Reference](#21-algorithm-reference)

---

## 1. Project Overview

**ChronosDB** is a high-performance, multi-protocol database management system written in C++20, designed from the ground up to support temporal data operations with built-in time-travel capabilities and an AI-native intelligence layer.

### Core Features

- **Role-Based Access Control (RBAC)** — 5 roles: SUPERADMIN, ADMIN, USER, READONLY, DENIED
- **Persistent Storage** — Crash recovery via ARIES-compliant Write-Ahead Logging
- **Time Travel** — Query data at any historical point using `SELECT ... AS OF`
- **CQL (Chronos Query Language)** — SQL with Franco-Arabic keyword alternatives
- **B+ Tree Indexes** — Fast ordered lookups with page-level locking
- **Buffer Pool Management** — 4 strategies including adaptive distributed partitioning
- **Transaction Support** — ACID with two-phase locking and deadlock detection
- **Multi-Protocol** — TEXT, JSON, and BINARY wire protocols
- **AI-Native Intelligence** — Self-learning query optimizer, anomaly detection, temporal index optimization
- **Multi-Database** — Isolated databases with per-database WAL and catalog

### Key Technologies

| Component | Technology |
|-----------|-----------|
| Language | C++20 |
| Build System | CMake + Ninja |
| Page Size | 4 KB |
| Default Buffer Pool | 65,536 pages (256 MB) |
| Network Port | 2501 |
| Protocol | `chronos://user:pass@host:port/db` |
| Platform | Windows + Linux |

### Query Execution Flow

```
Client (Shell / Network)
    │
    ▼
ConnectionHandler (Multi-protocol: TEXT, JSON, BINARY)
    │
    ▼
Parser (Lexer → Token Stream → AST)
    │
    ▼
ExecutionEngine (Dispatch Map Pattern)
    │
    ├──→ DDLExecutor      (CREATE, DROP, ALTER)
    ├──→ DMLExecutor      (INSERT, SELECT, UPDATE, DELETE)
    ├──→ SystemExecutor   (SHOW, WHOAMI, STATUS)
    ├──→ UserExecutor     (CREATE USER, ALTER USER)
    ├──→ DatabaseExecutor (CREATE DATABASE, USE)
    └──→ TransactionExecutor (BEGIN, COMMIT, ROLLBACK)
    │
    ▼
Storage Layer (BufferPoolManager → DiskManager → Pages)
    │
    ▼
Recovery Layer (LogManager → WAL → CheckpointManager)
```

---

## 2. System Architecture

### Design Patterns

| Pattern | Where Used | Purpose |
|---------|-----------|---------|
| **Factory** | `ExecutorFactory` | Creates executors based on statement type |
| **Dispatch Map** | `ExecutionEngine` | Routes statements without switch/if-else chains |
| **Observer** | `DMLObserverRegistry` | Decouples AI from execution engine |
| **Strategy** | `IQueryOptimizer`, `TimeTravelEngine` | Swappable algorithms at runtime |
| **Singleton** | `AIManager`, `MetricsStore`, `AIScheduler`, `Logger` | Global coordination |
| **RAII** | `PageGuard` | Automatic page unpin on scope exit |
| **Iterator** | `TableHeap::Iterator` | Tuple-at-a-time row processing |
| **Builder** | `ForeignKeyConstraint`, `LogContext` | Fluent configuration |
| **Double Buffering** | `LogManager` | Non-blocking WAL writes |
| **Two-Phase Locking** | `LockManager` | Transaction isolation |

### Thread Safety Model

- **Global Reader-Writer Lock**: `ExecutionEngine::global_lock_` protects concurrent DML (shared) vs. exclusive recovery operations
- **Per-Component Mutexes**: AI subsystems use `std::shared_mutex` for read-heavy patterns
- **Lock-Free Ring Buffer**: `MetricsStore` uses atomic write index for zero-contention metric recording
- **Atomic Flags**: Lifecycle transitions use `std::atomic<bool>` without mutex overhead
- **Double Buffering**: `LogManager` swaps write/flush buffers to avoid blocking the hot path

### SOLID Principles

1. **Single Responsibility**: Separate executors for DDL, DML, transactions, system, users, databases
2. **Open/Closed**: Dispatch map — add statement types without modifying ExecutionEngine
3. **Liskov Substitution**: `IBufferManager`, `ITableStorage`, `IDiskManager` interfaces
4. **Interface Segregation**: `IQueryOptimizer`, `IDMLObserver`, `ProtocolSerializer`
5. **Dependency Inversion**: Executors depend on `IBufferManager`, not concrete `BufferPoolManager`

---

## 3. Storage Layer

### 3.1 DiskManager

Manages physical I/O to `.chronosdb` database files.

```cpp
class DiskManager {
    explicit DiskManager(const std::string& db_file);
    void ReadPage(uint32_t page_id, char* page_data);
    void WritePage(uint32_t page_id, const char* page_data);
    void WriteMetadata(const std::string& data);
    bool ReadMetadata(std::string& data);
    void SetEncryptionKey(const std::string& key);
    int GetNumPages();
};
```

- Cross-platform: Windows `HANDLE` / Linux `fd` abstraction
- Separate `.chronosdb.meta` metadata file
- Optional encryption support
- Checksum validation for page integrity

### 3.2 Page

Generic 4 KB in-memory page container.

```cpp
class Page {
    char data_[PAGE_SIZE];          // 4KB data written to disk
    page_id_t page_id_;            // Logical page ID
    int pin_count_;                // Reference count
    bool is_dirty_;                // Needs flush?
    lsn_t page_lsn_;              // WAL protocol tracking
    ReaderWriterLatch rwlatch_;    // Thread-safe access

    void RLock() / RUnlock();      // Reader latch
    void WLock() / WUnlock();      // Writer latch
    lsn_t GetPageLSN();           // For WAL protocol
};
```

### 3.3 TablePage (Slotted Page Layout)

```
| Checksum (4B) | PrevPageId (4B) | NextPageId (4B) |
| FreeSpacePtr (4B) | TupleCount (4B) |
| Slot[0] | Slot[1] | ... | ← grows forward
|                          → grows backward | Tuple[n] | ... | Tuple[0] |
```

- Doubly-linked list of pages for sequential scanning
- Slots grow forward from header; tuples grow backward from page end
- Soft deletes via `TUPLE_DELETED` flag in slot metadata
- 4,064 bytes max usable space per page (after 24-byte header + slot overhead)

### 3.4 Tuple

Represents a single row in serialized format.

```cpp
class Tuple {
    RID rid_;                    // Record ID (page_id, slot_id)
    std::vector<char> data_;     // Serialized column data

    Tuple(const std::vector<Value>& values, const Schema& schema);
    Value GetValue(const Schema& schema, uint32_t column_idx) const;
    void SerializeTo(char* storage) const;
    void DeserializeFrom(const char* storage, uint32_t size);
};
```

### 3.5 Schema & Column

```cpp
class Column {
    std::string name_;
    TypeId type_;                      // INTEGER, VARCHAR, BOOLEAN, DECIMAL, TIMESTAMP
    bool is_primary_key_;
    bool is_nullable_, is_unique_, is_auto_increment_;
    std::optional<Value> default_value_;
    std::string check_constraint_;

    bool ValidateValue(const Value& value) const;
};

class Schema {
    std::vector<Column> columns_;
    uint32_t length_;                 // Total row size in bytes

    const Column& GetColumn(uint32_t idx) const;
    int GetColumnIndex(const std::string& name) const;
};
```

### 3.6 TableHeap

Heap-file storage implementing `ITableStorage`. Pages form a doubly-linked list.

```cpp
class TableHeap : public ITableStorage {
    bool InsertTuple(const Tuple& tuple, RID* rid, Transaction* txn) override;
    bool GetTuple(const RID& rid, Tuple* tuple, Transaction* txn) override;
    bool MarkDelete(const RID& rid, Transaction* txn) override;
    bool UpdateTuple(const Tuple& tuple, const RID& rid, Transaction* txn) override;

    Iterator Begin(Transaction* txn);
    Iterator End();

    size_t CountAllTuples() const;
    size_t CountPages() const;
};
```

**Optimization**: `last_page_hint_` caches the last page with free space for O(1) inserts in the common case.

**Iterator Pattern**: Cached tuple in iterator avoids re-reading on dereference. Supports `ExtractTuple()` with move semantics.

### 3.7 InMemoryTableHeap

RAM-based table for time travel snapshots — bypasses buffer pool entirely.

```cpp
class InMemoryTableHeap {
    std::vector<Tuple> tuples_;
    void Reserve(size_t count);
    bool InsertTuple(Tuple&& tuple, RID* rid, void* txn);  // Move semantics
    Iterator Begin() const;
    Iterator End() const;
};
```

**Why?** Buffer pool thrashing degrades from 40K rows/sec to <2K rows/sec for 1M+ row snapshots. InMemoryTableHeap achieves ~500ms for 1M rows.

### 3.8 B+ Tree Index

Template-based B+ tree for ordered lookups.

```cpp
template <typename KeyType, typename ValueType, typename KeyComparator>
class BPlusTree {
    bool Insert(const KeyType& key, const ValueType& value, Transaction* txn);
    void Remove(const KeyType& key, Transaction* txn);
    bool GetValue(const KeyType& key, std::vector<ValueType>* result, Transaction* txn);
    page_id_t GetRootPageId() const;
};
```

- **BPlusTreeLeafPage**: Stores key-RID pairs. Linked list of siblings for range scans.
- **BPlusTreeInternalPage**: Stores key-child pointer pairs for tree navigation.
- **Concurrency**: `ReaderWriterLatch root_latch_` protects root during restructuring.
- **GenericKey<8>**: Fixed-size 8-byte keys using `memcmp` for comparison.

### 3.9 FreePageManager

Bitmap-based free page tracking on dedicated page 2.

```
Bit = 0 → Page is free
Bit = 1 → Page is allocated
```

O(1) allocation/deallocation via bitmap scan.

### 3.10 Storage Interfaces

```cpp
class ITableStorage {
    virtual bool InsertTuple(const Tuple& tuple, RID* rid, Transaction* txn) = 0;
    virtual bool GetTuple(const RID& rid, Tuple* tuple, Transaction* txn) = 0;
    virtual std::unique_ptr<Iterator> CreateIterator(Transaction* txn) = 0;
};

class IBufferManager {
    virtual Page* FetchPage(page_id_t page_id) = 0;
    virtual Page* NewPage(page_id_t* page_id) = 0;
    virtual bool UnpinPage(page_id_t page_id, bool is_dirty) = 0;
    virtual bool FlushPage(page_id_t page_id) = 0;
    virtual void SetLogManager(LogManager* log_manager) = 0;
};
```

---

## 4. Buffer Pool Management

### 4.1 BufferPoolManager (Standard)

Single-mutex buffer pool. Simple but serializes under high concurrency.

```cpp
class BufferPoolManager : public IBufferManager {
    Page* pages_;                                          // Pre-allocated array
    Replacer* replacer_;                                   // LRU or Clock
    std::unordered_map<page_id_t, frame_id_t> page_table_; // Page lookup
    std::list<frame_id_t> free_list_;                      // Available frames
    std::mutex latch_;                                     // Single lock
    LogManager* log_manager_;                              // WAL integration
};
```

**Fetch flow**: Check page_table → if miss, find free frame (free_list or replacer eviction) → read from disk → pin.

### 4.2 PartitionedBufferPoolManager

Reduces lock contention via hash-based partitioning (PostgreSQL-inspired).

```cpp
class PartitionedBufferPoolManager : public IBufferManager {
    struct Partition {
        std::mutex latch;
        Page* pages;
        Replacer* replacer;         // ClockReplacer
        std::unordered_map<page_id_t, frame_id_t> page_table;
    };
    std::vector<std::unique_ptr<Partition>> partitions_;  // 16 partitions

    size_t GetPartitionIndex(page_id_t id) { return id % num_partitions_; }
};
```

Each partition has an independent lock — ~16× throughput improvement.

### 4.3 AdaptiveBufferPoolManager

Self-sizing pool that grows/shrinks based on metrics.

```cpp
struct BufferPoolMetrics {
    std::atomic<uint64_t> cache_hits{0};
    std::atomic<uint64_t> cache_misses{0};
    std::atomic<uint64_t> evictions{0};
    double GetHitRate() const;
};
```

- **Grow** if hit rate < 90% (cache undersized)
- **Shrink** if hit rate > 98% (over-provisioned)
- **Throttle** growth if dirty ratio > 70% (I/O pressure)
- Chunk-based: adds/removes 128 MB chunks, background adaptation every 30 seconds

### 4.4 AdaptiveDistributedBufferPool

Combines partitioning + adaptive sizing + rebalancing.

- **Lock-free routing**: Knuth's multiplicative hash for partition selection
- **Per-partition metrics**: Independent hit rate tracking
- **Hot/cold detection**: Identifies imbalanced workloads
- **Dynamic rebalancing**: Transfers pages from cold to hot partitions

### 4.5 Replacer Strategies

| Strategy | Algorithm | Complexity | Best For |
|----------|-----------|-----------|----------|
| **LRUReplacer** | Doubly-linked list + hash map | O(1) all ops | General workloads |
| **ClockReplacer** | Circular buffer + reference bit | O(1) amortized | High concurrency |

### 4.6 PageGuard (RAII)

```cpp
class PageGuard {
    PageGuard(IBufferManager* bpm, page_id_t page_id, bool is_write);
    ~PageGuard() { Release(); }   // Auto-unpin
    template<typename T> T* As(); // Cast page data to typed pointer
    void SetDirty();
    bool UpgradeToWrite();        // RLock → WLock
};
```

Prevents pin count leaks — if an exception is thrown, pages are automatically unpinned.

---

## 5. Catalog & Metadata

### 5.1 Catalog

Central schema repository for all tables and indexes.

```cpp
class Catalog {
    TableMetadata* CreateTable(const std::string& name, const Schema& schema);
    TableMetadata* GetTable(const std::string& name);
    bool DropTable(const std::string& name);
    IndexInfo* CreateIndex(const std::string& name, const std::string& table, const std::string& col);
    std::vector<IndexInfo*> GetTableIndexes(const std::string& table);
    void SaveCatalog() / LoadCatalog();
};
```

- **OID-based storage**: Atomic `next_table_oid_` for stable identifiers
- **Dual lookup**: By OID (`tables_`) and by name (`names_to_oid_`)
- **Thread-safe**: `std::mutex latch_` protects all operations

### 5.2 TableMetadata

```cpp
struct TableMetadata {
    Schema schema_;
    std::string name_;
    std::unique_ptr<TableHeap> table_heap_;
    page_id_t first_page_id_;
    uint32_t oid_;
    std::vector<ForeignKey> foreign_keys_;
    lsn_t last_checkpoint_lsn_;         // Time travel optimization
};
```

### 5.3 IndexInfo

```cpp
struct IndexInfo {
    std::string name_, table_name_, col_name_;
    std::unique_ptr<BPlusTree<GenericKey<8>, RID, GenericComparator<8>>> b_plus_tree_;
};
```

### 5.4 ForeignKeyConstraint

Fluent builder pattern for referential integrity:

```cpp
ForeignKeyConstraint("fk_order_user")
    .SetColumns("user_id", "id")
    .SetReferencedTable("users")
    .SetOnDelete(Action::CASCADE)
    .SetOnUpdate(Action::RESTRICT);
```

---

## 6. Parser & Lexer

### 6.1 Lexer

Tokenizes SQL/CQL input with bilingual support.

```cpp
class Lexer {
    Lexer(std::string input);
    Token NextToken();
    std::vector<Token> Tokenize();
    static const std::map<std::string, TokenType>& GetKeywords();
};
```

**155+ token types** supporting:
- SQL keywords: SELECT, FROM, WHERE, CREATE, INSERT, UPDATE, DELETE, ...
- Franco-Arabic: `2E5TAR` (SELECT), `MEN` (FROM), `LAMA` (WHERE), `ZAWED` (INSERT), ...
- Types: INT_TYPE, STRING_TYPE, BOOL_TYPE, DECIMAL_TYPE, DATE_TYPE
- Operators: EQUALS, NOT_EQUALS, LESS, GREATER, AND, OR, IN, LIKE
- Aggregates: COUNT, SUM, AVG, MIN_AGG, MAX_AGG
- AI Layer: AI, ANOMALIES, EXECUTION, STATS

### 6.2 Parser

Recursive descent parser producing Statement AST nodes.

```cpp
class Parser {
    explicit Parser(Lexer lexer);
    std::unique_ptr<Statement> ParseQuery();
    static uint64_t ParseHumanDateToMicros(const std::string& text);
};
```

**Parsing capabilities**:
- DDL: CREATE TABLE (with constraints, FK, CHECK), DROP, ALTER, CREATE INDEX
- DML: INSERT, SELECT (JOIN, GROUP BY, HAVING, ORDER BY, LIMIT, OFFSET, AS OF), UPDATE, DELETE
- Transactions: BEGIN, COMMIT, ROLLBACK
- Database: CREATE/DROP/USE DATABASE
- Auth: LOGIN, CREATE/ALTER/DELETE USER
- System: SHOW (DATABASES, TABLES, STATUS, USERS, AI STATUS, ANOMALIES, EXECUTION STATS)
- Recovery: CHECKPOINT, RECOVER TO, STOP SERVER

### 6.3 Statement Classes

**StatementType enum (30+ types)** with one class per type:

| Category | Types |
|----------|-------|
| DDL | CREATE, DROP, ALTER_TABLE, CREATE_INDEX, DESCRIBE_TABLE, SHOW_CREATE_TABLE |
| DML | INSERT, SELECT, UPDATE_CMD, DELETE_CMD |
| Transaction | BEGIN, COMMIT, ROLLBACK |
| Database | CREATE_DB, USE_DB, DROP_DB |
| Auth | LOGIN, CREATE_USER, ALTER_USER_ROLE, DELETE_USER |
| System | SHOW_DATABASES, SHOW_TABLES, SHOW_STATUS, SHOW_USERS, WHOAMI |
| AI | SHOW_AI_STATUS, SHOW_ANOMALIES, SHOW_EXECUTION_STATS |
| Recovery | CHECKPOINT, RECOVER, STOP_SERVER |

### 6.4 SelectStatement (Most Complex)

```cpp
class SelectStatement : public Statement {
    bool select_all_;
    std::vector<std::string> columns_;
    std::string table_name_;
    std::vector<WhereCondition> where_clause_;

    // Enterprise features
    bool is_distinct_;
    std::vector<std::pair<std::string, std::string>> aggregates_;
    std::vector<JoinClause> joins_;
    std::vector<std::string> group_by_columns_;
    std::vector<WhereCondition> having_clause_;
    std::vector<OrderByClause> order_by_;
    int limit_, offset_;
    uint64_t as_of_timestamp_;    // Time travel
};
```

### 6.5 WhereCondition

```cpp
struct WhereCondition {
    std::string column;
    std::string op;                // "=", ">", "<", ">=", "<=", "IN", "LIKE"
    Value value;
    std::vector<Value> in_values;  // For IN operator
    LogicType next_logic;          // AND or OR
};
```

---

## 7. Execution Engine

### 7.1 ExecutionEngine

The central coordinator using the dispatch map pattern.

```cpp
class ExecutionEngine {
    static std::shared_mutex global_lock_;

    ExecutionResult Execute(Statement* stmt, SessionContext* session);
    Catalog* GetCatalog();
    bool IsShutdownRequested() const;
};
```

**Dispatch Map**: `std::unordered_map<StatementType, StatementHandler>` — maps each statement type to a handler function. No switch/if-else chains. Adding a new statement type requires adding one map entry.

**Specialized Executors** (Single Responsibility Principle):

| Executor | Responsibility |
|----------|---------------|
| `DDLExecutor` | CREATE TABLE/INDEX, DROP, ALTER, DESCRIBE |
| `DMLExecutor` | INSERT, SELECT, UPDATE, DELETE |
| `SystemExecutor` | SHOW commands, WHOAMI, AI status |
| `UserExecutor` | CREATE/ALTER/DELETE USER |
| `DatabaseExecutor` | CREATE/USE/DROP DATABASE |
| `TransactionExecutor` | BEGIN, COMMIT, ROLLBACK |

### 7.2 DDLExecutor

```cpp
class DDLExecutor {
    ExecutionResult CreateTable(CreateStatement* stmt);
    ExecutionResult DropTable(DropStatement* stmt);
    ExecutionResult AlterTable(AlterTableStatement* stmt);
    ExecutionResult CreateIndex(CreateIndexStatement* stmt);
    ExecutionResult DescribeTable(DescribeTableStatement* stmt);
    ExecutionResult ShowCreateTable(ShowCreateTableStatement* stmt);
};
```

### 7.3 DMLExecutor

```cpp
class DMLExecutor {
    ExecutionResult Insert(InsertStatement* stmt, Transaction* txn);
    ExecutionResult Select(SelectStatement* stmt, SessionContext* session, Transaction* txn);
    ExecutionResult Update(UpdateStatement* stmt, Transaction* txn);
    ExecutionResult Delete(DeleteStatement* stmt, Transaction* txn);
};
```

**AI integration points**:
- Before each DML: `DMLObserverRegistry::NotifyBefore()` — Immune System can block
- After each DML: `DMLObserverRegistry::NotifyAfter()` — Learning Engine records feedback
- SELECT-specific: `LearningEngine::RecommendScanStrategy()` consulted before index decision
- Time travel: `TemporalIndexManager::OnTimeTravelQuery()` on AS OF queries

### 7.4 ExecutorFactory

```cpp
class ExecutorFactory {
    static ExecutorFactory& Instance();
    void Register(StatementType type, ExecutorFunc executor);
    ExecutionResult Execute(Statement* stmt, ExecutorContext* ctx,
                           SessionContext* session, Transaction* txn);
};
```

### 7.5 PredicateEvaluator

Centralized WHERE clause evaluation — eliminates 180 lines of duplicated code.

```cpp
class PredicateEvaluator {
    static bool Evaluate(const Tuple& tuple, const Schema& schema,
                        const std::vector<WhereCondition>& conditions);
};
```

Supports: `=`, `!=`, `<>`, `>`, `<`, `>=`, `<=`, `IN`, `LIKE` with AND/OR chaining.

### 7.6 ExecutionResult

```cpp
struct ExecutionResult {
    bool success;
    std::string message;
    std::shared_ptr<ResultSet> result_set;

    static ExecutionResult Message(std::string msg);
    static ExecutionResult Error(std::string error_msg);
    static ExecutionResult Data(std::shared_ptr<ResultSet> rs);
};
```

---

## 8. Concurrency Control

### 8.1 Transaction

```cpp
class Transaction {
    txn_id_t txn_id_;
    TransactionState state_;               // RUNNING, COMMITTED, ABORTED
    lsn_t prev_lsn_;                      // Undo chain
    std::set<RID> write_set_;             // Modified rows
    std::vector<Modification> modifications_;
};
```

### 8.2 TransactionManager

```cpp
class TransactionManager {
    Transaction* Begin();             // Create + log BEGIN
    bool Commit(Transaction* txn);    // Log COMMIT, flush, release locks
    bool Abort(Transaction* txn);     // Log ABORT, undo, release locks
};
```

### 8.3 LockManager (Two-Phase Locking)

```cpp
class LockManager {
    bool LockTable(txn_id_t, const std::string& table, LockMode mode);
    bool UnlockTable(txn_id_t, const std::string& table);
    bool LockRow(txn_id_t, RID rid, LockMode mode);
    bool UnlockRow(txn_id_t, RID rid);
    void ReleaseAllLocks(txn_id_t);
    void EnableDeadlockDetection();
};
```

**Lock modes**: SHARED (multiple readers) and EXCLUSIVE (single writer).

**Deadlock handling**:
- Wait-for graph built from blocked transactions
- DFS cycle detection
- Victim selection: youngest transaction in cycle aborted
- Consistent lock ordering: Table → Page → Row

---

## 9. Recovery & Write-Ahead Logging

### 9.1 LogManager

Production-grade WAL with double buffering and multi-database support.

```cpp
class LogManager {
    lsn_t AppendLogRecord(LogRecord& record);
    void Flush(bool force);
    void FlushToLSN(lsn_t target_lsn);     // WAL protocol

    void SwitchDatabase(const std::string& db_name);
    void CreateDatabaseLog(const std::string& db_name);
    void DropDatabaseLog(const std::string& db_name);

    void BeginTransaction(txn_id_t);
    void CommitTransaction(txn_id_t);
    void AbortTransaction(txn_id_t);
};
```

**Multi-file architecture**:
```
data/system/sys.log           — DDL operations
data/<db_name>/wal.log        — Per-database DML operations
```

**Double buffering**: Write buffer + flush buffer swapped atomically. Background flush thread writes to disk without blocking the hot path.

**WAL protocol**: Before any dirty page is flushed, `FlushToLSN(page_lsn)` ensures the corresponding log record is on disk first.

### 9.2 LogRecord

ARIES-compliant log structure.

```cpp
struct LogRecord {
    lsn_t lsn_;              // Unique sequential ID
    lsn_t prev_lsn_;         // Previous record in transaction (undo chain)
    lsn_t undo_next_lsn_;    // For CLR: next to undo
    txn_id_t txn_id_;
    timestamp_t timestamp_;   // Microseconds since epoch
    LogRecordType type_;      // BEGIN, COMMIT, ABORT, INSERT, UPDATE, DELETE, ...

    std::string table_name_;
    Value old_value_;         // UNDO info
    Value new_value_;         // REDO info
};
```

**Record types**: BEGIN, COMMIT, ABORT, INSERT, UPDATE, MARK_DELETE, APPLY_DELETE, ROLLBACK_DELETE, CREATE_DB, DROP_DB, CREATE_TABLE, DROP_TABLE, CHECKPOINT_BEGIN, CHECKPOINT_END, CLR.

### 9.3 CheckpointManager

ARIES-compliant fuzzy checkpointing.

```cpp
class CheckpointManager {
    void BeginCheckpoint();                  // Blocking
    void FuzzyCheckpoint();                  // Non-blocking (production)
    lsn_t GetLastCheckpointLSN();
    MasterRecord GetMasterRecord();

    void StartBackgroundCheckpointing(uint32_t interval_seconds);
    void StopBackgroundCheckpointing();
    void OnLogOperation();                   // Operation-based trigger
};
```

**Master Record** (`data/system/master_record`):
```cpp
struct MasterRecord {
    lsn_t checkpoint_lsn;
    std::streampos checkpoint_offset;
    timestamp_t timestamp;
};
```

**Checkpoint phases**: (1) Log CHECKPOINT_BEGIN → (2) Capture ATT/DPT → (3) Flush dirty pages → (4) Log CHECKPOINT_END → (5) Update master record.

### 9.4 RecoveryManager

ARIES three-phase crash recovery.

```cpp
class RecoveryManager {
    void ARIES();                           // Full recovery
    void RecoverDatabase(const std::string& db_name);
    void RecoverToTime(timestamp_t target);  // Time travel recover
};
```

**Phase 1 — Analysis**: Scan log from last checkpoint, build Active Transaction Table (ATT) and Dirty Page Table (DPT).

**Phase 2 — Redo**: Replay all operations forward from checkpoint, restoring pre-crash state.

**Phase 3 — Undo**: Roll back uncommitted transactions via `prev_lsn` chains, writing CLR records for crash safety.

---

## 10. Time Travel Engine

### 10.1 Overview

Enables querying data at any historical timestamp and recovering to past states.

```cpp
class TimeTravelEngine {
    std::unique_ptr<InMemoryTableHeap> BuildSnapshotInMemory(
        const std::string& table, uint64_t target_time, const std::string& db);

    TimeTravelResult RecoverTo(uint64_t target_time, const std::string& db);
};
```

### 10.2 Strategies

| Strategy | When Used | Complexity |
|----------|----------|-----------|
| **REVERSE_DELTA** | Target near present (within threshold) | O(delta) |
| **FORWARD_REPLAY** | Target far in past (before threshold) | O(total_history) |
| **AUTO** | Automatic selection based on time distance | Best of both |

**Reverse Delta** (default, 1-hour threshold):
1. Clone current table state into InMemoryTableHeap
2. Collect all operations after `target_time`
3. Apply inverse operations in reverse order
4. Result is the historical snapshot

**Forward Replay** (fallback):
1. Start with empty InMemoryTableHeap
2. Replay all operations from log start up to `target_time`
3. Result is the historical snapshot

### 10.3 InverseOperation

```cpp
struct InverseOperation {
    LogRecordType original_type;       // What happened
    std::vector<Value> values_to_insert;   // Undo DELETE → re-insert
    std::vector<Value> values_to_delete;   // Undo INSERT → remove
    std::vector<Value> old_values;         // Undo UPDATE → restore old
    std::vector<Value> new_values;         // Undo UPDATE → find current
};
```

### 10.4 LogPageReader

Page-cached log file reading for efficient sequential access during time travel.

```cpp
class LogPageReader {
    void Open(const std::string& path);
    size_t Read(size_t offset, char* buffer, size_t length);
    double GetHitRate() const;
};
```

LRU page cache avoids repeated disk seeks during log scanning.

---

## 11. Network Layer

### 11.1 ChronosServer

Multi-protocol server with thread pool-based connection handling.

```cpp
class ChronosServer {
    void Start(int port);        // Listen on port (blocking)
    void Shutdown();             // Graceful shutdown
    void HandleClient(socket);   // Per-connection processing
};
```

**Resources managed**: System DiskManager, BufferPoolManager, Catalog, AuthManager, DatabaseRegistry, CheckpointManager, ThreadPool.

### 11.2 ConnectionHandler

Per-connection request processor.

```cpp
class ClientConnectionHandler {
    std::string ProcessRequest(const std::string& request);
    void SetResponseFormat(ProtocolType type);
    std::shared_ptr<SessionContext> GetSession();
};
```

### 11.3 Protocol System

| Protocol | Format | Use Case |
|----------|--------|----------|
| TextProtocol | Plain text table | CLI / humans |
| JsonProtocol | JSON objects | Web APIs |
| BinaryProtocol | Binary encoding | High-performance clients |

**Wire format**: 5-byte header (1B type + 4B length) + payload.

### 11.4 SessionContext

```cpp
struct SessionContext {
    std::string current_user;
    std::string current_db = "chronosdb";
    bool is_authenticated = false;
    uint32_t session_id = 0;
    UserRole role = UserRole::READONLY;
};
```

### 11.5 DatabaseRegistry

Per-database resource isolation.

```cpp
class DatabaseRegistry {
    DbEntry* GetOrCreate(const std::string& name, size_t pool_size);
    DbEntry* Get(const std::string& name);
    void Remove(const std::string& name);
    void FlushAllDatabases();
};
```

Each database gets isolated: DiskManager + BufferPoolManager + Catalog.

---

## 12. Authentication & RBAC

### 12.1 AuthManager

```cpp
class AuthManager {
    bool Authenticate(const std::string& username, const std::string& password,
                     UserRole& out_role);
    void CreateUser(const std::string& username, const std::string& password,
                   UserRole role);
    void DeleteUser(const std::string& username);
    UserRole GetUserRole(const std::string& username, const std::string& db_name);
    bool HasPermission(UserRole role, StatementType stmt_type);
};
```

### 12.2 Role Hierarchy

| Role | DDL | DML Write | DML Read | User Mgmt | System |
|------|-----|-----------|----------|-----------|--------|
| SUPERADMIN | Yes | Yes | Yes | Yes | Yes |
| ADMIN | Yes | Yes | Yes | No | Yes |
| NORMAL | No | Yes | Yes | No | Limited |
| READONLY | No | No | Yes | No | Limited |
| DENIED | No | No | No | No | No |

### 12.3 Per-Database Roles

Users can have different roles in different databases:

```cpp
struct UserInfo {
    std::string username;
    std::string password_hash;
    std::unordered_map<std::string, UserRole> db_roles;  // Per-database
};
```

---

## 13. AI Layer — Self-Learning Execution Engine

### 13.1 Overview

Uses a **UCB1 Multi-Armed Bandit** algorithm to learn which scan strategy (Sequential Scan vs. Index Scan) is optimal for each query pattern. No manual tuning — the system learns from real execution feedback.

### 13.2 QueryFeatureExtractor

Extracts an 8-dimensional feature vector from each `SelectStatement`:

| Feature | Description |
|---------|-------------|
| `table_row_count_log` | log2(estimated row count) |
| `where_clause_count` | Number of WHERE conditions |
| `has_equality_predicate` | 1.0 if any WHERE uses `=` |
| `has_index_available` | 1.0 if index exists on first WHERE column |
| `selectivity_estimate` | Estimated fraction of rows matching |
| `column_count` | Number of selected columns |
| `has_order_by` | 1.0 if ORDER BY present |
| `has_limit` | 1.0 if LIMIT present |

### 13.3 UCB1 Bandit Algorithm

```
Selection:
    score(a) = Q(a) + c · √(ln(N) / N_a)

    Q(a) = average reward for arm a
    c    = √2 ≈ 1.414 (exploration constant)
    N    = total pulls across all arms
    N_a  = pulls for arm a

    Select: argmax_a [ score(a) ]

Reward:
    reward(time_ms) = 1.0 / (1.0 + time_ms / 100.0)

    10ms   → 0.91 (excellent)
    100ms  → 0.50 (average)
    1000ms → 0.09 (poor)
```

**Per-table contextual learning**: Global statistics track overall performance. Per-table statistics override globals after 10+ pulls per arm for that table.

**Exploration strategy**: First 30 queries use existing heuristics. Per-arm minimum 5 pulls before exploitation begins.

### 13.4 LearningEngine

Orchestrator implementing `IDMLObserver` + `IQueryOptimizer`.

```cpp
class LearningEngine : public IDMLObserver, public IQueryOptimizer {
    void OnAfterDML(const DMLEvent& event) override;
    bool RecommendScanStrategy(const SelectStatement* stmt,
                               const std::string& table_name,
                               ScanStrategy& out_strategy) override;
};
```

**Flow**: DMLExecutor → RecommendScanStrategy() → UCB1Bandit::SelectStrategy() → Execute → NotifyAfter() → RecordOutcome()

---

## 14. AI Layer — Immune System

### 14.1 Overview

Autonomous anomaly detection and self-healing. Monitors mutation patterns, detects anomalies via Z-score analysis, and can automatically block operations or recover data.

### 14.2 MutationMonitor

Per-table rolling window of mutation events.

```cpp
class MutationMonitor {
    void RecordMutation(const std::string& table, uint32_t row_count);
    double GetMutationRate(const std::string& table) const;
    std::vector<double> GetHistoricalRates(const std::string& table) const;
};
```

10-minute sliding window. Rate calculated over last 5 seconds. Historical rates divided into 1-second intervals for Z-score.

### 14.3 UserBehaviorProfiler

```cpp
class UserBehaviorProfiler {
    void RecordActivity(const std::string& user, const std::string& table, bool is_mutation);
    double GetDeviationScore(const std::string& user) const;
};
```

`deviation = 0.6 × mutation_rate_deviation + 0.4 × table_access_deviation`

### 14.4 AnomalyDetector (Z-Score)

```
z = (x - μ) / σ

Severity:
    z < 2.0  → NONE   (within 95.4% of normal)
    z ≥ 2.0  → LOW    (4.6% false positive — log warning)
    z ≥ 3.0  → MEDIUM (0.3% false positive — block mutations)
    z ≥ 4.0  → HIGH   (0.006% false positive — auto-recover)
```

### 14.5 ResponseEngine

Graduated responses based on severity.

| Severity | Action | Description |
|----------|--------|-------------|
| LOW | LOG_WARN | Log warning with Z-score |
| MEDIUM | BLOCK | Block mutations on table + user |
| HIGH | AUTO-RECOVER | Block + TimeTravelEngine::RecoverTo(now - 60s) |

### 14.6 ImmuneSystem

```cpp
class ImmuneSystem : public IDMLObserver {
    bool OnBeforeDML(const DMLEvent& event) override;   // Can block!
    void OnAfterDML(const DMLEvent& event) override;    // Records mutations
    void PeriodicAnalysis();                             // Every 1 second
    std::vector<AnomalyReport> GetRecentAnomalies(size_t max) const;
};
```

---

## 15. AI Layer — Temporal Index Manager

### 15.1 Overview

Makes time-travel queries faster by intelligently scheduling snapshots. Uses DBSCAN clustering to detect temporal hotspots and CUSUM change-point detection to identify optimal snapshot moments.

### 15.2 TemporalAccessTracker

Records which timestamps are queried in time-travel operations.

```cpp
class TemporalAccessTracker {
    void RecordAccess(const TemporalAccessEvent& event);
    std::vector<uint64_t> GetHotTimestamps(size_t k) const;
    std::vector<FrequencyBucket> GetFrequencyHistogram(uint64_t bucket_width) const;
};
```

### 15.3 HotspotDetector — DBSCAN Clustering

Simplified 1D DBSCAN on sorted timestamps (O(n)):

```
Parameters: epsilon = 60s, min_points = 5

1. Sort queried timestamps
2. Group consecutive timestamps within epsilon
3. Clusters with ≥ min_points become hotspots
4. For each hotspot: center = mean, density = count / range
```

### 15.4 HotspotDetector — CUSUM Change-Point Detection

Detects sustained shifts in temporal query patterns:

```
Parameters: threshold = 4.0σ, drift = 0.5σ

S⁺ = max(0, S⁺ + (x_i - μ - drift))     // Upward shift
S⁻ = max(0, S⁻ + (μ - x_i - drift))     // Downward shift

If S⁺ > threshold OR S⁻ > threshold → Change point detected
```

### 15.5 SmartSnapshotScheduler

```
Trigger checkpoint if:
    1. ≥ 30 seconds since last snapshot
    AND either:
    2a. Change point detected in last 5 minutes
    2b. Hotspot with density > 1.0 and access_count ≥ 10
```

### 15.6 WALRetentionManager

Adaptive retention policy:
- **Hot zone (24h)**: Full WAL fidelity
- **Cold cutoff (7d)**: WAL can be pruned
- **Adaptive**: Extends cold cutoff if users query older data

### 15.7 TemporalIndexManager

```cpp
class TemporalIndexManager {
    void OnTimeTravelQuery(const std::string& table, uint64_t timestamp, const std::string& db);
    void PeriodicAnalysis();   // Every 30 seconds
    std::vector<TemporalHotspot> GetCurrentHotspots() const;
};
```

Pipeline: Access events → DBSCAN hotspots → CUSUM change points → Snapshot scheduling → WAL retention update.

---

## 16. AI Shared Foundation

### 16.1 AIConfig

All constants centralized in `ai_config.h`:

| Constant | Value | Purpose |
|----------|-------|---------|
| `UCB1_EXPLORATION_CONSTANT` | √2 ≈ 1.414 | Exploration vs. exploitation |
| `MIN_SAMPLES_BEFORE_LEARNING` | 30 | Queries before AI recommends |
| `ZSCORE_LOW_THRESHOLD` | 2.0 | Warning level |
| `ZSCORE_MEDIUM_THRESHOLD` | 3.0 | Block mutations |
| `ZSCORE_HIGH_THRESHOLD` | 4.0 | Auto-recover |
| `HOTSPOT_CLUSTER_EPSILON_US` | 60,000,000 | DBSCAN: 60s neighborhood |
| `HOTSPOT_CLUSTER_MIN_POINTS` | 5 | DBSCAN: minimum cluster size |
| `CUSUM_THRESHOLD_SIGMA_MULT` | 4.0 | CUSUM detection threshold |
| `METRICS_RING_BUFFER_SIZE` | 8,192 | Max metrics in ring buffer |

### 16.2 MetricsStore (Lock-Free Ring Buffer)

```cpp
class MetricsStore {
    static MetricsStore& Instance();
    void Record(const MetricEvent& event);     // Lock-free write via atomic index
    std::vector<MetricEvent> Query(MetricType type, uint64_t since_us) const;
    uint64_t GetMutationCount(const std::string& table, uint64_t since_us) const;
};
```

Fixed-size array of 8,192 entries. Atomic write index for zero-contention writes. Oldest entries silently overwritten on wrap.

### 16.3 DMLObserverRegistry

```cpp
class IDMLObserver {
    virtual bool OnBeforeDML(const DMLEvent& event) { return true; }  // false = block
    virtual void OnAfterDML(const DMLEvent& event) {}
};

class DMLObserverRegistry {
    static DMLObserverRegistry& Instance();
    void Register(IDMLObserver* observer);
    bool NotifyBefore(const DMLEvent& event);   // Any observer can block
    void NotifyAfter(const DMLEvent& event);
};
```

### 16.4 AIScheduler

```cpp
class AIScheduler {
    static AIScheduler& Instance();
    TaskId SchedulePeriodic(const std::string& name, uint32_t interval_ms,
                           std::function<void()> task);
    void Cancel(TaskId id);
};
```

10ms sleep ticks for responsive shutdown. Uses existing ThreadPool for task execution.

### 16.5 AIManager

```cpp
class AIManager {
    static AIManager& Instance();
    void Initialize(Catalog*, IBufferManager*, LogManager*, CheckpointManager*);
    void Shutdown();
    LearningEngine* GetLearningEngine();
    ImmuneSystem* GetImmuneSystem();
    TemporalIndexManager* GetTemporalIndexManager();
    AIStatus GetStatus() const;
};
```

Lifecycle: Initialize() in ExecutionEngine constructor → Shutdown() in destructor.

---

## 17. Command-Line Interfaces

### 17.1 Server (`chronosdb_server`)

```bash
./chronosdb_server [--service] [--config path]
```

Startup: Load config → Initialize disk → Create buffer pool (4 strategies) → Create catalog → Start WAL → Listen on port 2501.

Buffer pool strategy selection order:
1. AdaptiveDistributedBufferPool (16 partitions + adaptive + rebalancing)
2. AdaptiveBufferPoolManager (dynamic sizing)
3. PartitionedBufferPoolManager (16 partitions)
4. BufferPoolManager (single mutex)

### 17.2 Shell (`chronosdb_shell`)

```bash
./chronosdb_shell
chronos> CONNECT chronos://chronos:root@localhost:2501/mydb;
chronos> SELECT * FROM users;
```

Interactive SQL/CQL client with bilingual keyword reference and syntax help.

### 17.3 Setup (`chronosdb_setup`)

```bash
./chronosdb_setup [--port 2501] [--username chronos] [--password root]
                  [--data-dir ./data] [--encryption] [--non-interactive]
```

First-time configuration wizard. Checks port availability, generates encryption keys.

### 17.4 Service (`chronosdb_service`)

Windows service wrapper. Register/unregister as system service.

---

## 18. SQL Commands Reference

### Data Definition

```sql
CREATE TABLE users (
    id INT PRIMARY KEY AUTO_INCREMENT,
    name STRING NOT NULL,
    email STRING UNIQUE,
    age INT CHECK(age > 0),
    dept_id INT REFERENCES departments(id) ON DELETE CASCADE
);

CREATE INDEX idx_name ON users(name);
DROP TABLE users;
ALTER TABLE users ADD COLUMN phone STRING;
DESCRIBE users;
SHOW CREATE TABLE users;
```

### Data Manipulation

```sql
INSERT INTO users (name, email) VALUES ('Alice', 'alice@example.com');

SELECT * FROM users WHERE age > 25 ORDER BY name ASC LIMIT 10;
SELECT department, COUNT(*), AVG(salary) FROM employees
    GROUP BY department HAVING COUNT(*) > 5;
SELECT * FROM orders INNER JOIN users ON orders.user_id = users.id;

-- Time Travel
SELECT * FROM users AS OF '2026-01-15 14:30:00';

UPDATE users SET email = 'new@email.com' WHERE id = 1;
DELETE FROM users WHERE id > 100;
```

### Transaction Control

```sql
BEGIN;
INSERT INTO accounts (id, balance) VALUES (1, 1000);
UPDATE accounts SET balance = balance - 100 WHERE id = 1;
COMMIT;
-- or ROLLBACK;
```

### Database & User Management

```sql
CREATE DATABASE mydb;
USE mydb;
DROP DATABASE mydb;

LOGIN chronos root;
CREATE USER alice WITH PASSWORD 'secret' ROLE ADMIN;
ALTER USER alice SET ROLE READONLY;
DELETE USER alice;
SHOW USERS;
WHOAMI;
```

### Recovery & System

```sql
CHECKPOINT;
RECOVER TO '2026-01-15 14:30:00';
SHOW STATUS;
SHOW DATABASES;
SHOW TABLES;
STOP SERVER;
```

### AI Layer

```sql
SHOW AI STATUS;
SHOW ANOMALIES;
SHOW EXECUTION STATS;

-- Franco-Arabic equivalents
WARENY ZAKA2 7ALAH;
WARENY SHOZOOZ;
WARENY TANFEEZ E7SA2EYAT;
```

---

## 19. UML Diagrams

### 19.1 Full System Component Diagram

```plantuml
@startuml ChronosDB_Full_System
!theme plain
skinparam componentStyle rectangle

package "Client Layer" {
    [Shell CLI] as SHELL
    [Network Client] as NCLIENT
    [Browser/API] as BROWSER
}

package "Network Layer" {
    [ChronosServer] as SRV
    [ConnectionHandler] as CH
    [ProtocolSerializer] as PROTO
    [SessionContext] as SESS
    [DatabaseRegistry] as DBREG
}

package "Query Processing" {
    [Lexer] as LEX
    [Parser] as PARSE
    [ExecutionEngine] as EE
}

package "Execution Layer" {
    [DDLExecutor] as DDL
    [DMLExecutor] as DML
    [SystemExecutor] as SYS
    [UserExecutor] as UEXEC
    [DatabaseExecutor] as DBEXEC
    [TransactionExecutor] as TXEXEC
    [PredicateEvaluator] as PRED
}

package "Storage Layer" {
    [IBufferManager] as IBPM
    [BufferPoolManager] as BPM
    [PartitionedBPM] as PBPM
    [AdaptiveDistributedBPM] as ADBPM
    [DiskManager] as DM
    [TableHeap] as TH
    [BPlusTree] as BPT
    [Catalog] as CAT
    [PageGuard] as PG
}

package "Concurrency" {
    [TransactionManager] as TXM
    [LockManager] as LM
}

package "Recovery Layer" {
    [LogManager] as LOG
    [CheckpointManager] as CP
    [RecoveryManager] as RM
    [TimeTravelEngine] as TTE
}

package "AI Layer" {
    package "Foundation" {
        [AIManager] as AIM
        [MetricsStore] as MS
        [DMLObserverRegistry] as DOR
        [AIScheduler] as SCHED
    }
    package "Learning Engine" {
        [LearningEngine] as LE
        [UCB1Bandit] as UCB
        [QueryFeatureExtractor] as QFE
    }
    package "Immune System" {
        [ImmuneSystem] as IS
        [MutationMonitor] as MM
        [AnomalyDetector] as AD
        [ResponseEngine] as RE
    }
    package "Temporal Index" {
        [TemporalIndexManager] as TIM
        [HotspotDetector] as HD
        [SmartSnapshotScheduler] as SSS
    }
}

package "Auth" {
    [AuthManager] as AUTH
}

SHELL --> SRV
NCLIENT --> SRV
BROWSER --> SRV

SRV --> CH
CH --> PROTO
CH --> SESS
SRV --> DBREG
SRV --> AUTH

CH --> LEX
LEX --> PARSE
PARSE --> EE

EE --> DDL
EE --> DML
EE --> SYS
EE --> UEXEC
EE --> DBEXEC
EE --> TXEXEC
EE --> AIM

DML --> DOR
DML --> LE
DML --> TIM
DML --> PRED
DML --> CAT
DML --> TH
DML --> BPT

IBPM <|.. BPM
IBPM <|.. PBPM
IBPM <|.. ADBPM
TH --> IBPM
BPT --> IBPM
IBPM --> DM
PG --> IBPM

TXEXEC --> TXM
TXM --> LM
TXM --> LOG

LOG --> CP
CP --> RM
TTE --> LOG

AIM --> LE
AIM --> IS
AIM --> TIM
LE --> UCB
LE --> QFE
LE --> MS
IS --> MM
IS --> AD
IS --> RE
RE --> TTE
TIM --> HD
TIM --> SSS
SSS --> CP
SCHED --> IS
SCHED --> TIM

SYS --> AIM
SYS --> IS
SYS --> LE

@enduml
```

### 19.2 Class Diagram — Storage Layer

```plantuml
@startuml Storage_Classes

interface ITableStorage {
    + InsertTuple(tuple, rid, txn): bool
    + GetTuple(rid, tuple, txn): bool
    + MarkDelete(rid, txn): bool
    + UpdateTuple(tuple, rid, txn): bool
    + CreateIterator(txn): unique_ptr<Iterator>
}

interface IBufferManager {
    + FetchPage(page_id): Page*
    + NewPage(page_id): Page*
    + UnpinPage(page_id, is_dirty): bool
    + FlushPage(page_id): bool
    + SetLogManager(log_mgr): void
}

class Page {
    - data_: char[4096]
    - page_id_: page_id_t
    - pin_count_: int
    - is_dirty_: bool
    - page_lsn_: lsn_t
    - rwlatch_: ReaderWriterLatch
    + RLock() / RUnlock()
    + WLock() / WUnlock()
}

class TablePage {
    + InsertTuple(tuple, rid): bool
    + GetTuple(rid, tuple): bool
    + MarkDelete(rid): bool
    + GetFreeSpaceRemaining(): uint32_t
}

class TableHeap {
    - bpm_: IBufferManager*
    - first_page_id_: page_id_t
    - last_page_hint_: page_id_t
    + InsertTuple(tuple, rid, txn): bool
    + Begin(txn): Iterator
    + CountAllTuples(): size_t
}

class Tuple {
    - rid_: RID
    - data_: vector<char>
    + GetValue(schema, col_idx): Value
    + SerializeTo() / DeserializeFrom()
}

class Schema {
    - columns_: vector<Column>
    - length_: uint32_t
    + GetColumn(idx): Column
    + GetColumnIndex(name): int
}

class "BPlusTree<K,V,Comp>" as BPT {
    - root_page_id_: page_id_t
    - bpm_: IBufferManager*
    + Insert(key, value, txn): bool
    + Remove(key, txn): void
    + GetValue(key, result, txn): bool
}

class DiskManager {
    + ReadPage(page_id, data): void
    + WritePage(page_id, data): void
    + SetEncryptionKey(key): void
}

TableHeap ..|> ITableStorage
TablePage --|> Page
TableHeap --> IBufferManager
TableHeap --> TablePage
BPT --> IBufferManager
Tuple --> Schema

@enduml
```

### 19.3 Class Diagram — Execution Engine

```plantuml
@startuml Execution_Engine_Classes

class ExecutionEngine {
    - dispatch_map_: map<StatementType, Handler>
    - ddl_executor_: unique_ptr<DDLExecutor>
    - dml_executor_: unique_ptr<DMLExecutor>
    - system_executor_: unique_ptr<SystemExecutor>
    + Execute(stmt, session): ExecutionResult
    - InitializeDispatchMap(): void
    - CheckPermissions(type, session): bool
}

class DDLExecutor {
    + CreateTable(stmt): ExecutionResult
    + DropTable(stmt): ExecutionResult
    + AlterTable(stmt): ExecutionResult
    + CreateIndex(stmt): ExecutionResult
}

class DMLExecutor {
    + Insert(stmt, txn): ExecutionResult
    + Select(stmt, session, txn): ExecutionResult
    + Update(stmt, txn): ExecutionResult
    + Delete(stmt, txn): ExecutionResult
}

class SystemExecutor {
    + ShowDatabases(stmt, session): ExecutionResult
    + ShowTables(stmt, session): ExecutionResult
    + ShowAIStatus(stmt): ExecutionResult
    + ShowAnomalies(stmt): ExecutionResult
    + ShowExecutionStats(stmt): ExecutionResult
}

class UserExecutor {
    + CreateUser(stmt, session): ExecutionResult
    + AlterUserRole(stmt, session): ExecutionResult
    + DeleteUser(stmt, session): ExecutionResult
}

class DatabaseExecutor {
    + CreateDatabase(stmt, session): ExecutionResult
    + UseDatabase(stmt, session): ExecutionResult
    + DropDatabase(stmt, session): ExecutionResult
}

class TransactionExecutor {
    + Begin(): ExecutionResult
    + Commit(): ExecutionResult
    + Rollback(): ExecutionResult
}

class ExecutorFactory {
    + {static} Instance(): ExecutorFactory&
    + Register(type, func): void
    + Execute(stmt, ctx, session, txn): ExecutionResult
}

class PredicateEvaluator {
    + {static} Evaluate(tuple, schema, conditions): bool
}

ExecutionEngine --> DDLExecutor
ExecutionEngine --> DMLExecutor
ExecutionEngine --> SystemExecutor
ExecutionEngine --> UserExecutor
ExecutionEngine --> DatabaseExecutor
ExecutionEngine --> TransactionExecutor
ExecutionEngine ..> ExecutorFactory

@enduml
```

### 19.4 Class Diagram — Recovery & Time Travel

```plantuml
@startuml Recovery_Classes

class LogManager {
    - next_lsn_: atomic<lsn_t>
    - persistent_lsn_: atomic<lsn_t>
    - log_buffer_: vector<char>
    - flush_buffer_: vector<char>
    + AppendLogRecord(record): lsn_t
    + Flush(force): void
    + FlushToLSN(target): void
    + SwitchDatabase(db): void
    + BeginTransaction(txn_id): void
    + CommitTransaction(txn_id): void
}

class LogRecord {
    + lsn_: lsn_t
    + prev_lsn_: lsn_t
    + txn_id_: txn_id_t
    + timestamp_: timestamp_t
    + type_: LogRecordType
    + table_name_: string
    + old_value_: Value
    + new_value_: Value
}

class CheckpointManager {
    + BeginCheckpoint(): void
    + FuzzyCheckpoint(): void
    + GetLastCheckpointLSN(): lsn_t
    + StartBackgroundCheckpointing(interval): void
    + OnLogOperation(): void
}

class RecoveryManager {
    + ARIES(): void
    + RecoverDatabase(db): void
    + RecoverToTime(target): void
}

class TimeTravelEngine {
    - reverse_delta_threshold_: uint64_t
    + BuildSnapshotInMemory(table, time, db): InMemoryTableHeap
    + RecoverTo(time, db): TimeTravelResult
    - ChooseStrategy(): Strategy
    - CollectOperationsAfter(): vector<InverseOp>
}

class InMemoryTableHeap {
    - tuples_: vector<Tuple>
    + InsertTuple(tuple, rid): bool
    + Begin(): Iterator
}

LogManager --> LogRecord
CheckpointManager --> LogManager
RecoveryManager --> LogManager
RecoveryManager --> CheckpointManager
TimeTravelEngine --> LogManager
TimeTravelEngine --> InMemoryTableHeap

@enduml
```

### 19.5 Class Diagram — AI Layer

```plantuml
@startuml AI_Layer_Classes

abstract class IDMLObserver {
    + OnBeforeDML(event): bool
    + OnAfterDML(event): void
}

abstract class IQueryOptimizer {
    + RecommendScanStrategy(stmt, table, out): bool
}

class AIManager {
    - learning_engine_: unique_ptr<LearningEngine>
    - immune_system_: unique_ptr<ImmuneSystem>
    - temporal_index_mgr_: unique_ptr<TemporalIndexManager>
    + {static} Instance(): AIManager&
    + Initialize(...): void
    + Shutdown(): void
    + GetStatus(): AIStatus
}

class LearningEngine {
    - bandit_: unique_ptr<UCB1Bandit>
    - feature_extractor_: unique_ptr<QueryFeatureExtractor>
    + OnAfterDML(event): void
    + RecommendScanStrategy(stmt, table, out): bool
}

class UCB1Bandit {
    - global_pulls_: atomic<uint64_t>[2]
    - global_reward_x10000_: atomic<uint64_t>[2]
    + SelectStrategy(table): ScanStrategy
    + RecordOutcome(strategy, time, table): void
}

class ImmuneSystem {
    - mutation_monitor_: unique_ptr<MutationMonitor>
    - anomaly_detector_: unique_ptr<AnomalyDetector>
    - response_engine_: unique_ptr<ResponseEngine>
    + OnBeforeDML(event): bool
    + OnAfterDML(event): void
    + PeriodicAnalysis(): void
}

class TemporalIndexManager {
    - access_tracker_: unique_ptr<TemporalAccessTracker>
    - hotspot_detector_: unique_ptr<HotspotDetector>
    - snapshot_scheduler_: unique_ptr<SmartSnapshotScheduler>
    + OnTimeTravelQuery(table, timestamp, db): void
    + PeriodicAnalysis(): void
}

class MetricsStore {
    - buffer_: array<MetricEvent, 8192>
    - write_index_: atomic<uint64_t>
    + {static} Instance(): MetricsStore&
    + Record(event): void
}

class DMLObserverRegistry {
    + {static} Instance(): DMLObserverRegistry&
    + Register(observer): void
    + NotifyBefore(event): bool
    + NotifyAfter(event): void
}

LearningEngine --|> IDMLObserver
LearningEngine --|> IQueryOptimizer
ImmuneSystem --|> IDMLObserver

AIManager --> LearningEngine
AIManager --> ImmuneSystem
AIManager --> TemporalIndexManager
LearningEngine --> UCB1Bandit
DMLObserverRegistry "1" o-- "*" IDMLObserver

@enduml
```

### 19.6 Sequence Diagram — SELECT Query Full Flow

```plantuml
@startuml SELECT_Full_Flow

actor Client
participant ConnectionHandler
participant Lexer
participant Parser
participant ExecutionEngine
participant DMLExecutor
participant LearningEngine
participant TableHeap
participant BufferPoolManager
participant DMLObserverRegistry

Client -> ConnectionHandler: "SELECT * FROM users WHERE id = 5;"
ConnectionHandler -> Lexer: Tokenize(query)
Lexer --> ConnectionHandler: Token stream
ConnectionHandler -> Parser: ParseQuery(tokens)
Parser --> ConnectionHandler: SelectStatement AST

ConnectionHandler -> ExecutionEngine: Execute(stmt, session)
ExecutionEngine -> ExecutionEngine: CheckPermissions(SELECT, session)
ExecutionEngine -> ExecutionEngine: dispatch_map_[SELECT]
ExecutionEngine -> DMLExecutor: Select(stmt, session, txn)

DMLExecutor -> LearningEngine: RecommendScanStrategy(stmt, "users")
LearningEngine --> DMLExecutor: INDEX_SCAN

DMLExecutor -> BufferPoolManager: FetchPage(index_root)
BufferPoolManager --> DMLExecutor: Page*
DMLExecutor -> TableHeap: GetTuple(rid, tuple)
DMLExecutor --> DMLExecutor: PredicateEvaluator::Evaluate()

DMLExecutor -> DMLObserverRegistry: NotifyAfter(event)
DMLObserverRegistry -> LearningEngine: OnAfterDML(event)

DMLExecutor --> ExecutionEngine: ExecutionResult(ResultSet)
ExecutionEngine --> ConnectionHandler: ExecutionResult
ConnectionHandler -> ConnectionHandler: ProtocolSerializer::Serialize()
ConnectionHandler --> Client: Formatted response

@enduml
```

### 19.7 Sequence Diagram — Anomaly Detection & Recovery

```plantuml
@startuml Anomaly_Recovery

actor Attacker
participant DMLExecutor
participant DMLObserverRegistry
participant ImmuneSystem
participant MutationMonitor
participant AnomalyDetector
participant ResponseEngine
participant TimeTravelEngine

== Mass Delete Attack ==
Attacker -> DMLExecutor: DELETE FROM users WHERE id > 0;
DMLExecutor -> DMLObserverRegistry: NotifyBefore(DELETE)
DMLObserverRegistry -> ImmuneSystem: OnBeforeDML(event)
ImmuneSystem --> DMLObserverRegistry: true (allowed)
DMLExecutor -> DMLExecutor: Execute DELETE (500 rows)
DMLExecutor -> DMLObserverRegistry: NotifyAfter(DELETE, 500)
DMLObserverRegistry -> ImmuneSystem: OnAfterDML(event)
ImmuneSystem -> MutationMonitor: RecordMutation("users", 500)

== Periodic Analysis (1s) ==
ImmuneSystem -> AnomalyDetector: Analyze(mutation_monitor)
AnomalyDetector -> AnomalyDetector: Z-score = (500 - 2) / 0.8 = 622.5
AnomalyDetector --> ImmuneSystem: AnomalyReport(HIGH)

ImmuneSystem -> ResponseEngine: Respond(report)
ResponseEngine -> ResponseEngine: BlockTable("users")
ResponseEngine -> TimeTravelEngine: RecoverTo(now - 60s)
TimeTravelEngine --> ResponseEngine: success
ResponseEngine -> ResponseEngine: UnblockTable("users")

== Subsequent Attack Blocked ==
Attacker -> DMLExecutor: DELETE FROM users WHERE id > 0;
DMLExecutor -> DMLObserverRegistry: NotifyBefore(DELETE)
DMLObserverRegistry -> ImmuneSystem: OnBeforeDML(event)
ImmuneSystem --> DMLObserverRegistry: false (BLOCKED)
DMLExecutor --> Attacker: ERROR: [IMMUNE] DELETE blocked

@enduml
```

### 19.8 Sequence Diagram — Time Travel Query

```plantuml
@startuml Time_Travel_Flow

actor User
participant DMLExecutor
participant TemporalIndexManager
participant TimeTravelEngine
participant LogManager
participant InMemoryTableHeap

User -> DMLExecutor: SELECT * FROM users AS OF '2026-01-15';

DMLExecutor -> TemporalIndexManager: OnTimeTravelQuery("users", ts, db)
TemporalIndexManager -> TemporalIndexManager: RecordAccess(event)

DMLExecutor -> TimeTravelEngine: BuildSnapshotInMemory("users", ts, db)
TimeTravelEngine -> TimeTravelEngine: ChooseStrategy()
note right: If ts within 1 hour → REVERSE_DELTA\nOtherwise → FORWARD_REPLAY

alt REVERSE_DELTA
    TimeTravelEngine -> TimeTravelEngine: Clone current table to InMemoryTableHeap
    TimeTravelEngine -> LogManager: CollectOperationsAfter(ts)
    LogManager --> TimeTravelEngine: vector<InverseOperation>
    TimeTravelEngine -> InMemoryTableHeap: Apply inverses in reverse order
else FORWARD_REPLAY
    TimeTravelEngine -> InMemoryTableHeap: Create empty
    TimeTravelEngine -> LogManager: Replay operations up to ts
    LogManager --> TimeTravelEngine: Sequential log records
    TimeTravelEngine -> InMemoryTableHeap: Apply operations forward
end

TimeTravelEngine --> DMLExecutor: unique_ptr<InMemoryTableHeap>
DMLExecutor -> DMLExecutor: Execute SELECT on snapshot
DMLExecutor --> User: ResultSet (historical data)

@enduml
```

### 19.9 Sequence Diagram — Crash Recovery (ARIES)

```plantuml
@startuml ARIES_Recovery

participant RecoveryManager
participant CheckpointManager
participant LogManager
participant BufferPoolManager
participant Catalog

== Server Startup ==
RecoveryManager -> CheckpointManager: GetMasterRecord()
CheckpointManager --> RecoveryManager: {checkpoint_lsn, offset}

== Phase 1: Analysis ==
RecoveryManager -> LogManager: Scan from checkpoint_offset
loop For each log record
    RecoveryManager -> RecoveryManager: Build ATT (Active Transaction Table)
    RecoveryManager -> RecoveryManager: Build DPT (Dirty Page Table)
end
note right: ATT = uncommitted transactions\nDPT = pages needing redo

== Phase 2: Redo ==
RecoveryManager -> LogManager: Scan from min(DPT.recovery_lsn)
loop For each log record
    RecoveryManager -> BufferPoolManager: FetchPage(page_id)
    alt page_lsn < record.lsn
        RecoveryManager -> RecoveryManager: Redo operation
        RecoveryManager -> BufferPoolManager: Mark page dirty
    end
end

== Phase 3: Undo ==
loop For each transaction in ATT (backward)
    RecoveryManager -> LogManager: Read via prev_lsn chain
    RecoveryManager -> RecoveryManager: Apply inverse operation
    RecoveryManager -> LogManager: Write CLR record
end

RecoveryManager -> BufferPoolManager: FlushAllPages()
RecoveryManager -> CheckpointManager: BeginCheckpoint()
note right: Database is now consistent

@enduml
```

---

## 20. File Inventory

### Storage Layer

| File | Purpose |
|------|---------|
| `src/include/storage/storage_interface.h` | ITableStorage, IBufferManager, IDiskManager interfaces |
| `src/include/storage/disk/disk_manager.h` | Physical disk I/O |
| `src/include/storage/page/page.h` | Generic 4KB page container |
| `src/include/storage/page/table_page.h` | Slotted page layout |
| `src/include/storage/page/b_plus_tree_page.h` | B+ tree node base |
| `src/include/storage/page/b_plus_tree_leaf_page.h` | Leaf node |
| `src/include/storage/page/b_plus_tree_internal_page.h` | Internal node |
| `src/include/storage/page/free_page_manager.h` | Bitmap page allocation |
| `src/include/storage/table/tuple.h` | Row representation |
| `src/include/storage/table/schema.h` | Column definitions |
| `src/include/storage/table/column.h` | Single column metadata |
| `src/include/storage/table/table_heap.h` | Heap-file table storage |
| `src/include/storage/table/in_memory_table_heap.h` | RAM-based table |
| `src/include/storage/index/b_plus_tree.h` | B+ tree index |
| `src/include/storage/index/index_key.h` | Generic key wrapper |
| `src/include/storage/index/index_iterator.h` | Index scan iterator |

### Buffer Pool

| File | Purpose |
|------|---------|
| `src/include/buffer/buffer_pool_manager.h` | Standard single-mutex BPM |
| `src/include/buffer/partitioned_buffer_pool_manager.h` | Hash-partitioned BPM |
| `src/include/buffer/adaptive_buffer_pool.h` | Self-sizing BPM |
| `src/include/buffer/adaptive_distributed_buffer_pool.h` | Partitioned + adaptive |
| `src/include/buffer/replacer.h` | Abstract replacer interface |
| `src/include/buffer/lru_replacer.h` | LRU eviction |
| `src/include/buffer/clock_replacer.h` | Clock/second-chance eviction |
| `src/include/buffer/page_guard.h` | RAII page access |

### Catalog

| File | Purpose |
|------|---------|
| `src/include/catalog/catalog.h` | Schema metadata repository |
| `src/include/catalog/table_metadata.h` | Table schema + storage info |
| `src/include/catalog/index_info.h` | Index metadata |
| `src/include/catalog/foreign_key.h` | FK constraint builder |

### Parser

| File | Purpose |
|------|---------|
| `src/include/parser/token.h` | 155+ token types |
| `src/include/parser/lexer.h` | SQL/CQL tokenizer |
| `src/include/parser/parser.h` | Recursive descent parser |
| `src/include/parser/statement.h` | 30+ statement AST classes |
| `src/include/parser/advanced_statements.h` | JOIN, aggregate, FK types |
| `src/include/parser/extended_statements.h` | ALTER, VIEW, ANALYZE, EXPLAIN |

### Execution

| File | Purpose |
|------|---------|
| `src/include/execution/execution_engine.h` | Dispatch map coordinator |
| `src/include/execution/executor_factory.h` | Statement handler registry |
| `src/include/execution/executor_context.h` | Shared executor state |
| `src/include/execution/execution_result.h` | Universal response format |
| `src/include/execution/ddl_executor.h` | Schema operations |
| `src/include/execution/dml_executor.h` | Data operations |
| `src/include/execution/system_executor.h` | SHOW commands |
| `src/include/execution/user_executor.h` | User management |
| `src/include/execution/database_executor.h` | Database lifecycle |
| `src/include/execution/transaction_executor.h` | Transaction control |
| `src/include/execution/predicate_evaluator.h` | WHERE evaluation |

### Concurrency

| File | Purpose |
|------|---------|
| `src/include/concurrency/transaction.h` | Transaction state |
| `src/include/concurrency/transaction_manager.h` | Transaction lifecycle |
| `src/include/concurrency/lock_manager.h` | Two-phase locking |

### Recovery

| File | Purpose |
|------|---------|
| `src/include/recovery/log_record.h` | ARIES log record structure |
| `src/include/recovery/log_manager.h` | WAL with double buffering |
| `src/include/recovery/log_page_reader.h` | Cached log reading |
| `src/include/recovery/checkpoint_manager.h` | Fuzzy checkpointing |
| `src/include/recovery/recovery_manager.h` | ARIES crash recovery |
| `src/include/recovery/time_travel_engine.h` | Reverse delta time travel |
| `src/include/recovery/snapshot_manager.h` | Snapshot building facade |

### Network

| File | Purpose |
|------|---------|
| `src/include/network/chronos_server.h` | Multi-protocol server |
| `src/include/network/connection_handler.h` | Per-connection handler |
| `src/include/network/session_context.h` | Connection state |
| `src/include/network/protocol.h` | TEXT/JSON/BINARY protocols |
| `src/include/network/packet.h` | Wire format |
| `src/include/network/database_registry.h` | Multi-database registry |
| `src/include/network/chronos_client.h` | Client connection library |

### Common

| File | Purpose |
|------|---------|
| `src/include/common/config.h` | System constants |
| `src/include/common/config_manager.h` | Configuration file manager |
| `src/include/common/auth_manager.h` | RBAC authentication |
| `src/include/common/logger.h` | Production logging system |
| `src/include/common/value.h` | Typed data value |
| `src/include/common/type.h` | Type system (INTEGER, VARCHAR, ...) |
| `src/include/common/rid.h` | Row identifier (page_id, slot) |
| `src/include/common/rwlatch.h` | Reader-writer lock |
| `src/include/common/thread_pool.h` | Fixed-size thread pool |
| `src/include/common/exception.h` | Custom exceptions |
| `src/include/common/encryption.h` | Optional encryption |
| `src/include/common/crc32.h` | Checksum utilities |

### AI Layer (35 new files)

| File | Purpose |
|------|---------|
| `src/include/ai/ai_config.h` | All AI constants |
| `src/include/ai/metrics_store.h` | Lock-free ring buffer |
| `src/include/ai/dml_observer.h` | Observer interface + registry |
| `src/include/ai/ai_scheduler.h` | Background task scheduler |
| `src/include/ai/ai_manager.h` | Top-level AI coordinator |
| `src/include/ai/learning/query_features.h` | Feature extraction |
| `src/include/ai/learning/bandit.h` | UCB1 multi-armed bandit |
| `src/include/ai/learning/learning_engine.h` | Learning orchestrator |
| `src/include/ai/immune/mutation_monitor.h` | Mutation tracking |
| `src/include/ai/immune/user_profiler.h` | User behavior profiling |
| `src/include/ai/immune/anomaly_detector.h` | Z-score detection |
| `src/include/ai/immune/response_engine.h` | Graduated response |
| `src/include/ai/immune/immune_system.h` | Immune orchestrator |
| `src/include/ai/temporal/access_tracker.h` | Access tracking |
| `src/include/ai/temporal/hotspot_detector.h` | DBSCAN + CUSUM |
| `src/include/ai/temporal/snapshot_scheduler.h` | Smart scheduling |
| `src/include/ai/temporal/retention_manager.h` | WAL retention |
| `src/include/ai/temporal/temporal_index_manager.h` | Temporal orchestrator |
| + 17 corresponding `.cpp` implementation files | |

---

## 21. Algorithm Reference

### 21.1 UCB1 Multi-Armed Bandit

```
Selection:  a* = argmax_a [ Q̂(a) + c · √(ln N / N_a) ]

    Q̂(a) = total_reward(a) / N_a          (estimated arm value)
    c    = √2 ≈ 1.414                      (exploration constant)
    N    = total pulls across all arms
    N_a  = pulls for arm a

Reward:  r(t) = 1.0 / (1.0 + t_ms / 100.0)

    Bounded in (0, 1], smooth, differentiable
    Per-table contextual learning after 10+ pulls per arm
```

### 21.2 Z-Score Anomaly Detection

```
z = (x - μ) / σ

    x = current mutation rate (mutations/sec)
    μ = mean of last 100 intervals
    σ = standard deviation

Thresholds:
    z < 2.0  → NONE   (95.4% of data falls within ±2σ)
    z ≥ 2.0  → LOW    (4.6% false positive rate)
    z ≥ 3.0  → MEDIUM (0.3% false positive rate)
    z ≥ 4.0  → HIGH   (0.006% false positive rate)
```

### 21.3 DBSCAN Clustering (1D Simplified)

```
Parameters: epsilon = 60s, min_points = 5

1. Sort queried timestamps
2. Group consecutive points within epsilon distance
3. Clusters with ≥ min_points become hotspots
4. center = mean(cluster), density = count / range

Complexity: O(n) for sorted 1D data
```

### 21.4 CUSUM Change-Point Detection

```
Parameters: threshold = 4.0σ, drift = 0.5σ

S⁺ = max(0, S⁺ + (x_i - μ - drift))    // Detect upward shift
S⁻ = max(0, S⁻ + (μ - x_i - drift))    // Detect downward shift

If S⁺ > threshold OR S⁻ > threshold → Change point
    → Reset S⁺ = S⁻ = 0

Properties: Sensitive to sustained mean shifts, O(n) single pass
```

### 21.5 LRU Replacement

```
Data structure: Doubly-linked list + hash map
    Insert: append to front of list, add to map       O(1)
    Access: move to front via map lookup               O(1)
    Evict:  remove from back (least recently used)     O(1)
```

### 21.6 Clock Replacement (Second Chance)

```
Data structure: Circular array + reference bits
    On access: set reference bit = 1
    On evict:  scan clockwise
        if ref_bit = 1 → set to 0, advance
        if ref_bit = 0 → evict this frame

Lower overhead than LRU (no list operations)
```

### 21.7 ARIES Recovery

```
Phase 1 — Analysis:
    Scan log from last checkpoint → Build ATT + DPT

Phase 2 — Redo (History Repeating):
    Replay all operations forward → Restore pre-crash state

Phase 3 — Undo (Loser Rollback):
    Walk prev_lsn chains backwards → Undo uncommitted transactions
    Write CLR records → Crash-safe undo
```

### 21.8 Reverse Delta Time Travel

```
Current state: [A=10, B=20, C=30]
Target: time T2 (after A=10 but before B, C inserts)

    Start with current → [A=10, B=20, C=30]
    Undo LSN 4 (INSERT C=30) → [A=10, B=20]
    Undo LSN 3 (INSERT B=20) → [A=10]
    Stop: reached T2

Complexity: O(delta) from current time to target
    (vs. O(total_history) for forward replay)
```

---

*ChronosDB Complete System Documentation — Version 1.0 — February 2026*
*C++20 — ~100+ source files — ~15,000+ lines of code*
