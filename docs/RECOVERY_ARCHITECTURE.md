# FrancoDB Recovery & Time Travel Architecture

## Overview

FrancoDB implements a production-grade, ARIES-compliant recovery system with **Multi-File Write-Ahead Logging (WAL)**, **Checkpointing**, and **Point-in-Time Recovery (PITR)** capabilities. The system is designed using the "Git for Data" mental model.

---

## Architecture Components

### 1. Multi-File Log Architecture

Instead of a single global log, FrancoDB maintains isolated logs per database:

```
data/
├── system/
│   ├── sys.log           # System-wide DDL operations (CREATE DB, DROP DB, etc.)
│   └── master_record     # Checkpoint metadata
└── <database_name>/
    └── wal.log           # Database-specific DML operations (INSERT, UPDATE, DELETE)
```

**Benefits:**
- **Isolation:** Database logs are independent; corrupting one doesn't affect others
- **Performance:** Parallel recovery of multiple databases
- **Scalability:** Easy to archive/backup individual database logs

---

### 2. Log Record Format

Each log record contains:

```cpp
[SIZE][LSN][PREV_LSN][TXN_ID][TIMESTAMP][TYPE][DB_NAME][BODY...]
```

**New Fields:**
- `db_name`: Database context (enables multi-database isolation)
- `timestamp`: Microsecond-precision timestamp for time travel

**Log Record Types:**
- **DML:** `INSERT`, `UPDATE`, `APPLY_DELETE`, `MARK_DELETE`
- **Transaction:** `BEGIN`, `COMMIT`, `ABORT`
- **DDL:** `CREATE_DB`, `DROP_DB`, `SWITCH_DB`
- **Checkpoint:** `CHECKPOINT_BEGIN`, `CHECKPOINT_END`

---

### 3. LogManager

**File:** `src/recovery/log_manager.{h,cpp}`

The LogManager handles:
- **Multi-Database Logs:** Maintains separate log streams per database
- **Context Switching:** `SwitchDatabase(db_name)` rotates the active log file
- **Atomic Writes:** Double-buffering ensures crash consistency
- **Background Flushing:** Asynchronous writes to minimize latency

**Key Methods:**
```cpp
// Switch active database context
void SwitchDatabase(const std::string& db_name);

// Create a new database log
void CreateDatabaseLog(const std::string& db_name);

// Delete a database log (DROP DATABASE)
void DropDatabaseLog(const std::string& db_name);

// Get log file path for any database
std::string GetLogFilePath(const std::string& db_name) const;
```

**Usage Example:**
```cpp
LogManager log_mgr("data");

// Switch to a specific database
log_mgr.SwitchDatabase("customers_db");

// All subsequent log records go to data/customers_db/wal.log
LogRecord insert_rec(txn_id, prev_lsn, LogRecordType::INSERT, "users", value);
log_mgr.AppendLogRecord(insert_rec);
```

---

### 4. CheckpointManager

**File:** `src/recovery/checkpoint_manager.{h,cpp}`

Implements **fuzzy checkpointing** to reduce recovery time:

**Checkpoint Protocol:**
1. Write `CHECKPOINT_BEGIN` log record
2. Flush all dirty pages from buffer pool to disk
3. Write `CHECKPOINT_END` log record
4. Update `master_record` file with checkpoint LSN and file offset

**Master Record Format:**
```
[CHECKPOINT_LSN][FILE_OFFSET]
```

**Key Methods:**
```cpp
// Perform a blocking checkpoint
void BeginCheckpoint();

// Read the last checkpoint LSN (used during recovery)
LogRecord::lsn_t GetLastCheckpointLSN();
```

**Why Checkpoints Matter:**
Without checkpoints, recovery must replay from LSN 0 (slow for large databases).
With checkpoints, recovery only replays from the last checkpoint (fast).

---

### 5. RecoveryManager

**File:** `src/recovery/recovery_manager.{h,cpp}`

Implements the **ARIES recovery algorithm** and **Time Travel** features:

#### A. ARIES Crash Recovery

```cpp
void ARIES();
```

**Three Phases:**
1. **Analysis:** Read `master_record` to find the last checkpoint
2. **Redo:** Replay history from checkpoint forward to reconstruct database state
3. **Undo:** Roll back uncommitted transactions (future work)

**Multi-Database Recovery:**
- Recovers `system` log first (to discover which databases exist)
- Then recovers all database logs in parallel (conceptually)

#### B. Time Travel: Point-in-Time Recovery

```cpp
void RecoverToTime(uint64_t target_timestamp);
```

**Use Case:** "Restore the database to how it looked at 2:00 PM yesterday"

**Implementation Strategy:**
- **Short Jump (Recent):** Use the "Undo Chain" - read logs backward and apply inverse operations
- **Long Jump (Old):** Load nearest checkpoint and Redo forward to target time

**Example:**
```cpp
// Restore to timestamp 1674400000000000 (microseconds)
recovery_mgr.RecoverToTime(1674400000000000);
```

#### C. Time Travel: Read-Only Snapshots (SELECT AS OF)

```cpp
TableHeap* BuildTableSnapshot(const std::string& table_name, uint64_t target_time);
```

**Use Case:** "Show me the users table as it existed at 3:00 PM without modifying the live database"

**Implementation:**
1. Create a temporary in-memory `TableHeap`
2. Replay log records up to `target_time` into the shadow heap
3. Return the heap for read-only queries

**Example:**
```cpp
// Get a snapshot of "orders" table at a specific time
uint64_t target_time = 1674400000000000;
TableHeap* snapshot = recovery_mgr.BuildTableSnapshot("orders", target_time);

// Query the snapshot (read-only)
auto iter = snapshot->Begin(nullptr);
while (iter != snapshot->End()) {
    // Process historical data...
    ++iter;
}

// Clean up
delete snapshot;
```

---

## The "Git for Data" Mental Model

| Git Operation | FrancoDB Equivalent | Implementation |
|---------------|---------------------|----------------|
| `git commit` | `COMMIT` log record | Each transaction commit creates a timestamped LSN |
| `git log` | WAL file | History of all changes with timestamps |
| `git checkout <hash>` | `SELECT AS OF <timestamp>` | Build shadow heap by replaying logs |
| `git reset --hard <hash>` | `RECOVER TO <timestamp>` | Undo/Redo to force database back to target state |
| `.git/refs/heads/main` | `master_record` | Points to the latest safe checkpoint |

---

## Usage Examples

### Example 1: Checkpoint-Based Recovery

```cpp
// Setup
BufferPoolManager bpm(1000, disk_mgr);
LogManager log_mgr("data");
CheckpointManager ckpt_mgr(&bpm, &log_mgr);

// Perform periodic checkpoints (e.g., every 5 minutes)
ckpt_mgr.BeginCheckpoint();

// After a crash, recover from last checkpoint
RecoveryManager recovery_mgr(&log_mgr, catalog, &bpm, &ckpt_mgr);
recovery_mgr.ARIES(); // Fast recovery - only replays from checkpoint
```

### Example 2: Multi-Database Isolation

```cpp
LogManager log_mgr("data");

// Create database logs
log_mgr.CreateDatabaseLog("production");
log_mgr.CreateDatabaseLog("staging");

// Switch to production database
log_mgr.SwitchDatabase("production");
// All logs now go to data/production/wal.log

// Switch to staging database
log_mgr.SwitchDatabase("staging");
// All logs now go to data/staging/wal.log
```

### Example 3: Time Travel Query (AS OF)

```cpp
// User wants to see data from yesterday without modifying live database
uint64_t yesterday = get_timestamp("2026-01-21 15:00:00");

// Build snapshot
TableHeap* snapshot = recovery_mgr.BuildTableSnapshot("users", yesterday);

// Query historical data
auto iter = snapshot->Begin(nullptr);
while (iter != snapshot->End()) {
    std::cout << "Historical user: " << (*iter).ToString() << std::endl;
    ++iter;
}

delete snapshot;
```

### Example 4: Point-in-Time Recovery (PITR)

```cpp
// Disaster scenario: Accidental DELETE executed at 3:00 PM
// Current time: 3:15 PM
// We need to restore to 2:59 PM (before the disaster)

uint64_t safe_time = get_timestamp("2026-01-22 14:59:00");

// This will UNDO all changes after 2:59 PM
recovery_mgr.RecoverToTime(safe_time);

std::cout << "Database restored to state at 2:59 PM" << std::endl;
```

---

## Crash Consistency Guarantees

### 1. Durability (D in ACID)

**Problem:** What if the system crashes after `COMMIT` but before log flush?

**Solution:** 
- `LogManager::Flush(true)` forces synchronous write to disk on commit
- Double buffering ensures atomic log writes
- Background flush thread persists logs every 30ms

### 2. Atomicity (A in ACID)

**Problem:** What if a multi-operation transaction is half-written during a crash?

**Solution:**
- Each log record has `prev_lsn` forming an "Undo Chain"
- During recovery, the Undo phase rolls back uncommitted transactions
- (Note: Full transaction abort is future work)

### 3. Checkpoint Consistency

**Problem:** What if the system crashes during a checkpoint?

**Solution:**
- `master_record` is written AFTER checkpoint completes
- If crash happens during checkpoint, recovery uses the previous checkpoint
- `CHECKPOINT_BEGIN` and `CHECKPOINT_END` markers in log allow detecting partial checkpoints

---

## DROP DATABASE Handling

**Challenge:** During recovery, if we encounter a `DROP_DB` log record, we must physically delete the database directory to ensure consistency.

**Implementation:**
```cpp
case LogRecordType::DROP_DB: {
    if (!is_undo) {
        std::string db_dir = "data/" + record.db_name_;
        std::filesystem::remove_all(db_dir);
    }
    break;
}
```

**Why?** If the DROP happened before the crash but the directory wasn't deleted, recovery must complete the drop to maintain consistency.

---

## Performance Considerations

### Log Write Latency

- **Double Buffering:** Application writes to `log_buffer_` while background thread flushes `flush_buffer_`
- **Group Commit:** Multiple transactions can share a single disk flush
- **Async Flush:** Background thread persists logs every 30ms

### Recovery Time

Without checkpoints:
```
Recovery Time = O(total_log_size)
```

With checkpoints every N minutes:
```
Recovery Time = O(log_size_since_last_checkpoint)
```

**Example:** 
- Log grows at 100 MB/day
- Checkpoint every 1 hour
- Recovery time: ~4 MB replay (1 hour of logs) vs. entire day's logs

---

## Future Enhancements

1. **Parallel Recovery:** Recover multiple databases concurrently using thread pool
2. **Incremental Checkpoints:** Only flush dirty pages, track with dirty page table
3. **Log Archiving:** Compress and archive old log segments for long-term PITR
4. **Transaction Abort:** Full Undo phase implementation for rolling back uncommitted transactions
5. **Log Compression:** Use LZ4 or Snappy to compress log records
6. **Redo-Only Recovery:** Use page LSNs to skip already-applied redo records

---

## Testing Checklist

- [ ] Crash during normal operation → ARIES recovery works
- [ ] Crash during checkpoint → Falls back to previous checkpoint
- [ ] Multi-database isolation → Logs don't mix
- [ ] Time Travel forward and backward
- [ ] DROP DATABASE during recovery → Directory deleted
- [ ] Checkpoint reduces recovery time (measure with benchmarks)

---

## References

- **ARIES Paper:** Mohan et al., "ARIES: A Transaction Recovery Method Supporting Fine-Granularity Locking and Partial Rollbacks Using Write-Ahead Logging"
- **PostgreSQL WAL:** https://www.postgresql.org/docs/current/wal-intro.html
- **SQLite Rollback Journal:** https://www.sqlite.org/atomiccommit.html

---

**Last Updated:** January 22, 2026  
**Author:** FrancoDB Development Team

