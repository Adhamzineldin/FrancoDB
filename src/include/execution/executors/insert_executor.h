#pragma once

#include <memory>
#include <vector>
#include "execution/executors/abstract_executor.h"
#include "parser/statement.h"
#include "storage/table/tuple.h"
#include "common/exception.h"
#include "catalog/index_info.h"

namespace chronosdb {

    class InsertExecutor : public AbstractExecutor {
        
        struct CachedConstraint {
            uint32_t col_idx;
            std::string op;
            Value limit_value;
        };
    public:
        InsertExecutor(ExecutorContext *exec_ctx, InsertStatement *plan, Transaction *txn = nullptr)
            : AbstractExecutor(exec_ctx), 
              plan_(plan), 
              table_info_(nullptr),
              txn_(txn) {} // <--- FIX 2: Initialize pointer

        void Init() override;
        bool Next(Tuple *tuple) override;
        const Schema *GetOutputSchema() override;

        // Get the number of rows inserted (for multi-row insert support)
        size_t GetInsertedCount() const { return inserted_count_; }

    private:
        InsertStatement *plan_;
        TableMetadata *table_info_;
        bool is_finished_ = false;
        Transaction *txn_;
        std::vector<IndexInfo *> table_indexes_;
        std::vector<CachedConstraint> cached_constraints_;
        size_t current_row_idx_ = 0;  // For multi-row insert support
        size_t inserted_count_ = 0;   // Track number of rows inserted
    };

} // namespace chronosdb