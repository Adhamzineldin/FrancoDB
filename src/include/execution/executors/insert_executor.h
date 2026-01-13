#pragma once

#include <memory>
#include <vector>
#include "execution/executors/abstract_executor.h"
#include "parser/statement.h"
#include "storage/table/tuple.h"
#include "common/exception.h"

namespace francodb {

    class InsertExecutor : public AbstractExecutor {
    public:
        InsertExecutor(ExecutorContext *exec_ctx, InsertStatement *plan)
            : AbstractExecutor(exec_ctx), 
              plan_(plan), 
              table_info_(nullptr) {} // <--- FIX 2: Initialize pointer

        void Init() override {
            // 1. Look up the table in the Catalog
            table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
            if (table_info_ == nullptr) {
                throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
            }
        }

        // Insert executes everything in one go, then returns false.
        bool Next(Tuple *tuple) override {
            // <--- FIX 3: Silence "unused parameter" warning
            (void)tuple; 
        
            if (is_finished_) {
                return false;
            }

            // 1. Create the Tuple from the Values in the Plan
            Tuple to_insert(plan_->values_, table_info_->schema_);

            // 2. Insert into TableHeap
            // <--- FIX 1: Provide the missing RID argument
            RID rid; // This will hold the PageID and SlotID of where it got saved
            bool success = table_info_->table_heap_->InsertTuple(to_insert, &rid, nullptr);

            if (!success) {
                throw Exception(ExceptionType::EXECUTION, "Failed to insert tuple (Out of space?)");
            }

            is_finished_ = true;
            return false; // We don't produce output rows for INSERT
        }

        const Schema *GetOutputSchema() override { return &table_info_->schema_; }

    private:
        InsertStatement *plan_;
        TableMetadata *table_info_;
        bool is_finished_ = false;
    };

} // namespace francodb