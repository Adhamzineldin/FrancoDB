#pragma once

#include <vector>
#include <memory>
#include <string>
#include "execution/execution_result.h" 
#include "execution/executor_context.h"
#include "parser/statement.h"
#include "concurrency/transaction.h"
#include "catalog/catalog.h"

namespace francodb {
    class ExecutionEngine {
    public:
        ExecutionEngine(BufferPoolManager *bpm, Catalog *catalog);
        ~ExecutionEngine();
        
        Transaction* GetCurrentTransaction();
        Transaction* GetCurrentTransactionForWrite();
        void AutoCommitIfNeeded();

        // CHANGE: Returns Result Object instead of printing
        ExecutionResult Execute(Statement *stmt);

    private:
        std::string ValueToString(const Value &v);
        
        // Internal helpers now return ExecutionResult
        ExecutionResult ExecuteCreate(CreateStatement *stmt);
        ExecutionResult ExecuteCreateIndex(CreateIndexStatement *stmt);
        ExecutionResult ExecuteInsert(InsertStatement *stmt);
        ExecutionResult ExecuteSelect(SelectStatement *stmt);
        ExecutionResult ExecuteDrop(DropStatement *stmt);
        ExecutionResult ExecuteDelete(DeleteStatement *stmt);
        ExecutionResult ExecuteUpdate(UpdateStatement *stmt);
        ExecutionResult ExecuteBegin();
        ExecutionResult ExecuteRollback();
        ExecutionResult ExecuteCommit();

        Catalog *catalog_;
        ExecutorContext *exec_ctx_;
        Transaction *current_transaction_;
        int next_txn_id_;
        bool in_explicit_transaction_;
    };
} // namespace francodb