#pragma once

#include <vector>
#include "execution/executors/abstract_executor.h"
#include "parser/statement.h"
#include "storage/table/tuple.h"
#include "catalog/index_info.h"
#include "storage/index/index_key.h"

namespace francodb {

class DeleteExecutor : public AbstractExecutor {
public:
    DeleteExecutor(ExecutorContext *exec_ctx, DeleteStatement *plan, Transaction *txn = nullptr)
        : AbstractExecutor(exec_ctx), plan_(plan), table_info_(nullptr), txn_(txn) {}

    void Init() override {
        table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
        if (table_info_ == nullptr) {
            throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
        }
    }

    bool Next(Tuple *tuple) override {
        (void)tuple; // Unused
        if (is_finished_) return false;

        // 1. Gather all RIDs and tuples to delete
        // We scan FIRST, then delete. This is safer than deleting while iterating.
        // We need tuples to remove entries from indexes
        std::vector<std::pair<RID, Tuple>> tuples_to_delete;
        
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
                        tuples_to_delete.push_back({rid, t});
                    }
                }
            }
            page_id_t next = table_page->GetNextPageId();
            bpm->UnpinPage(curr_page_id, false);
            curr_page_id = next;
        }

        // 2. Perform Deletes (with index updates)
        // IMPORTANT: Verify tuple still exists before deleting (handles concurrent deletes)
        int count = 0;
        for (const auto &pair : tuples_to_delete) {
            const RID &rid = pair.first;
            const Tuple &tuple = pair.second;
            
            // Verify the tuple still exists (might have been deleted by another thread)
            Tuple verify_tuple;
            if (!table_info_->table_heap_->GetTuple(rid, &verify_tuple, txn_)) {
                // Tuple was already deleted, skip
                continue;
            }
            
            // Verify it still matches the predicate
            if (!EvaluatePredicate(verify_tuple)) {
                // Tuple no longer matches predicate, skip
                continue;
            }
            
            // Track modification for rollback
            if (txn_) {
                txn_->AddModifiedTuple(rid, verify_tuple, true, plan_->table_name_); // true = is_deleted
            }
            
            // A. Remove from indexes BEFORE deleting from table
            auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(plan_->table_name_);
            for (auto *index : indexes) {
                int col_idx = table_info_->schema_.GetColIdx(index->col_name_);
                Value key_val = tuple.GetValue(table_info_->schema_, col_idx);
                
                GenericKey<8> key;
                key.SetFromValue(key_val);
                
                index->b_plus_tree_->Remove(key, txn_);
            }
            
            // B. Delete from table (only if not already deleted)
            bool deleted = table_info_->table_heap_->MarkDelete(rid, txn_);
            if (deleted) {
                count++;
            }
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
    Transaction *txn_;
};

} // namespace francodb