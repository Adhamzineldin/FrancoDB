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

    void Init() override {
        table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
        if (table_info_ == nullptr) {
            throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
        }
    }

    bool Next(Tuple *tuple) override {
        (void)tuple;
        if (is_finished_) return false;

        // Store old RID, old tuple (for index removal), and new tuple (for index insertion)
        struct UpdateInfo {
            RID old_rid;
            Tuple old_tuple;
            Tuple new_tuple;
        };
        std::vector<UpdateInfo> updates_to_apply;
        
        // 1. SCAN PHASE
        page_id_t curr_page_id = table_info_->first_page_id_;
        auto bpm = exec_ctx_->GetBufferPoolManager();

        while (curr_page_id != INVALID_PAGE_ID) {
            Page *page = bpm->FetchPage(curr_page_id);
            auto *table_page = reinterpret_cast<TablePage *>(page->GetData());
            
            for (uint32_t i = 0; i < table_page->GetTupleCount(); ++i) {
                RID rid(curr_page_id, i);
                Tuple old_tuple;
                
                if (table_page->GetTuple(rid, &old_tuple, txn_)) {
                    if (EvaluatePredicate(old_tuple)) {
                        // FOUND MATCH! Prepare the NEW tuple.
                        Tuple new_tuple = CreateUpdatedTuple(old_tuple);
                        updates_to_apply.push_back({rid, old_tuple, new_tuple});
                    }
                }
            }
            page_id_t next = table_page->GetNextPageId();
            bpm->UnpinPage(curr_page_id, false);
            curr_page_id = next;
        }

        // 2. APPLY PHASE (Update indexes, Delete Old, Insert New)
        // IMPORTANT: Verify tuple still exists before updating (handles concurrent deletes)
        int count = 0;
        for (const auto &update : updates_to_apply) {
            // Verify the tuple still exists (might have been deleted by another thread)
            Tuple verify_tuple;
            if (!table_info_->table_heap_->GetTuple(update.old_rid, &verify_tuple, txn_)) {
                // Tuple was already deleted by another thread, skip this update
                continue;
            }
            
            // Verify it still matches the predicate (tuple might have been updated)
            if (!EvaluatePredicate(verify_tuple)) {
                // Tuple no longer matches predicate, skip this update
                continue;
            }
            
            // Track modification for rollback (old RID with old tuple)
            if (txn_) {
                txn_->AddModifiedTuple(update.old_rid, verify_tuple, false, plan_->table_name_); // false = not deleted, just updated
            }
            
            // --- CHECK PRIMARY KEY UNIQUENESS (if updating a PK column) ---
            for (uint32_t i = 0; i < table_info_->schema_.GetColumnCount(); i++) {
                const Column &col = table_info_->schema_.GetColumn(i);
                if (col.IsPrimaryKey() && col.GetName() == plan_->target_column_) {
                    Value new_pk_value = update.new_tuple.GetValue(table_info_->schema_, i);
                    
                    // Check if new primary key value already exists (excluding current tuple)
                    auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(plan_->table_name_);
                    for (auto *index : indexes) {
                        if (index->col_name_ == col.GetName()) {
                            GenericKey<8> key;
                            key.SetFromValue(new_pk_value);
                            
                            std::vector<RID> result_rids;
                            if (index->b_plus_tree_->GetValue(key, &result_rids, txn_)) {
                                // Check if any of the RIDs point to non-deleted tuples (excluding current)
                                for (const RID &rid : result_rids) {
                                    if (rid == update.old_rid) continue; // Skip current tuple
                                    Tuple existing_tuple;
                                    if (table_info_->table_heap_->GetTuple(rid, &existing_tuple, txn_)) {
                                        throw Exception(ExceptionType::EXECUTION, 
                                            "PRIMARY KEY violation: Duplicate value for " + col.GetName());
                                    }
                                }
                            }
                            break;
                        }
                    }
                }
            }
            // -----------------------------------------------------------------
            
            // A. Remove old tuple from indexes
            auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(plan_->table_name_);
            for (auto *index : indexes) {
                int col_idx = table_info_->schema_.GetColIdx(index->col_name_);
                Value old_key_val = update.old_tuple.GetValue(table_info_->schema_, col_idx);
                
                GenericKey<8> old_key;
                old_key.SetFromValue(old_key_val);
                
                index->b_plus_tree_->Remove(old_key, txn_);
            }
            
            // B. Delete Old tuple from table (only if not already deleted)
            bool deleted = table_info_->table_heap_->MarkDelete(update.old_rid, txn_);
            if (!deleted) {
                // Tuple was already deleted, skip to next
                continue;
            }
            
            // C. Insert New tuple into table
            RID new_rid;
            bool inserted = table_info_->table_heap_->InsertTuple(update.new_tuple, &new_rid, txn_);
            if (!inserted) {
                // Failed to insert, skip index update
                continue;
            }
            
            // Track new tuple for rollback (new RID - this is an INSERT that needs to be deleted on rollback)
            if (txn_) {
                Tuple empty_tuple;
                txn_->AddModifiedTuple(new_rid, empty_tuple, false, plan_->table_name_);
            }
            
            // D. Add new tuple to indexes
            for (auto *index : indexes) {
                int col_idx = table_info_->schema_.GetColIdx(index->col_name_);
                Value new_key_val = update.new_tuple.GetValue(table_info_->schema_, col_idx);
                
                GenericKey<8> new_key;
                new_key.SetFromValue(new_key_val);
                
                index->b_plus_tree_->Insert(new_key, new_rid, txn_);
            }
            
            count++;
        }
        (void) count;
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
    Transaction *txn_;
};

} // namespace francodb