#pragma once

#include <vector>
#include "execution/executors/abstract_executor.h"
#include "parser/statement.h"
#include "storage/table/tuple.h"
#include "catalog/index_info.h"
#include "storage/index/index_key.h"

namespace chronosdb {

class DeleteExecutor : public AbstractExecutor {
public:
    DeleteExecutor(ExecutorContext *exec_ctx, DeleteStatement *plan, Transaction *txn = nullptr)
        : AbstractExecutor(exec_ctx), plan_(plan), table_info_(nullptr), txn_(txn) {}

    void Init() override;
    bool Next(Tuple *tuple) override;
    const Schema *GetOutputSchema() override;
    size_t GetDeletedCount() const { return deleted_count_; }

private:
    bool EvaluatePredicate(const Tuple &tuple);

    DeleteStatement *plan_;
    TableMetadata *table_info_;
    bool is_finished_ = false;
    Transaction *txn_;
    size_t deleted_count_ = 0;
};

} // namespace chronosdb