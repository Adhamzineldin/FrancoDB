#include "execution/executors/update_executor.h"
#include "execution/predicate_evaluator.h"
#include "concurrency/lock_manager.h"
#include "common/exception.h"
#include "catalog/index_info.h"
#include "storage/index/index_key.h"
#include "storage/table/table_page.h"
#include "buffer/buffer_pool_manager.h"
#include "storage/page/page.h"
#include "storage/table/column.h"
#include <iostream>
#include <cmath>

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
    (void)tuple;
    if (is_finished_) return false;

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
    page_id_t curr_page_id = table_info_->first_page_id_;
    auto bpm = exec_ctx_->GetBufferPoolManager();

    while (curr_page_id != INVALID_PAGE_ID) {
        Page *page = bpm->FetchPage(curr_page_id);
        if (page == nullptr) {
            break; // Skip if page fetch fails
        }
        
        try {
        auto *table_page = reinterpret_cast<TablePage *>(page->GetData());
        
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
                            bpm->UnpinPage(curr_page_id, false);
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
        page_id_t next = table_page->GetNextPageId();
        bpm->UnpinPage(curr_page_id, false);
        curr_page_id = next;
            
        } catch (...) {
            // CRITICAL: Unpin if we crash here (e.g. Type mismatch exception)
            bpm->UnpinPage(curr_page_id, false);
            throw; 
        }
    }

    // 2. APPLY PHASE (Update indexes, Delete Old, Insert New)
    // NOTE: We already hold exclusive locks on all rows in updates_to_apply
    int count = 0;
    for (const auto &update : updates_to_apply) {
        // Track modification for rollback (old RID with old tuple)
        if (txn_) {
            txn_->AddModifiedTuple(update.old_rid, update.old_tuple, false, plan_->table_name_);
        }
        
        // --- CHECK PRIMARY KEY UNIQUENESS (if updating a PK column) ---
        for (uint32_t i = 0; i < table_info_->schema_.GetColumnCount(); i++) {
            const Column &col = table_info_->schema_.GetColumn(i);
            
            if (col.IsPrimaryKey() && col.GetName() == plan_->target_column_) {
                Value new_pk_value = update.new_tuple.GetValue(table_info_->schema_, i);
                bool found_duplicate = false;
                
                // Try to use index first (faster)
                auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(plan_->table_name_);
                bool has_index = false;
                for (auto *index : indexes) {
                    if (index->col_name_ == col.GetName()) {
                        has_index = true;
                        GenericKey<8> key;
                        key.SetFromValue(new_pk_value);
                        
                        std::vector<RID> result_rids;
                        if (index->b_plus_tree_->GetValue(key, &result_rids, txn_)) {
                            // Check if any of the RIDs point to non-deleted tuples (excluding current)
                            for (const RID &rid : result_rids) {
                                if (rid == update.old_rid) continue; // Skip current tuple
                                Tuple existing_tuple;
                                if (table_info_->table_heap_->GetTuple(rid, &existing_tuple, txn_)) {
                                    found_duplicate = true;
                                    break;
                                }
                            }
                        }
                        break;
                    }
                }
                
                // Fallback: Sequential scan if no index exists
                if (!has_index) {
                    page_id_t scan_page_id = table_info_->first_page_id_;
                    while (scan_page_id != INVALID_PAGE_ID && !found_duplicate) {
                        Page *page = bpm->FetchPage(scan_page_id);
                        if (page == nullptr) {
                            break; // Skip if page fetch fails
                        }
                        auto *table_page = reinterpret_cast<TablePage *>(page->GetData());
                        
                        for (uint32_t slot = 0; slot < table_page->GetTupleCount(); slot++) {
                            RID rid(scan_page_id, slot);
                            if (rid == update.old_rid) continue; // Skip the tuple we're updating
                            
                            Tuple existing_tuple;
                            if (table_page->GetTuple(rid, &existing_tuple, txn_)) {
                                Value existing_pk = existing_tuple.GetValue(table_info_->schema_, i);
                                // Compare values based on type
                                bool matches = false;
                                if (existing_pk.GetTypeId() == new_pk_value.GetTypeId()) {
                                    if (existing_pk.GetTypeId() == TypeId::INTEGER) {
                                        matches = (existing_pk.GetAsInteger() == new_pk_value.GetAsInteger());
                                    } else if (existing_pk.GetTypeId() == TypeId::DECIMAL) {
                                        matches = (std::abs(existing_pk.GetAsDouble() - new_pk_value.GetAsDouble()) < 0.0001);
                                    } else if (existing_pk.GetTypeId() == TypeId::VARCHAR) {
                                        matches = (existing_pk.GetAsString() == new_pk_value.GetAsString());
                                    } else {
                                        matches = (existing_pk.GetAsInteger() == new_pk_value.GetAsInteger());
                                    }
                                }
                                if (matches) {
                                    found_duplicate = true;
                                    break;
                                }
                            }
                        }
                        
                        page_id_t next = table_page->GetNextPageId();
                        bpm->UnpinPage(scan_page_id, false);
                        scan_page_id = next;
                    }
                }
                
                if (found_duplicate) {
                    throw Exception(ExceptionType::EXECUTION, 
                        "PRIMARY KEY violation: Duplicate value for " + col.GetName());
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
    }
    (void) count;
    // Logging removed to avoid interleaved output during concurrent operations
    is_finished_ = true;
    return false;
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

