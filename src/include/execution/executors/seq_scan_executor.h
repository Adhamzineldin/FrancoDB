#pragma once

#include <vector>
#include <cmath> // For std::abs if used
#include "execution/executors/abstract_executor.h"
#include "parser/statement.h"
#include "storage/table/tuple.h"
#include "common/rid.h"
#include "common/exception.h"

namespace francodb {

class SeqScanExecutor : public AbstractExecutor {
public:
    SeqScanExecutor(ExecutorContext *exec_ctx, SelectStatement *plan, Transaction *txn = nullptr)
        : AbstractExecutor(exec_ctx), 
          plan_(plan), 
          table_info_(nullptr),
          txn_(txn) {}

    void Init() override;
    bool Next(Tuple *tuple) override;
    const Schema *GetOutputSchema() override;

private:
    void AdvanceIterator(TablePage *curr_page);
    bool EvaluatePredicate(const Tuple &tuple);

    SelectStatement *plan_;
    TableMetadata *table_info_;
    RID current_rid_;
    Transaction *txn_;
};

} // namespace francodb