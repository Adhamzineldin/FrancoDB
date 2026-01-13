#pragma once

#include <vector>
#include "execution/executors/abstract_executor.h"
#include "parser/statement.h"
#include "storage/table/tuple.h"

namespace francodb {

class DeleteExecutor : public AbstractExecutor {
public:
    DeleteExecutor(ExecutorContext *exec_ctx, DeleteStatement *plan)
        : AbstractExecutor(exec_ctx), plan_(plan), table_info_(nullptr) {}

    void Init() override {
        table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
        if (table_info_ == nullptr) {
            throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
        }
    }

    bool Next(Tuple *tuple) override {
        (void)tuple; // Unused
        if (is_finished_) return false;

        // 1. Gather all RIDs to delete
        // We scan FIRST, then delete. This is safer than deleting while iterating.
        std::vector<RID> rids_to_delete;
        
        // --- SCAN LOGIC (Similar to SeqScan) ---
        page_id_t curr_page_id = table_info_->first_page_id_;
        auto bpm = exec_ctx_->GetBufferPoolManager();

        while (curr_page_id != INVALID_PAGE_ID) {
            Page *page = bpm->FetchPage(curr_page_id);
            auto *table_page = reinterpret_cast<TablePage *>(page->GetData());
            
            // Loop over all slots in page
            for (uint32_t i = 0; i < table_page->GetTupleCount(); ++i) {
                RID rid(curr_page_id, i);
                Tuple t;
                
                // Read tuple (if it exists)
                if (table_page->GetTuple(rid, &t, nullptr)) {
                    // Check WHERE clause
                    if (EvaluatePredicate(t)) {
                        rids_to_delete.push_back(rid);
                    }
                }
            }
            page_id_t next = table_page->GetNextPageId();
            bpm->UnpinPage(curr_page_id, false);
            curr_page_id = next;
        }

        // 2. Perform Deletes
        int count = 0;
        for (const auto &rid : rids_to_delete) {
            table_info_->table_heap_->MarkDelete(rid, nullptr);
            count++;
        }

        std::cout << "[EXEC] Deleted " << count << " rows." << std::endl;
        is_finished_ = true;
        return false;
    }

    const Schema *GetOutputSchema() override { return &table_info_->schema_; }

private:
    // Helper: Evaluates WHERE clause (Copy-paste from SeqScan usually, or refactor to shared util)
    bool EvaluatePredicate(const Tuple &tuple) {
        if (plan_->where_clause_.empty()) return true;

        bool result = true;
        for (size_t i = 0; i < plan_->where_clause_.size(); ++i) {
            const auto &cond = plan_->where_clause_[i];
            
            Value tuple_val = tuple.GetValue(table_info_->schema_, table_info_->schema_.GetColIdx(cond.column));
            bool match = (tuple_val.GetAsString() == cond.value.GetAsString());
            
            // Type-specific comparison override
            if (tuple_val.GetTypeId() == TypeId::INTEGER) {
                match = (tuple_val.GetAsInteger() == cond.value.GetAsInteger());
            } else if (tuple_val.GetTypeId() == TypeId::DECIMAL) {
                 // Simple float comparison
                 match = (std::abs(tuple_val.GetAsDouble() - cond.value.GetAsDouble()) < 0.0001);
            }

            if (i == 0) result = match;
            else {
                if (plan_->where_clause_[i-1].next_logic == LogicType::AND) result = result && match;
                else if (plan_->where_clause_[i-1].next_logic == LogicType::OR) result = result || match;
            }
        }
        return result;
    }

    DeleteStatement *plan_;
    TableMetadata *table_info_;
    bool is_finished_ = false;
};

} // namespace francodb