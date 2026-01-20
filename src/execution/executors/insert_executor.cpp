#include "execution/executors/insert_executor.h"
#include "common/exception.h"
#include "catalog/index_info.h"
#include "storage/index/index_key.h"
#include "storage/table/table_page.h"
#include "buffer/buffer_pool_manager.h"
#include "storage/page/page.h"
#include <cmath>

namespace francodb {

void InsertExecutor::Init() {
    // 1. Look up the table in the Catalog
    table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
    if (table_info_ == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
    }
    
    // 2. SCHEMA VALIDATION: Check column count
    if (plan_->values_.size() != table_info_->schema_.GetColumnCount()) {
        throw Exception(ExceptionType::EXECUTION, 
            "Column count mismatch: expected " + 
            std::to_string(table_info_->schema_.GetColumnCount()) + 
            " but got " + std::to_string(plan_->values_.size()));
    }
    
    // 3. SCHEMA VALIDATION: Check types, NULL values, and CHECK constraints
    for (uint32_t i = 0; i < plan_->values_.size(); i++) {
        const Column &col = table_info_->schema_.GetColumn(i);
        const Value &val = plan_->values_[i];
        
        // Check if NULL value (represented by empty string or special marker)
        if (val.GetTypeId() == TypeId::VARCHAR && val.GetAsString().empty()) {
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
            // Simple CHECK validation: parse "column_name > value" or "column_name >= value"
            std::string check = col.GetCheckConstraint();
            std::string col_name = col.GetName();
            
            // Find operator
            size_t op_pos = std::string::npos;
            std::string op;
            if (check.find(">=") != std::string::npos) {
                op_pos = check.find(">=");
                op = ">=";
            } else if (check.find("<=") != std::string::npos) {
                op_pos = check.find("<=");
                op = "<=";
            } else if (check.find(">") != std::string::npos) {
                op_pos = check.find(">");
                op = ">";
            } else if (check.find("<") != std::string::npos) {
                op_pos = check.find("<");
                op = "<";
            } else if (check.find("!=") != std::string::npos) {
                op_pos = check.find("!=");
                op = "!=";
            } else if (check.find("=") != std::string::npos) {
                op_pos = check.find("=");
                op = "=";
            }
            
            if (op_pos != std::string::npos) {
                std::string right_side = check.substr(op_pos + op.length());
                // Trim whitespace
                right_side.erase(0, right_side.find_first_not_of(" \t"));
                right_side.erase(right_side.find_last_not_of(" \t") + 1);
                
                try {
                    // Compare values based on type
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
                    throw; // Re-throw our own exceptions
                } catch (...) {
                    // Ignore parse errors in check constraint
                }
            }
        }
    }
}

bool InsertExecutor::Next(Tuple *tuple) {
    (void)tuple;
    if (is_finished_) return false;

    Tuple to_insert(plan_->values_, table_info_->schema_);
    
    // --- CHECK PRIMARY KEY UNIQUENESS ---
    for (uint32_t i = 0; i < table_info_->schema_.GetColumnCount(); i++) {
        const Column &col = table_info_->schema_.GetColumn(i);
        if (col.IsPrimaryKey()) {
            Value pk_value = to_insert.GetValue(table_info_->schema_, i);
            bool found_duplicate = false;
            
            // Try to use index first (faster)
            auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(plan_->table_name_);
            bool has_index = false;
            for (auto *index : indexes) {
                if (index->col_name_ == col.GetName()) {
                    has_index = true;
                    GenericKey<8> key;
                    key.SetFromValue(pk_value);
                    
                    std::vector<RID> result_rids;
                    if (index->b_plus_tree_->GetValue(key, &result_rids, txn_)) {
                        // Check if any of the RIDs point to non-deleted tuples
                        for (const RID &rid : result_rids) {
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
                page_id_t curr_page_id = table_info_->first_page_id_;
                auto *bpm = exec_ctx_->GetBufferPoolManager();
                
                while (curr_page_id != INVALID_PAGE_ID) {
                    Page *page = bpm->FetchPage(curr_page_id);
                    if (page == nullptr) {
                        break; // Skip if page fetch fails
                    }
                    auto *table_page = reinterpret_cast<TablePage *>(page->GetData());
                    
                    for (uint32_t slot = 0; slot < table_page->GetTupleCount(); slot++) {
                        RID rid(curr_page_id, slot);
                        Tuple existing_tuple;
                        if (table_page->GetTuple(rid, &existing_tuple, txn_)) {
                            Value existing_pk = existing_tuple.GetValue(table_info_->schema_, i);
                            // Compare values based on type
                            bool matches = false;
                            if (existing_pk.GetTypeId() == pk_value.GetTypeId()) {
                                if (existing_pk.GetTypeId() == TypeId::INTEGER) {
                                    matches = (existing_pk.GetAsInteger() == pk_value.GetAsInteger());
                                } else if (existing_pk.GetTypeId() == TypeId::DECIMAL) {
                                    matches = (std::abs(existing_pk.GetAsDouble() - pk_value.GetAsDouble()) < 0.0001);
                                } else if (existing_pk.GetTypeId() == TypeId::VARCHAR) {
                                    matches = (existing_pk.GetAsString() == pk_value.GetAsString());
                                } else {
                                    matches = (existing_pk.GetAsInteger() == pk_value.GetAsInteger());
                                }
                            }
                            if (matches) {
                                found_duplicate = true;
                                break;
                            }
                        }
                    }
                    
                    page_id_t next = table_page->GetNextPageId();
                    bpm->UnpinPage(curr_page_id, false);
                    curr_page_id = next;
                    
                    if (found_duplicate) break;
                }
            }
            
            if (found_duplicate) {
                throw Exception(ExceptionType::EXECUTION, 
                    "PRIMARY KEY violation: Duplicate value for " + col.GetName());
            }
        }
    }
    // ------------------------------------
    
    RID rid;
    bool success = table_info_->table_heap_->InsertTuple(to_insert, &rid, txn_);
    if (!success) throw Exception(ExceptionType::EXECUTION, "Failed to insert tuple");
    
    // Track modification for rollback
    if (txn_) {
        Tuple empty_tuple; // Old tuple is empty for inserts
        txn_->AddModifiedTuple(rid, empty_tuple, false, plan_->table_name_);
    }

    // --- UPDATE INDEXES ---
    auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(plan_->table_name_);
    for (auto *index : indexes) {
        int col_idx = table_info_->schema_.GetColIdx(index->col_name_);
        Value key_val = to_insert.GetValue(table_info_->schema_, col_idx);
     
        GenericKey<8> key;
        key.SetFromValue(key_val); 
     
        index->b_plus_tree_->Insert(key, rid, txn_);
    }
    // ----------------------

    is_finished_ = true;
    return false;
}

const Schema *InsertExecutor::GetOutputSchema() {
    return &table_info_->schema_;
}

} // namespace francodb

