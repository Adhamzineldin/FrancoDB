#include "execution/executors/seq_scan_executor.h"
#include "execution/executor_context.h"
#include "common/type.h"

namespace francodb {

    void SeqScanExecutor::Init() {
        // 1. Determine which Table Heap to scan (Live vs Time Travel)
        if (table_heap_override_ != nullptr) {
            // TIME TRAVEL MODE
            active_heap_ = table_heap_override_;
            
            // Get schema from catalog, but data from snapshot
            table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
        } else {
            // LIVE MODE
            table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
            if (!table_info_) {
                throw Exception(ExceptionType::CATALOG, "Table not found: " + plan_->table_name_);
            }
            active_heap_ = table_info_->table_heap_.get();
        }

        // 2. Initialize the Iterator
        iter_ = active_heap_->Begin(txn_);
    }

    bool SeqScanExecutor::Next(Tuple *tuple) {
        // Use the Iterator to traverse the table
        // This abstracts away page fetching, pinning, and slot logic
        while (iter_ != active_heap_->End()) {
            
            // Get reference to cached tuple (avoids copy)
            const Tuple& candidate_tuple = iter_.GetCurrentTuple();
            
            // Apply WHERE clause
            if (EvaluatePredicate(candidate_tuple)) {
                // Use move semantics when extracting the tuple
                *tuple = iter_.ExtractTuple();
                ++iter_;
                return true;
            }
            
            // Move iterator forward (handles jumping across pages)
            ++iter_;
        }
        
        return false; // End of Scan
    }

    const Schema *SeqScanExecutor::GetOutputSchema() {
        if (!table_info_) {
             table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
        }
        return &table_info_->schema_;
    }

    bool SeqScanExecutor::EvaluatePredicate(const Tuple &tuple) {
        if (plan_->where_clause_.empty()) {
            return true;
        }

        bool result = true; 
        
        for (size_t i = 0; i < plan_->where_clause_.size(); ++i) {
            const auto &cond = plan_->where_clause_[i];
            
            Value tuple_val = tuple.GetValue(table_info_->schema_, table_info_->schema_.GetColIdx(cond.column));
            bool match = false;
            
            // --- IN Operator ---
            if (cond.op == "IN") {
                for (const auto& in_val : cond.in_values) {
                    if (tuple_val.GetTypeId() == TypeId::INTEGER) {
                        if (tuple_val.GetAsInteger() == in_val.GetAsInteger()) { match = true; break; }
                    } else if (tuple_val.GetTypeId() == TypeId::DECIMAL) {
                        if (std::abs(tuple_val.GetAsDouble() - in_val.GetAsDouble()) < 0.0001) { match = true; break; }
                    } else {
                        if (tuple_val.GetAsString() == in_val.GetAsString()) { match = true; break; }
                    }
                }
            } 
            // --- Standard Operators (=, >, <, etc) ---
            else {
                // Ensure value types match for comparison
                // (Note: In a full DB, you'd handle casting here)
                
                if (cond.op == "=") {
                    if (tuple_val.GetTypeId() == TypeId::INTEGER) 
                        match = (tuple_val.GetAsInteger() == cond.value.GetAsInteger());
                    else if (tuple_val.GetTypeId() == TypeId::DECIMAL) 
                        match = (std::abs(tuple_val.GetAsDouble() - cond.value.GetAsDouble()) < 0.0001);
                    else 
                        match = (tuple_val.GetAsString() == cond.value.GetAsString());
                } 
                else if (cond.op == ">") {
                    if (tuple_val.GetTypeId() == TypeId::INTEGER) match = (tuple_val.GetAsInteger() > cond.value.GetAsInteger());
                    else if (tuple_val.GetTypeId() == TypeId::DECIMAL) match = (tuple_val.GetAsDouble() > cond.value.GetAsDouble());
                    else match = (tuple_val.GetAsString() > cond.value.GetAsString());
                } 
                else if (cond.op == "<") {
                    if (tuple_val.GetTypeId() == TypeId::INTEGER) match = (tuple_val.GetAsInteger() < cond.value.GetAsInteger());
                    else if (tuple_val.GetTypeId() == TypeId::DECIMAL) match = (tuple_val.GetAsDouble() < cond.value.GetAsDouble());
                    else match = (tuple_val.GetAsString() < cond.value.GetAsString());
                }
                else if (cond.op == ">=") {
                    if (tuple_val.GetTypeId() == TypeId::INTEGER) match = (tuple_val.GetAsInteger() >= cond.value.GetAsInteger());
                    else if (tuple_val.GetTypeId() == TypeId::DECIMAL) match = (tuple_val.GetAsDouble() >= cond.value.GetAsDouble());
                    else match = (tuple_val.GetAsString() >= cond.value.GetAsString());
                }
                else if (cond.op == "<=") {
                    if (tuple_val.GetTypeId() == TypeId::INTEGER) match = (tuple_val.GetAsInteger() <= cond.value.GetAsInteger());
                    else if (tuple_val.GetTypeId() == TypeId::DECIMAL) match = (tuple_val.GetAsDouble() <= cond.value.GetAsDouble());
                    else match = (tuple_val.GetAsString() <= cond.value.GetAsString());
                }
            }
            
            // --- Logic Chaining (AND/OR) ---
            if (i == 0) {
                result = match;
            } else {
                LogicType prev_logic = plan_->where_clause_[i-1].next_logic;
                if (prev_logic == LogicType::AND) {
                    result = result && match;
                } else if (prev_logic == LogicType::OR) {
                    result = result || match;
                }
            }
        }
        return result;
    }

} // namespace francodb