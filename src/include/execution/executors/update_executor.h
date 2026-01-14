#pragma once

#include <vector>
#include "execution/executors/abstract_executor.h"
#include "parser/statement.h"
#include "storage/table/tuple.h"
#include "catalog/index_info.h"
#include "storage/index/index_key.h"

namespace francodb {

class UpdateExecutor : public AbstractExecutor {
public:
    UpdateExecutor(ExecutorContext *exec_ctx, UpdateStatement *plan, Transaction *txn = nullptr)
        : AbstractExecutor(exec_ctx), plan_(plan), table_info_(nullptr), txn_(txn) {}

    void Init() override;
    bool Next(Tuple *tuple) override;
    const Schema *GetOutputSchema() override;

private:
    Tuple CreateUpdatedTuple(const Tuple &old_tuple);
    bool EvaluatePredicate(const Tuple &tuple);

    UpdateStatement *plan_;
    TableMetadata *table_info_;
    bool is_finished_ = false;
    Transaction *txn_;
};

} // namespace francodb