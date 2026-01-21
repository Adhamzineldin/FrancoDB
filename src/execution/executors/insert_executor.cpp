#include "execution/executors/insert_executor.h"
#include "common/exception.h"
#include "catalog/index_info.h"
#include "storage/index/index_key.h"
#include "storage/table/table_page.h"
#include "buffer/buffer_pool_manager.h"
#include "storage/page/page.h"
#include <cmath>
#include <iostream>

namespace francodb {

void InsertExecutor::Init() {
    // 1. Look up the table in the Catalog
    table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
    if (table_info_ == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
    }
    
    // Note: Detailed validation (Types/Constraints) moved to Next() 
    // to handle column reordering correctly.
}

bool InsertExecutor::Next(Tuple *tuple) {
    (void)tuple;
    if (is_finished_) return false;

    // Defensive null checks
    if (table_info_ == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "table_info_ is null");
    }
    if (plan_ == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "plan_ is null");
    }

    // --- STEP 1: VALUE REORDERING ---
    std::vector<Value> reordered_values;
    reordered_values.resize(table_info_->schema_.GetColumnCount());

    if (!plan_->column_names_.empty()) {
        // Named columns - reorder to match schema
        // Ensure input value count matches provided column names
        if (plan_->values_.size() != plan_->column_names_.size()) {
             throw Exception(ExceptionType::EXECUTION, "Input value count does not match column name count");
        }

        for (size_t i = 0; i < plan_->column_names_.size(); ++i) {
            int col_idx = table_info_->schema_.GetColIdx(plan_->column_names_[i]);
            if (col_idx < 0) {
                throw Exception(ExceptionType::EXECUTION, "Column not found: " + plan_->column_names_[i]);
            }
            reordered_values[col_idx] = plan_->values_[i];
        }
    } else {
        // No column names - assume order matches schema
        if (plan_->values_.size() != table_info_->schema_.GetColumnCount()) {
            throw Exception(ExceptionType::EXECUTION, 
                "Column count mismatch: expected " + 
                std::to_string(table_info_->schema_.GetColumnCount()) + 
                " but got " + std::to_string(plan_->values_.size()));
        }
        reordered_values = plan_->values_;
    }

    // --- STEP 2: SCHEMA VALIDATION (Types & Constraints) ---
    // Now that values are aligned with schema indices, we can validate.
    for (uint32_t i = 0; i < reordered_values.size(); i++) {
        const Column &col = table_info_->schema_.GetColumn(i);
        const Value &val = reordered_values[i];
        
        // Check if NULL value (represented by empty string or special marker)
        if (val.GetTypeId() == TypeId::VARCHAR && val.GetAsString().empty()) {
            // Note: If you support Nullable columns, check col.IsNotNullable() here
            throw Exception(ExceptionType::EXECUTION, 
                "NULL values not allowed: column '" + col.GetName() + "'");
        }
        
        // Check type compatibility
        if (val.GetTypeId() != col.GetType()) {
            // Allow string to integer/decimal conversion attempts, but validate
            if (col.GetType() == TypeId::INTEGER && val.GetTypeId() == TypeId::VARCHAR) {
                try {
                    std::stoi(val.GetAsString());
                } catch (...) {
                    throw Exception(ExceptionType::EXECUTION, 
                        "Type mismatch for column '" + col.GetName() + 
                        "': expected INTEGER");
                }
            } else if (col.GetType() == TypeId::DECIMAL && val.GetTypeId() == TypeId::VARCHAR) {
                try {
                    std::stod(val.GetAsString());
                } catch (...) {
                    throw Exception(ExceptionType::EXECUTION, 
                        "Type mismatch for column '" + col.GetName() + 
                        "': expected DECIMAL");
                }
            } else {
                throw Exception(ExceptionType::EXECUTION, 
                    "Type mismatch for column '" + col.GetName() + "'");
            }
        }
        
        // Validate CHECK constraints
        if (col.HasCheckConstraint()) {
            std::string check = col.GetCheckConstraint();
            
            // Find operator
            size_t op_pos = std::string::npos;
            std::string op;
            if (check.find(">=") != std::string::npos) { op_pos = check.find(">="); op = ">="; }
            else if (check.find("<=") != std::string::npos) { op_pos = check.find("<="); op = "<="; }
            else if (check.find(">") != std::string::npos) { op_pos = check.find(">"); op = ">"; }
            else if (check.find("<") != std::string::npos) { op_pos = check.find("<"); op = "<"; }
            else if (check.find("!=") != std::string::npos) { op_pos = check.find("!="); op = "!="; }
            else if (check.find("=") != std::string::npos) { op_pos = check.find("="); op = "="; }
            
            if (op_pos != std::string::npos) {
                std::string right_side = check.substr(op_pos + op.length());
                // Trim whitespace
                right_side.erase(0, right_side.find_first_not_of(" \t"));
                right_side.erase(right_side.find_last_not_of(" \t") + 1);
                
                try {
                    bool check_passed = false;
                    if (col.GetType() == TypeId::INTEGER) {
                        int actual = val.GetAsInteger();
                        int expected = std::stoi(right_side);
                        if (op == ">") check_passed = (actual > expected);
                        else if (op == ">=") check_passed = (actual >= expected);
                        else if (op == "<") check_passed = (actual < expected);
                        else if (op == "<=") check_passed = (actual <= expected);
                        else if (op == "=") check_passed = (actual == expected);
                        else if (op == "!=") check_passed = (actual != expected);
                    } else if (col.GetType() == TypeId::DECIMAL) {
                        double actual = val.GetAsDouble();
                        double expected = std::stod(right_side);
                        if (op == ">") check_passed = (actual > expected);
                        else if (op == ">=") check_passed = (actual >= expected);
                        else if (op == "<") check_passed = (actual < expected);
                        else if (op == "<=") check_passed = (actual <= expected);
                        else if (op == "=") check_passed = (std::abs(actual - expected) < 0.0001);
                        else if (op == "!=") check_passed = (std::abs(actual - expected) >= 0.0001);
                    }
                    
                    if (!check_passed) {
                        throw Exception(ExceptionType::EXECUTION, 
                            "CHECK constraint violated for column '" + col.GetName() + 
                            "': " + check);
                    }
                } catch (const Exception &e) {
                    throw; 
                } catch (...) {
                    // Ignore parse errors in check constraint
                }
            }
        }
    }

    Tuple to_insert(reordered_values, table_info_->schema_);
    
    // --- STEP 3: CHECK PRIMARY KEY UNIQUENESS ---
    for (uint32_t i = 0; i < table_info_->schema_.GetColumnCount(); i++) {
        const Column &col = table_info_->schema_.GetColumn(i);
        if (col.IsPrimaryKey()) {
            
            // USE reordered_values[i], NOT plan_->values_[i]
            const Value &pk_value = reordered_values[i];
            bool found_duplicate = false;
            
            // std::cout << "[DEBUG][INSERT] PK check on column '" << col.GetName() << "' value=" << pk_value << std::endl;
            
            // Try to use index first (faster)
            auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(plan_->table_name_);
            bool has_index = false;
            for (auto *index : indexes) {
                if (index->col_name_ == col.GetName()) {
                    has_index = true;
                    GenericKey<8> key;
                    key.SetFromValue(pk_value);
                    
                    std::vector<RID> result_rids;
                    bool idx_found = index->b_plus_tree_->GetValue(key, &result_rids, txn_);
                    // std::cout << "[DEBUG][INSERT] Index scan on '" << index->name_ << "' found=" << (idx_found?"true":"false") << " rids=" << result_rids.size() << std::endl;
                    if (idx_found) {
                        for (const RID &rid : result_rids) {
                            Tuple existing_tuple;
                            if (table_info_->table_heap_->GetTuple(rid, &existing_tuple, txn_)) {
                                Value existing_pk = existing_tuple.GetValue(table_info_->schema_, i);
                                // std::cout << "[DEBUG][INSERT] Index RID(" << rid.GetPageId() << "," << rid.GetSlotId() << ") pk=" << existing_pk << std::endl;
                                
                                bool matches = false;
                                if (existing_pk.GetTypeId() == pk_value.GetTypeId()) {
                                    if (existing_pk.GetTypeId() == TypeId::INTEGER) {
                                        matches = (existing_pk.GetAsInteger() == pk_value.GetAsInteger());
                                    } else if (existing_pk.GetTypeId() == TypeId::DECIMAL) {
                                        matches = (std::abs(existing_pk.GetAsDouble() - pk_value.GetAsDouble()) < 0.0001);
                                    } else if (existing_pk.GetTypeId() == TypeId::VARCHAR) {
                                        matches = (existing_pk.GetAsString() == pk_value.GetAsString());
                                    }
                                }
                                if (matches) { found_duplicate = true; break; }
                            } else {
                                // std::cout << "[DEBUG][INSERT] Index RID(" << rid.GetPageId() << "," << rid.GetSlotId() << ") tuple missing" << std::endl;
                            }
                        }
                    }
                    break;
                }
            }
            
            // Fallback: Sequential scan if no index exists
            if (!has_index) {
                page_id_t curr_page_id = table_info_->first_page_id_;
                auto *bpm = exec_ctx_->GetBufferPoolManager();
                
                while (curr_page_id != INVALID_PAGE_ID) {
                    Page *page = bpm->FetchPage(curr_page_id);
                    if (page == nullptr) {
                        // std::cout << "[DEBUG][INSERT] FetchPage failed for " << curr_page_id << std::endl;
                        break; 
                    }
                    auto *table_page = reinterpret_cast<TablePage *>(page->GetData());
                    
                    uint32_t tuple_count = table_page->GetTupleCount();
                    // std::cout << "[DEBUG][INSERT] Scan Page=" << curr_page_id << " tuple_count=" << tuple_count << std::endl;
                    for (uint32_t slot = 0; slot < tuple_count; slot++) {
                        RID rid(curr_page_id, slot);
                        Tuple existing_tuple;
                        if (table_page->GetTuple(rid, &existing_tuple, txn_)) {
                            Value existing_pk = existing_tuple.GetValue(table_info_->schema_, i);
                            // std::cout << "[DEBUG][INSERT] Scan RID(" << rid.GetPageId() << "," << rid.GetSlotId() << ") pk=" << existing_pk << std::endl;
                            
                            bool matches = false;
                            if (existing_pk.GetTypeId() == pk_value.GetTypeId()) {
                                if (existing_pk.GetTypeId() == TypeId::INTEGER) {
                                    matches = (existing_pk.GetAsInteger() == pk_value.GetAsInteger());
                                } else if (existing_pk.GetTypeId() == TypeId::DECIMAL) {
                                    matches = (std::abs(existing_pk.GetAsDouble() - pk_value.GetAsDouble()) < 0.0001);
                                } else if (existing_pk.GetTypeId() == TypeId::VARCHAR) {
                                    matches = (existing_pk.GetAsString() == pk_value.GetAsString());
                                }
                            }
                            if (matches) { found_duplicate = true; break; }
                        }
                    }
                    
                    page_id_t next = table_page->GetNextPageId();
                    bpm->UnpinPage(curr_page_id, false);
                    curr_page_id = next;
                    
                    if (found_duplicate) break;
                }
            }
            
            if (found_duplicate) {
                // std::cout << "[DEBUG][INSERT] Duplicate detected for PK '" << col.GetName() << "' value=" << pk_value << std::endl;
                throw Exception(ExceptionType::EXECUTION, 
                    "PRIMARY KEY violation: Duplicate value for " + col.GetName());
            }
        }
    }
    
    // --- STEP 4: PHYSICAL INSERT ---
    RID rid;
    bool success = table_info_->table_heap_->InsertTuple(to_insert, &rid, txn_);
    if (!success) throw Exception(ExceptionType::EXECUTION, "Failed to insert tuple");
    // std::cout << "[DEBUG][INSERT] Inserted tuple RID(" << rid.GetPageId() << "," << rid.GetSlotId() << ")" << std::endl;
    
    // Track modification for rollback
    if (txn_) {
        Tuple empty_tuple; // Old tuple is empty for inserts
        txn_->AddModifiedTuple(rid, empty_tuple, false, plan_->table_name_);
    }

    // --- STEP 5: UPDATE INDEXES ---
    auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(plan_->table_name_);
    // std::cout << "[DEBUG][INSERT] Updating " << indexes.size() << " indexes for table '" << plan_->table_name_ << "'" << std::endl;
    if (!indexes.empty()) {
        for (auto *index : indexes) {
            if (index == nullptr) {
                // std::cout << "[DEBUG][INSERT] Skipping null index" << std::endl;
                continue; 
            }
            
            int col_idx = table_info_->schema_.GetColIdx(index->col_name_);
            if (col_idx < 0) {
                // std::cout << "[DEBUG][INSERT] Index '" << index->name_ << "' column '" << index->col_name_ << "' not found in schema" << std::endl;
                continue;
            }
            // USE reordered_values[col_idx], NOT plan_->values_[col_idx]
            const Value &key_val = reordered_values[col_idx];
            
            GenericKey<8> key;
            key.SetFromValue(key_val);
            
            if (index->b_plus_tree_ != nullptr) {
                bool insert_success = index->b_plus_tree_->Insert(key, rid, txn_);
                // std::cout << "[DEBUG][INSERT] Index '" << index->name_ << "' insert key=" << key_val << " success=" << (insert_success?"true":"false") << std::endl;
            } else {
                // std::cout << "[DEBUG][INSERT] Index '" << index->name_ << "' has null b_plus_tree_" << std::endl;
            }
        }
    }

    is_finished_ = true;
    return false;
}

const Schema *InsertExecutor::GetOutputSchema() {
    return &table_info_->schema_;
}

} // namespace francodb