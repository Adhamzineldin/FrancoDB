#pragma once

#include <vector>
#include "execution/executors/abstract_executor.h"
#include "parser/statement.h"
#include "storage/table/tuple.h"
#include "catalog/index_info.h"
#include "storage/index/index_key.h"
#include "common/config.h"
#include "common/exception.h"

namespace francodb {

class IndexScanExecutor : public AbstractExecutor {
public:
    IndexScanExecutor(ExecutorContext *exec_ctx, SelectStatement *plan, IndexInfo *index_info, Value lookup_value, Transaction *txn = nullptr)
        : AbstractExecutor(exec_ctx), 
          plan_(plan), 
          index_info_(index_info), 
          lookup_value_(lookup_value),
          table_info_(nullptr),
          txn_(txn) {}

    void Init() override;
    bool Next(Tuple *tuple) override;
    const Schema *GetOutputSchema() override;

private:
    SelectStatement *plan_;
    IndexInfo *index_info_;
    Value lookup_value_;     // The value we are searching for (e.g., 100)
    
    TableMetadata *table_info_;
    std::vector<RID> result_rids_; // The "Hit List" returned by the Index
    size_t cursor_ = 0;
    Transaction *txn_;
};

} // namespace francodb