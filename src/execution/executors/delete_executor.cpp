#include "execution/executors/delete_executor.h"
#include "execution/predicate_evaluator.h"
#include "concurrency/lock_manager.h"
#include "common/exception.h"
#include "catalog/index_info.h"
#include "storage/index/index_key.h"
#include "storage/table/table_page.h"
#include "buffer/buffer_pool_manager.h"
#include "buffer/page_guard.h"
#include "storage/page/page.h"
#include <iostream>
#include <cmath>

namespace chronosdb {
    void DeleteExecutor::Init() {
        table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
        if (table_info_ == nullptr) {
            throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
        }
    }

    bool DeleteExecutor::Next(Tuple *tuple) {
        (void) tuple;
        if (is_finished_) return false;

        // 1. Gather all RIDs and tuples to delete
        std::vector<std::pair<RID, Tuple> > tuples_to_delete;
        LockManager *lock_mgr = exec_ctx_->GetLockManager();

        page_id_t curr_page_id = table_info_->first_page_id_;
        auto bpm = exec_ctx_->GetBufferPoolManager();

        while (curr_page_id != INVALID_PAGE_ID) {
            PageGuard guard(bpm, curr_page_id, false);
            if (!guard.IsValid()) break;
            auto *table_page = guard.As<TablePage>();

            for (uint32_t i = 0; i < table_page->GetTupleCount(); ++i) {
                RID rid(curr_page_id, i);
                Tuple t;
                if (table_page->GetTuple(rid, &t, nullptr)) {
                    if (EvaluatePredicate(t)) {
                        tuples_to_delete.push_back({rid, t});
                    }
                }
            }
            curr_page_id = table_page->GetNextPageId();
        }

        // 2. Perform Deletes
        for (const auto &pair: tuples_to_delete) {
            const RID &rid = pair.first;
            const Tuple &tuple = pair.second;

            if (lock_mgr && txn_) {
                if (!lock_mgr->LockRow(txn_->GetTransactionId(), rid, LockMode::EXCLUSIVE)) {
                    throw Exception(ExceptionType::EXECUTION, "Could not acquire lock");
                }
            }

            Tuple verify_tuple;
            if (!table_info_->table_heap_->GetTuple(rid, &verify_tuple, txn_)) continue;
            if (!EvaluatePredicate(verify_tuple)) continue;

            if (txn_) txn_->AddModifiedTuple(rid, verify_tuple, true, plan_->table_name_);

            // A. Remove from indexes
            auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(plan_->table_name_);
            for (auto *index: indexes) {
                int col_idx = table_info_->schema_.GetColIdx(index->col_name_);
                Value key_val = tuple.GetValue(table_info_->schema_, col_idx);
                GenericKey<8> key;
                key.SetFromValue(key_val);
                index->b_plus_tree_->Remove(key, txn_);
            }

            // B. Delete from table
            bool deleted = table_info_->table_heap_->MarkDelete(rid, txn_);
            if (deleted && txn_ && exec_ctx_->GetLogManager()) {
                // OPTIMIZATION: Reserve String Memory for Logging
                std::string tuple_str;
                tuple_str.reserve(table_info_->schema_.GetColumnCount() * 10); // Estimate

                for (uint32_t i = 0; i < table_info_->schema_.GetColumnCount(); i++) {
                    if (i > 0) tuple_str += "|";
                    tuple_str += tuple.GetValue(table_info_->schema_, i).ToString();
                }
                Value old_val(TypeId::VARCHAR, tuple_str);
                LogRecord log_rec(txn_->GetTransactionId(), txn_->GetPrevLSN(),
                                  LogRecordType::APPLY_DELETE, plan_->table_name_, old_val);
                auto lsn = exec_ctx_->GetLogManager()->AppendLogRecord(log_rec);
                txn_->SetPrevLSN(lsn);
            }
        }

        deleted_count_ = tuples_to_delete.size();
        is_finished_ = true;
        return deleted_count_ > 0;
    }

    const Schema *DeleteExecutor::GetOutputSchema() {
        return &table_info_->schema_;
    }

    bool DeleteExecutor::EvaluatePredicate(const Tuple &tuple) {
        // Issue #12 Fix: Use shared PredicateEvaluator to eliminate code duplication
        return PredicateEvaluator::Evaluate(tuple, table_info_->schema_, plan_->where_clause_);
    }
} // namespace chronosdb
