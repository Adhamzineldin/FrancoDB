#include "execution/executors/update_executor.h"
#include "execution/predicate_evaluator.h"
#include "concurrency/lock_manager.h"
#include "common/exception.h"
#include "catalog/index_info.h"
#include "storage/index/index_key.h"
#include "storage/table/table_page.h"
#include "buffer/buffer_pool_manager.h"
#include "buffer/page_guard.h"
#include "storage/page/page.h"
#include "storage/table/column.h"
#include <iostream>
#include <cmath>
#include <unordered_set>

namespace francodb {

void UpdateExecutor::Init() {
    table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
    if (table_info_ == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
    }
    
    // SCHEMA VALIDATION: Check if target column exists
    int col_idx = table_info_->schema_.GetColIdx(plan_->target_column_);
    if (col_idx < 0) {
        throw Exception(ExceptionType::EXECUTION, 
            "Column not found: '" + plan_->target_column_ + "'");
    }
    
    // SCHEMA VALIDATION: Check type compatibility and NULL values
    const Column &col = table_info_->schema_.GetColumn(col_idx);
    const Value &val = plan_->new_value_;
    
    // Check if NULL value
    if (val.GetTypeId() == TypeId::VARCHAR && val.GetAsString().empty()) {
        throw Exception(ExceptionType::EXECUTION, 
            "NULL values not allowed: column '" + col.GetName() + "'");
    }
    
    // Check type compatibility
    if (val.GetTypeId() != col.GetType()) {
        if (col.GetType() == TypeId::INTEGER && val.GetTypeId() == TypeId::VARCHAR) {
            try {
                std::stoi(val.GetAsString());
            } catch (...) {
                throw Exception(ExceptionType::EXECUTION, 
                    "Type mismatch for column '" + col.GetName() + "': expected INTEGER");
            }
        } else if (col.GetType() == TypeId::DECIMAL && val.GetTypeId() == TypeId::VARCHAR) {
            try {
                std::stod(val.GetAsString());
            } catch (...) {
                throw Exception(ExceptionType::EXECUTION, 
                    "Type mismatch for column '" + col.GetName() + "': expected DECIMAL");
            }
        } else {
            throw Exception(ExceptionType::EXECUTION, 
                "Type mismatch for column '" + col.GetName() + "'");
        }
    }
}

bool UpdateExecutor::Next(Tuple *tuple) {
    if (is_finished_) return false;
    is_finished_ = true;  // We do all work in one call

    // Store old RID, old tuple (for index removal), and new tuple (for index insertion)
    struct UpdateInfo {
        RID old_rid;
        Tuple old_tuple;
        Tuple new_tuple;
    };
    std::vector<UpdateInfo> updates_to_apply;
    
    // Get lock manager for row-level locking (CONCURRENCY FIX)
    LockManager* lock_mgr = exec_ctx_->GetLockManager();
    
    // 1. SCAN PHASE - Acquire exclusive locks on matching rows
    // Issue #1 Fix: Use PageGuard for RAII-based pin/unpin
    page_id_t curr_page_id = table_info_->first_page_id_;
    auto bpm = exec_ctx_->GetBufferPoolManager();

    while (curr_page_id != INVALID_PAGE_ID) {
        PageGuard guard(bpm, curr_page_id, false);  // Read lock for initial scan
        if (!guard.IsValid()) {
            break; // Skip if page fetch fails
        }
        
        auto *table_page = guard.As<TablePage>();
        
        for (uint32_t i = 0; i < table_page->GetTupleCount(); ++i) {
            RID rid(curr_page_id, i);
            Tuple old_tuple;
            
            if (table_page->GetTuple(rid, &old_tuple, txn_)) {
                if (EvaluatePredicate(old_tuple)) {
                    // CONCURRENCY FIX: Acquire exclusive lock on this row
                    if (lock_mgr && txn_) {
                        txn_id_t txn_id = txn_->GetTransactionId();
                        if (!lock_mgr->LockRow(txn_id, rid, LockMode::EXCLUSIVE)) {
                            // Lock acquisition failed (deadlock, timeout)
                            throw Exception(ExceptionType::EXECUTION, 
                                "Could not acquire lock on row - transaction aborted");
                        }
                    }
                    
                    // Re-read tuple after acquiring lock (another txn might have modified it)
                    Tuple locked_tuple;
                    if (!table_page->GetTuple(rid, &locked_tuple, txn_)) {
                        // Tuple was deleted while we waited for the lock, skip it
                        continue;
                    }
                    
                    // Re-check predicate after acquiring lock
                    if (!EvaluatePredicate(locked_tuple)) {
                        // Tuple was modified and no longer matches, skip it
                        continue;
                    }
                    
                    // FOUND MATCH! Prepare the NEW tuple.
                    Tuple new_tuple = CreateUpdatedTuple(locked_tuple);
                    updates_to_apply.push_back({rid, locked_tuple, new_tuple});
                }
            }
        }
        curr_page_id = table_page->GetNextPageId();
        // PageGuard auto-releases here
    }

    // 2. APPLY PHASE (Update indexes, Delete Old, Insert New)
    // NOTE: We already hold exclusive locks on all rows in updates_to_apply
    int count = 0;
    
    // PERFORMANCE OPTIMIZATION: Pre-check if we're updating a primary key column
    bool updating_pk = false;
    int pk_col_idx = -1;
    for (uint32_t i = 0; i < table_info_->schema_.GetColumnCount(); i++) {
        const Column &col = table_info_->schema_.GetColumn(i);
        if (col.IsPrimaryKey() && col.GetName() == plan_->target_column_) {
            updating_pk = true;
            pk_col_idx = i;
            break;
        }
    }
    
    // PERFORMANCE OPTIMIZATION: For PK updates, build a set of existing values ONCE
    std::unordered_set<std::string> existing_pk_values;
    if (updating_pk && pk_col_idx >= 0) {
        // Also check that all new values are the same (bulk update to same value)
        // In that case, we only need to check uniqueness once
        bool all_same_new_value = true;
        std::string first_new_val;
        for (size_t i = 0; i < updates_to_apply.size(); i++) {
            Value new_pk = updates_to_apply[i].new_tuple.GetValue(table_info_->schema_, pk_col_idx);
            std::string val_str = new_pk.ToString();
            if (i == 0) {
                first_new_val = val_str;
            } else if (val_str != first_new_val) {
                all_same_new_value = false;
                break;
            }
        }
        
        // If all new values are the same and we're updating more than 1 row,
        // this will definitely cause a PK violation
        if (all_same_new_value && updates_to_apply.size() > 1) {
            throw Exception(ExceptionType::EXECUTION, 
                "PRIMARY KEY violation: Bulk update would create " + 
                std::to_string(updates_to_apply.size()) + " duplicate values");
        }
        
        // Build set of existing PK values (excluding rows being updated)
        std::unordered_set<page_id_t> updating_pages;
        for (const auto& upd : updates_to_apply) {
            updating_pages.insert(upd.old_rid.GetPageId());
        }
        
        // Use index if available for faster lookup
        auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(plan_->table_name_);
        bool has_pk_index = false;
        for (auto *index : indexes) {
            if (index->col_name_ == table_info_->schema_.GetColumn(pk_col_idx).GetName()) {
                has_pk_index = true;
                break;
            }
        }
        
        if (!has_pk_index) {
            // Only scan for existing values if no index (index will handle uniqueness)
            page_id_t scan_page_id = table_info_->first_page_id_;
            while (scan_page_id != INVALID_PAGE_ID) {
                Page *page = bpm->FetchPage(scan_page_id);
                if (page == nullptr) break;
                auto *table_page = reinterpret_cast<TablePage *>(page->GetData());
                
                for (uint32_t slot = 0; slot < table_page->GetTupleCount(); slot++) {
                    RID rid(scan_page_id, slot);
                    // Skip rows we're updating
                    bool is_updating = false;
                    for (const auto& upd : updates_to_apply) {
                        if (upd.old_rid == rid) { is_updating = true; break; }
                    }
                    if (is_updating) continue;
                    
                    Tuple existing_tuple;
                    if (table_page->GetTuple(rid, &existing_tuple, txn_)) {
                        Value pk_val = existing_tuple.GetValue(table_info_->schema_, pk_col_idx);
                        existing_pk_values.insert(pk_val.ToString());
                    }
                }
                
                page_id_t next = table_page->GetNextPageId();
                bpm->UnpinPage(scan_page_id, false);
                scan_page_id = next;
            }
        }
    }
    
    // Get indexes once (not per row)
    auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(plan_->table_name_);
    
    // PERFORMANCE: Progress tracking for large updates
    size_t total_updates = updates_to_apply.size();
    size_t progress_interval = std::max(total_updates / 10, size_t(1000));
    bool show_progress = (total_updates > 5000);
    size_t updates_done = 0;
    
    if (show_progress) {
        std::cout << "[UPDATE] Starting bulk update: " << total_updates << " rows" << std::endl;
    }
    
    for (const auto &update : updates_to_apply) {
        // Track modification for rollback (old RID with old tuple)
        if (txn_) {
            txn_->AddModifiedTuple(update.old_rid, update.old_tuple, false, plan_->table_name_);
        }
        
        // --- OPTIMIZED PRIMARY KEY UNIQUENESS CHECK ---
        // Only check if we're updating a PK column, and use pre-built set
        if (updating_pk && pk_col_idx >= 0) {
            Value new_pk_value = update.new_tuple.GetValue(table_info_->schema_, pk_col_idx);
            std::string new_pk_str = new_pk_value.ToString();
            
            // Check against pre-built set (O(1) lookup instead of O(n) scan)
            if (existing_pk_values.find(new_pk_str) != existing_pk_values.end()) {
                throw Exception(ExceptionType::EXECUTION, 
                    "PRIMARY KEY violation: Duplicate value for " + 
                    table_info_->schema_.GetColumn(pk_col_idx).GetName());
            }
            
            // Add the new value to the set (for detecting duplicates within the batch)
            existing_pk_values.insert(new_pk_str);
        }
        // -----------------------------------------------------------------
        
        // A. Remove old tuple from indexes
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
            txn_->AddModifiedTuple(update.old_rid, update.old_tuple, false, plan_->table_name_);
            
            if (exec_ctx_->GetLogManager()) {
                // Serialize old and new tuples as pipe-separated strings for complete recovery
                std::string old_tuple_str, new_tuple_str;
                for (uint32_t i = 0; i < table_info_->schema_.GetColumnCount(); i++) {
                    if (i > 0) {
                        old_tuple_str += "|";
                        new_tuple_str += "|";
                    }
                    old_tuple_str += update.old_tuple.GetValue(table_info_->schema_, i).ToString();
                    new_tuple_str += update.new_tuple.GetValue(table_info_->schema_, i).ToString();
                }
                Value old_val(TypeId::VARCHAR, old_tuple_str);
                Value new_val(TypeId::VARCHAR, new_tuple_str);
            
                LogRecord log_rec(txn_->GetTransactionId(), txn_->GetPrevLSN(), 
                                  LogRecordType::UPDATE, plan_->table_name_, old_val, new_val);
                auto lsn = exec_ctx_->GetLogManager()->AppendLogRecord(log_rec);
                txn_->SetPrevLSN(lsn);
            }
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
        updates_done++;
        
        // Progress reporting for large bulk updates
        if (show_progress && updates_done % progress_interval == 0) {
            int pct = static_cast<int>((updates_done * 100) / total_updates);
            std::cout << "[UPDATE] Progress: " << pct << "% (" << updates_done << "/" << total_updates << ")" << std::endl;
        }
    }
    
    if (show_progress) {
        std::cout << "[UPDATE] Bulk update complete: " << count << " rows updated" << std::endl;
    }
    
    count_ = count;  // Store count for GetUpdateCount()
    return false;    // All work done in this single call
}

const Schema *UpdateExecutor::GetOutputSchema() {
    return &table_info_->schema_;
}

Tuple UpdateExecutor::CreateUpdatedTuple(const Tuple &old_tuple) {
    std::vector<Value> new_values;
    const Schema &schema = table_info_->schema_;

    for (uint32_t i = 0; i < schema.GetColumnCount(); ++i) {
        const Column &col = schema.GetColumn(i);
        std::string col_name = col.GetName();
        
        if (col_name == plan_->target_column_) {
            // Found target. Convert raw value (String) to Schema Type (Int/Dec).
            const Value &raw_val = plan_->new_value_;
            Value final_val;

            try {
                // If types match, use directly
                if (raw_val.GetTypeId() == col.GetType()) {
                    final_val = raw_val; 
                } 
                // CONVERT: String -> Integer
                else if (col.GetType() == TypeId::INTEGER) {
                    if (raw_val.GetTypeId() == TypeId::VARCHAR) {
                        int32_t val = std::stoi(raw_val.GetAsString());
                        final_val = Value(TypeId::INTEGER, val);
                    } else if (raw_val.GetTypeId() == TypeId::DECIMAL) {
                        final_val = Value(TypeId::INTEGER, static_cast<int32_t>(raw_val.GetAsDouble()));
                    } else {
                        // Fallback/Error
                        throw Exception(ExceptionType::EXECUTION, "Cannot cast to INTEGER");
                    }
                } 
                // CONVERT: String -> Decimal
                else if (col.GetType() == TypeId::DECIMAL) {
                    if (raw_val.GetTypeId() == TypeId::VARCHAR) {
                        double val = std::stod(raw_val.GetAsString());
                        final_val = Value(TypeId::DECIMAL, val);
                    } else if (raw_val.GetTypeId() == TypeId::INTEGER) {
                        final_val = Value(TypeId::DECIMAL, static_cast<double>(raw_val.GetAsInteger()));
                    } else {
                        throw Exception(ExceptionType::EXECUTION, "Cannot cast to DECIMAL");
                    }
                }
                // CONVERT: Any -> String
                else if (col.GetType() == TypeId::VARCHAR) {
                    final_val = Value(TypeId::VARCHAR, raw_val.GetAsString());
                }
                else {
                    // Boolean etc.
                    final_val = raw_val;
                }
            } catch (...) {
                throw Exception(ExceptionType::EXECUTION, 
                    "Update Type Mismatch for column '" + col_name + "'");
            }
            new_values.push_back(final_val);
        } else {
            // Keep old value
            new_values.push_back(old_tuple.GetValue(schema, i)); 
        }
    }
    return Tuple(new_values, schema);
}

bool UpdateExecutor::EvaluatePredicate(const Tuple &tuple) {
    // Issue #12 Fix: Use shared PredicateEvaluator to eliminate code duplication
    return PredicateEvaluator::Evaluate(tuple, table_info_->schema_, plan_->where_clause_);
}

} // namespace francodb

