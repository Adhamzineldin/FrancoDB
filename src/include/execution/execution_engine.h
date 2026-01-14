#pragma once

#include <vector>
#include <memory>
#include <iostream>
#include <iomanip>
#include <sstream>
#include "execution/executor_context.h"
#include "parser/statement.h"
#include "execution/executors/insert_executor.h"
#include "execution/executors/seq_scan_executor.h"
#include "common/exception.h"
#include "executors/delete_executor.h"
#include "executors/index_scan_executor.h"
#include "executors/update_executor.h"
#include "concurrency/transaction.h"

namespace francodb {
    class ExecutionEngine {
    public:
        ExecutionEngine(BufferPoolManager *bpm, Catalog *catalog);
        ~ExecutionEngine();
        
        Transaction* GetCurrentTransaction();
        Transaction* GetCurrentTransactionForWrite();
        void AutoCommitIfNeeded();

        void Execute(Statement *stmt);

    private:
        std::string ValueToString(const Value &v);
        void PrintPostgresTable(const Schema *schema, const std::vector<std::vector<std::string>> &rows);
        void ExecuteCreate(CreateStatement *stmt);
        void ExecuteCreateIndex(CreateIndexStatement *stmt);
        void ExecuteInsert(InsertStatement *stmt);
        void ExecuteSelect(SelectStatement *stmt);
        void ExecuteDrop(DropStatement *stmt);
        void ExecuteDelete(DeleteStatement *stmt);
        void ExecuteUpdate(UpdateStatement *stmt);
        void ExecuteBegin();
        void ExecuteRollback();
        void ExecuteCommit();

        Catalog *catalog_;
        ExecutorContext *exec_ctx_;
        Transaction *current_transaction_;
        int next_txn_id_;
        bool in_explicit_transaction_; // True if user explicitly called BED2
    };
} // namespace francodb