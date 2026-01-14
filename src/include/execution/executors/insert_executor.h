#pragma once

#include <memory>
#include <vector>
#include "execution/executors/abstract_executor.h"
#include "parser/statement.h"
#include "storage/table/tuple.h"
#include "common/exception.h"
#include "catalog/index_info.h"

namespace francodb {

    class InsertExecutor : public AbstractExecutor {
    public:
        InsertExecutor(ExecutorContext *exec_ctx, InsertStatement *plan, Transaction *txn = nullptr)
            : AbstractExecutor(exec_ctx), 
              plan_(plan), 
              table_info_(nullptr),
              txn_(txn) {} // <--- FIX 2: Initialize pointer

        void Init() override;
        bool Next(Tuple *tuple) override;
        const Schema *GetOutputSchema() override;

    private:
        InsertStatement *plan_;
        TableMetadata *table_info_;
        bool is_finished_ = false;
        Transaction *txn_;
    };

} // namespace francodb