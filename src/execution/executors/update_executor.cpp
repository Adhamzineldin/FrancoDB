#include "execution/executors/update_executor.h"
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
    std::cerr << "[DEBUG UPDATE] Collected " << updates_to_apply.size() << " updates to apply" << std::endl;
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
                std::cerr << "[DEBUG UPDATE] Checking PK uniqueness: column=" << col.GetName() 
                          << ", new_value=" << new_pk_value.GetAsInteger() 
                          << ", old_rid=" << update.old_rid.GetPageId() << ":" << update.old_rid.GetSlotId() << std::endl;
                bool found_duplicate = false;
                
                // Try to use index first (faster)
                auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(plan_->table_name_);
                std::cerr << "[DEBUG UPDATE] Found " << indexes.size() << " indexes on table" << std::endl;
                bool has_index = false;
                for (auto *index : indexes) {
                    std::cerr << "[DEBUG UPDATE] Checking index: " << index->name_ << " on column: " << index->col_name_ << std::endl;
                    if (index->col_name_ == col.GetName()) {
                        has_index = true;
                        std::cerr << "[DEBUG UPDATE] Using index for PK check" << std::endl;
                        GenericKey<8> key;
                        key.SetFromValue(new_pk_value);
                        
                        std::vector<RID> result_rids;
                        if (index->b_plus_tree_->GetValue(key, &result_rids, txn_)) {
                            std::cerr << "[DEBUG UPDATE] Index returned " << result_rids.size() << " RIDs" << std::endl;
                            // Check if any of the RIDs point to non-deleted tuples (excluding current)
                            for (const RID &rid : result_rids) {
                                std::cerr << "[DEBUG UPDATE] Checking RID " << rid.GetPageId() << ":" << rid.GetSlotId() << std::endl;
                                if (rid == update.old_rid) {
                                    std::cerr << "[DEBUG UPDATE] Skipping current tuple" << std::endl;
                                    continue; // Skip current tuple
                                }
                                Tuple existing_tuple;
                                if (table_info_->table_heap_->GetTuple(rid, &existing_tuple, txn_)) {
                                    std::cerr << "[DEBUG UPDATE] DUPLICATE FOUND!" << std::endl;
                                    found_duplicate = true;
                                    break;
                                } else {
                                    std::cerr << "[DEBUG UPDATE] RID points to deleted tuple" << std::endl;
                                }
                            }
                        } else {
                            std::cerr << "[DEBUG UPDATE] Index returned no matches" << std::endl;
                        }
                        break;
                    }
                }
                
                // Fallback: Sequential scan if no index exists
                if (!has_index) {
                    page_id_t scan_page_id = table_info_->first_page_id_;
                    while (scan_page_id != INVALID_PAGE_ID && !found_duplicate) {
                        Page *page = bpm->FetchPage(scan_page_id);
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

bool UpdateExecutor::EvaluatePredicate(const Tuple &tuple) {
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
            if (tuple_val.GetTypeId() == TypeId::INTEGER) match = (tuple_val.GetAsInteger() == cond.value.GetAsInteger());
            else if (tuple_val.GetTypeId() == TypeId::DECIMAL) match = (std::abs(tuple_val.GetAsDouble() - cond.value.GetAsDouble()) < 0.0001);
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

