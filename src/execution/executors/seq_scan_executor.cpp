#include "execution/executors/seq_scan_executor.h"
#include "common/exception.h"
#include "storage/table/table_page.h"
#include "buffer/buffer_pool_manager.h"
#include "storage/page/page.h"
#include <cmath>

namespace francodb {

void SeqScanExecutor::Init() {
    // 1. Get Table Info
    table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
    if (table_info_ == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
    }

    // 2. Start at the beginning of the table
    page_id_t first_page = table_info_->first_page_id_;
    current_rid_ = RID(first_page, 0); 
}

bool SeqScanExecutor::Next(Tuple *tuple) {
    while (true) {
        // 1. Check if we are done
        if (current_rid_.GetPageId() == INVALID_PAGE_ID) {
            return false;
        }

        // 2. Fetch Page
        auto bpm = exec_ctx_->GetBufferPoolManager();
        Page *page = bpm->FetchPage(current_rid_.GetPageId());
        if (page == nullptr) {
            return false; 
        }
        
        auto table_page = reinterpret_cast<TablePage *>(page->GetData());
        
        // 3. Try to read the tuple
        Tuple potential_tuple;
        
        bool exists = table_page->GetTuple(current_rid_, &potential_tuple, txn_);
        
        // 4. Advance Iterator
        AdvanceIterator(table_page);
        
        bpm->UnpinPage(page->GetPageId(), false); 

        if (!exists) {
            continue; // Slot was empty, try next
        }

        // 5. Evaluate Filter (WHERE)
        if (EvaluatePredicate(potential_tuple)) {
            *tuple = potential_tuple;
            return true; 
        }
    }
}

const Schema *SeqScanExecutor::GetOutputSchema() {
    return &table_info_->schema_;
}

void SeqScanExecutor::AdvanceIterator(TablePage *curr_page) {
    uint32_t next_slot = current_rid_.GetSlotId() + 1;
    
    if (next_slot < curr_page->GetTupleCount()) {
        current_rid_.Set(current_rid_.GetPageId(), next_slot);
    } else {
        page_id_t next_page_id = curr_page->GetNextPageId();
        current_rid_.Set(next_page_id, 0); 
    }
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
            // Handle = operator
            match = (tuple_val.GetAsString() == cond.value.GetAsString()); 
            
            if (tuple_val.GetTypeId() == TypeId::INTEGER) {
                match = (tuple_val.GetAsInteger() == cond.value.GetAsInteger());
            } else if (tuple_val.GetTypeId() == TypeId::DECIMAL) {
                match = (std::abs(tuple_val.GetAsDouble() - cond.value.GetAsDouble()) < 0.0001);
            }
        }
        
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

