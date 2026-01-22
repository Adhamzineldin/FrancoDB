# FrancoDB Architecture: Solving God Class & Switch Statement

## The Problem (BEFORE)

### God Class: ExecutionEngine
The `ExecutionEngine` class had **25+ methods** doing completely different things:

```cpp
class ExecutionEngine {
    // DDL (Schema Operations)
    ExecutionResult ExecuteCreate(...);
    ExecutionResult ExecuteCreateIndex(...);
    ExecutionResult ExecuteDrop(...);
    ExecutionResult ExecuteAlterTable(...);
    
    // DML (Data Operations)
    ExecutionResult ExecuteInsert(...);
    ExecutionResult ExecuteSelect(...);
    ExecutionResult ExecuteUpdate(...);
    ExecutionResult ExecuteDelete(...);
    
    // Transactions
    ExecutionResult ExecuteBegin(...);
    ExecutionResult ExecuteCommit(...);
    ExecutionResult ExecuteRollback(...);
    
    // User Management
    ExecutionResult ExecuteCreateUser(...);
    ExecutionResult ExecuteAlterUserRole(...);
    ExecutionResult ExecuteDeleteUser(...);
    
    // System Commands
    ExecutionResult ExecuteShowDatabases(...);
    ExecutionResult ExecuteShowTables(...);
    ExecutionResult ExecuteDescribeTable(...);
    // ... and more
};
```

This violates **Single Responsibility Principle** - the class has 6+ reasons to change.

### Giant Switch Statement (200+ lines)
```cpp
ExecutionResult ExecutionEngine::Execute(Statement* stmt, SessionContext* session) {
    switch (stmt->GetType()) {
        case StatementType::CREATE_INDEX: return ExecuteCreateIndex(...);
        case StatementType::CREATE: return ExecuteCreate(...);
        case StatementType::DROP: return ExecuteDrop(...);
        case StatementType::INSERT: return ExecuteInsert(...);
        case StatementType::SELECT: return ExecuteSelect(...);
        case StatementType::UPDATE_CMD: return ExecuteUpdate(...);
        case StatementType::DELETE_CMD: return ExecuteDelete(...);
        case StatementType::BEGIN: return ExecuteBegin();
        case StatementType::COMMIT: return ExecuteCommit();
        // ... 20+ MORE CASES
    }
}
```

This violates **Open/Closed Principle** - adding a new statement requires modifying this file.

---

## The Solution (AFTER)

### 1. Extract Specialized Classes (SRP)

```
ExecutionEngine (God Class)
    ├── DDLExecutor          → CREATE, DROP, ALTER, DESCRIBE
    ├── DMLExecutor          → INSERT, SELECT, UPDATE, DELETE
    ├── TransactionManager   → BEGIN, COMMIT, ROLLBACK
    └── ExecutorFactory      → Routes statements to executors
```

**Files Created:**
- `src/include/execution/ddl_executor.h` + `src/execution/ddl_executor.cpp`
- `src/include/execution/dml_executor.h` + `src/execution/dml_executor.cpp`
- `src/include/concurrency/transaction_manager.h`
- `src/include/execution/executor_factory.h`
- `src/execution/executor_registry.cpp`

### 2. Factory Pattern (OCP)

Instead of a switch statement, use a **registry of executors**:

```cpp
// executor_factory.h
class ExecutorFactory {
    std::unordered_map<StatementType, ExecutorFunc> executors_;
    
public:
    void Register(StatementType type, ExecutorFunc func);
    ExecutionResult Execute(Statement* stmt, ...);
};
```

### 3. How It All Connects

```
┌─────────────────────────────────────────────────────────────────┐
│                         CLIENT REQUEST                           │
│                    "INSERT INTO users ..."                       │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                          PARSER                                  │
│              Creates InsertStatement object                      │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                     EXECUTION ENGINE                             │
│                                                                  │
│   ExecutionResult Execute(Statement* stmt) {                     │
│       // OLD WAY (switch):                                       │
│       // switch(stmt->GetType()) { case INSERT: ... }            │
│                                                                  │
│       // NEW WAY (factory):                                      │
│       return ExecutorFactory::Instance()                         │
│           .Execute(stmt, ctx, session, txn);                     │
│   }                                                              │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                     EXECUTOR FACTORY                             │
│                                                                  │
│   ExecutionResult Execute(Statement* stmt, ...) {                │
│       auto executor = executors_[stmt->GetType()];  // O(1) lookup│
│       return executor(stmt, ctx, session, txn);                  │
│   }                                                              │
└─────────────────────────┬───────────────────────────────────────┘
                          │
            ┌─────────────┴─────────────┐
            │                           │
            ▼                           ▼
┌───────────────────────┐   ┌───────────────────────┐
│     DML EXECUTOR      │   │     DDL EXECUTOR      │
│                       │   │                       │
│  Insert()             │   │  CreateTable()        │
│  Select()             │   │  DropTable()          │
│  Update()             │   │  CreateIndex()        │
│  Delete()             │   │  DescribeTable()      │
└───────────────────────┘   └───────────────────────┘
```

---

## How to Add a New Statement Type

### BEFORE (Bad - Violates OCP)
1. Add case to giant switch in `execution_engine.cpp`
2. Add `ExecuteXxx()` method to ExecutionEngine class
3. Add method declaration to `execution_engine.h`
4. Recompile ExecutionEngine and all dependents

### AFTER (Good - OCP Compliant)
1. Create your executor function
2. Register it in `executor_registry.cpp`:

```cpp
factory.Register(StatementType::MY_NEW_STATEMENT,
    [](Statement* stmt, ExecutorContext* ctx, SessionContext* session, Transaction* txn)
    -> ExecutionResult {
        auto* my_stmt = dynamic_cast<MyNewStatement*>(stmt);
        // Do stuff...
        return ExecutionResult::Message("Success!");
    });
```

3. Done! No modification to ExecutionEngine needed.

---

## File Structure Summary

```
src/
├── include/
│   ├── execution/
│   │   ├── executor_factory.h      # Factory pattern - routes statements
│   │   ├── ddl_executor.h          # DDL operations (CREATE, DROP, etc.)
│   │   ├── dml_executor.h          # DML operations (INSERT, SELECT, etc.)
│   │   └── predicate_evaluator.h   # Shared WHERE clause logic
│   │
│   └── concurrency/
│       ├── transaction_manager.h   # Transaction lifecycle (BEGIN, COMMIT)
│       └── lock_manager.h          # 2PL with deadlock detection
│
└── execution/
    ├── executor_registry.cpp       # Registers all executors at startup
    ├── ddl_executor.cpp            # DDL implementation
    └── dml_executor.cpp            # DML implementation
```

---

## Integration Status

| Component | Header | Implementation | Integrated |
|-----------|--------|----------------|------------|
| ExecutorFactory | ✅ | ✅ (header-only) | ⚠️ Partial |
| DDLExecutor | ✅ | ✅ | ⚠️ Partial |
| DMLExecutor | ✅ | ✅ | ⚠️ Partial |
| TransactionManager | ✅ | ✅ (header-only) | ⚠️ Partial |
| LockManager | ✅ | ✅ (header-only) | ⚠️ Partial |
| ExecutorRegistry | N/A | ✅ | ⚠️ Partial |

**"Partial" means:** The new architecture is in place and working, but the old 
switch statement in ExecutionEngine is still there for backward compatibility.
To fully migrate:

1. Update ExecutionEngine::Execute() to use ExecutorFactory
2. Gradually move each case from the switch to ExecutorRegistry
3. Remove the switch statement when all cases are migrated

---

## Example: How INSERT Works Now

```cpp
// 1. At startup, executor_registry.cpp registers:
factory.Register(StatementType::INSERT, 
    [](Statement* stmt, ExecutorContext* ctx, ...) {
        auto* insert_stmt = dynamic_cast<InsertStatement*>(stmt);
        InsertExecutor executor(ctx, insert_stmt, txn);
        executor.Init();
        Tuple t;
        executor.Next(&t);
        return ExecutionResult::Message("INSERT 1");
    });

// 2. When user runs "INSERT INTO users VALUES (1, 'bob')"
//    Parser creates InsertStatement with type = StatementType::INSERT

// 3. ExecutionEngine::Execute() is called
ExecutionResult Execute(Statement* stmt, SessionContext* session) {
    // Can now be simplified to:
    return ExecutorFactory::Instance().Execute(stmt, exec_ctx_, session, txn);
}

// 4. Factory looks up INSERT handler and calls it
//    Returns ExecutionResult::Message("INSERT 1")
```

---

## Benefits Achieved

| Principle | Before | After |
|-----------|--------|-------|
| **SRP** | 1 class, 25+ responsibilities | 4+ classes, 1 responsibility each |
| **OCP** | Modify switch for new statement | Register new executor, no modifications |
| **DRY** | Predicate logic duplicated 3x | Single PredicateEvaluator class |
| **DIP** | Executors depend on TableHeap | Executors depend on ITableStorage interface |
| **Testability** | Hard to test (giant class) | Easy to test (small focused classes) |

