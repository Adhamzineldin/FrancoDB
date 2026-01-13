#pragma once

#include <vector>
#include "execution/executors/abstract_executor.h"
#include "parser/statement.h"
#include "storage/table/tuple.h"

namespace francodb {

class UpdateExecutor : public AbstractExecutor {
public:
    UpdateExecutor(ExecutorContext *exec_ctx, UpdateStatement *plan)
        : AbstractExecutor(exec_ctx), plan_(plan), table_info_(nullptr) {}

    void Init() override {
        table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
        if (table_info_ == nullptr) {
            throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
        }
    }

    bool Next(Tuple *tuple) override {
        (void)tuple;
        if (is_finished_) return false;

        std::vector<std::pair<RID, Tuple>> updates_to_apply;
        
        // 1. SCAN PHASE
        page_id_t curr_page_id = table_info_->first_page_id_;
        auto bpm = exec_ctx_->GetBufferPoolManager();

        while (curr_page_id != INVALID_PAGE_ID) {
            Page *page = bpm->FetchPage(curr_page_id);
            auto *table_page = reinterpret_cast<TablePage *>(page->GetData());
            
            for (uint32_t i = 0; i < table_page->GetTupleCount(); ++i) {
                RID rid(curr_page_id, i);
                Tuple old_tuple;
                
                if (table_page->GetTuple(rid, &old_tuple, nullptr)) {
                    if (EvaluatePredicate(old_tuple)) {
                        // FOUND MATCH! Prepare the NEW tuple.
                        Tuple new_tuple = CreateUpdatedTuple(old_tuple);
                        updates_to_apply.push_back({rid, new_tuple});
                    }
                }
            }
            page_id_t next = table_page->GetNextPageId();
            bpm->UnpinPage(curr_page_id, false);
            curr_page_id = next;
        }

        // 2. APPLY PHASE (Delete Old, Insert New)
        int count = 0;
        for (const auto &pair : updates_to_apply) {
            // A. Delete Old
            table_info_->table_heap_->MarkDelete(pair.first, nullptr);
            
            // B. Insert New
            RID new_rid;
            table_info_->table_heap_->InsertTuple(pair.second, &new_rid, nullptr);
            
            count++;
        }

        std::cout << "[EXEC] Updated " << count << " rows." << std::endl;
        is_finished_ = true;
        return false;
    }

    const Schema *GetOutputSchema() override { return &table_info_->schema_; }

private:
    // Construct the new tuple based on "SET col = val"
    Tuple CreateUpdatedTuple(const Tuple &old_tuple) {
        std::vector<Value> new_values;
        const Schema &schema = table_info_->schema_;

        // Loop through all columns
        for (uint32_t i = 0; i < schema.GetColumnCount(); ++i) {
            std::string col_name = schema.GetColumn(i).GetName();
            
            // Is this the target column? (e.g., "points")
            if (col_name == plan_->target_column_) {
                new_values.push_back(plan_->new_value_); // Use the new value
            } else {
                new_values.push_back(old_tuple.GetValue(schema, i)); // Keep old value
            }
        }
        return Tuple(new_values, schema);
    }

    // (Same EvaluatePredicate as DeleteExecutor/SeqScan)
    bool EvaluatePredicate(const Tuple &tuple) {
        if (plan_->where_clause_.empty()) return true;

        bool result = true;
        for (size_t i = 0; i < plan_->where_clause_.size(); ++i) {
            const auto &cond = plan_->where_clause_[i];
            Value tuple_val = tuple.GetValue(table_info_->schema_, table_info_->schema_.GetColIdx(cond.column));
            bool match = (tuple_val.GetAsString() == cond.value.GetAsString());
            
            if (tuple_val.GetTypeId() == TypeId::INTEGER) match = (tuple_val.GetAsInteger() == cond.value.GetAsInteger());
            else if (tuple_val.GetTypeId() == TypeId::DECIMAL) match = (std::abs(tuple_val.GetAsDouble() - cond.value.GetAsDouble()) < 0.0001);

            if (i == 0) result = match;
            else {
                if (plan_->where_clause_[i-1].next_logic == LogicType::AND) result = result && match;
                else if (plan_->where_clause_[i-1].next_logic == LogicType::OR) result = result || match;
            }
        }
        return result;
    }

    UpdateStatement *plan_;
    TableMetadata *table_info_;
    bool is_finished_ = false;
};

} // namespace francodb