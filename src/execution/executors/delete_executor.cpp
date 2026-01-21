#include "execution/executors/delete_executor.h"
#include "common/exception.h"
#include "catalog/index_info.h"
#include "storage/index/index_key.h"
#include "storage/table/table_page.h"
#include "buffer/buffer_pool_manager.h"
#include "storage/page/page.h"
#include <iostream>
#include <cmath>

namespace francodb {

void DeleteExecutor::Init() {
    table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
    if (table_info_ == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
    }
}

bool DeleteExecutor::Next(Tuple *tuple) {
    (void)tuple; // Unused
    if (is_finished_) return false;

    // 1. Gather all RIDs and tuples to delete
    std::vector<std::pair<RID, Tuple>> tuples_to_delete;
    
    // --- SCAN LOGIC (Similar to SeqScan) ---
    page_id_t curr_page_id = table_info_->first_page_id_;
    auto bpm = exec_ctx_->GetBufferPoolManager();

    while (curr_page_id != INVALID_PAGE_ID) {
        Page *page = bpm->FetchPage(curr_page_id);
        if (page == nullptr) {
            break; // Skip if page fetch fails
        }
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
        if (deleted && txn_) {
            txn_->AddModifiedTuple(rid, tuple, true, plan_->table_name_);
            
            // [ACID] WRITE-AHEAD LOGGING
            Value old_val; 
            if (table_info_->schema_.GetColumnCount() > 0) {
                old_val = tuple.GetValue(table_info_->schema_, 0); // Log first column
            }
            
            LogRecord log_rec(txn_->GetTransactionId(), txn_->GetPrevLSN(), LogRecordType::APPLY_DELETE, plan_->table_name_, old_val);
            LogRecord::lsn_t lsn = exec_ctx_->GetLogManager()->AppendLogRecord(log_rec);
            txn_->SetPrevLSN(lsn);
        }
    }

    // Logging removed to avoid interleaved output during concurrent operations
    is_finished_ = true;
    return false;
}

const Schema *DeleteExecutor::GetOutputSchema() {
    return &table_info_->schema_;
}

bool DeleteExecutor::EvaluatePredicate(const Tuple &tuple) {
    if (plan_->where_clause_.empty()) return true;

    bool result = true;
    for (size_t i = 0; i < plan_->where_clause_.size(); ++i) {
        const auto &cond = plan_->where_clause_[i];
        
        Value tuple_val = tuple.GetValue(table_info_->schema_, table_info_->schema_.GetColIdx(cond.column));
        bool match = false;
        
        // Handle IN operator
        if (cond.op == "IN") {
            for (const auto& in_val : cond.in_values) {
                if (tuple_val.GetTypeId() == TypeId::INTEGER) {
                    if (tuple_val.GetAsInteger() == in_val.GetAsInteger()) {
                        match = true;
                        break;
                    }
                } else if (tuple_val.GetTypeId() == TypeId::DECIMAL) {
                    if (std::abs(tuple_val.GetAsDouble() - in_val.GetAsDouble()) < 0.0001) {
                        match = true;
                        break;
                    }
                } else {
                    if (tuple_val.GetAsString() == in_val.GetAsString()) {
                        match = true;
                        break;
                    }
                }
            }
        } else if (cond.op == "=") {
            match = (tuple_val.GetAsString() == cond.value.GetAsString());
            // Type-specific comparison override
            if (tuple_val.GetTypeId() == TypeId::INTEGER) {
                match = (tuple_val.GetAsInteger() == cond.value.GetAsInteger());
            } else if (tuple_val.GetTypeId() == TypeId::DECIMAL) {
                match = (std::abs(tuple_val.GetAsDouble() - cond.value.GetAsDouble()) < 0.0001);
            }
        } else if (cond.op == ">") {
            if (tuple_val.GetTypeId() == TypeId::INTEGER) match = (tuple_val.GetAsInteger() > cond.value.GetAsInteger());
            else if (tuple_val.GetTypeId() == TypeId::DECIMAL) match = (tuple_val.GetAsDouble() > cond.value.GetAsDouble());
            else match = (tuple_val.GetAsString() > cond.value.GetAsString());
        } else if (cond.op == "<") {
            if (tuple_val.GetTypeId() == TypeId::INTEGER) match = (tuple_val.GetAsInteger() < cond.value.GetAsInteger());
            else if (tuple_val.GetTypeId() == TypeId::DECIMAL) match = (tuple_val.GetAsDouble() < cond.value.GetAsDouble());
            else match = (tuple_val.GetAsString() < cond.value.GetAsString());
        } else if (cond.op == ">=") {
            if (tuple_val.GetTypeId() == TypeId::INTEGER) match = (tuple_val.GetAsInteger() >= cond.value.GetAsInteger());
            else if (tuple_val.GetTypeId() == TypeId::DECIMAL) match = (tuple_val.GetAsDouble() >= cond.value.GetAsDouble());
            else match = (tuple_val.GetAsString() >= cond.value.GetAsString());
        } else if (cond.op == "<=") {
            if (tuple_val.GetTypeId() == TypeId::INTEGER) match = (tuple_val.GetAsInteger() <= cond.value.GetAsInteger());
            else if (tuple_val.GetTypeId() == TypeId::DECIMAL) match = (tuple_val.GetAsDouble() <= cond.value.GetAsDouble());
            else match = (tuple_val.GetAsString() <= cond.value.GetAsString());
        }

        if (i == 0) result = match;
        else {
            if (plan_->where_clause_[i-1].next_logic == LogicType::AND) result = result && match;
            else if (plan_->where_clause_[i-1].next_logic == LogicType::OR) result = result || match;
        }
    }
    return result;
}

} // namespace francodb

