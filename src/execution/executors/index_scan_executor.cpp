#include "execution/executors/index_scan_executor.h"
#include "common/exception.h"
#include "catalog/index_info.h"
#include "storage/index/index_key.h"
#include <iostream>

namespace francodb {

void IndexScanExecutor::Init() {
    // 1. Get Table Metadata (so we can fetch the actual data later)
    table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
    if (table_info_ == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
    }
    
    // 2. Validate index info
    if (index_info_ == nullptr || index_info_->b_plus_tree_ == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "Invalid index info");
    }
    
    // 3. Convert the Lookup Value (from WHERE clause) to a GenericKey
    GenericKey<8> key; // Constructor initializes to zero
    key.SetFromValue(lookup_value_);

    // 4. Ask the B+Tree for the RIDs
    result_rids_.clear();
    try {
        index_info_->b_plus_tree_->GetValue(key, &result_rids_, txn_);
    } catch (const std::exception &e) {
        // If GetValue crashes, return empty result set
        result_rids_.clear();
    } catch (...) {
        // If GetValue crashes, return empty result set
        result_rids_.clear();
    }


    // 5. Validate all RIDs before storing them
    std::vector<RID> valid_rids;
    for (const auto &rid : result_rids_) {
        if (rid.GetPageId() != INVALID_PAGE_ID && rid.GetPageId() >= 0) {
            valid_rids.push_back(rid);
        }
        // Skip invalid RIDs silently
    }
    result_rids_ = std::move(valid_rids);

    // 6. Reset iterator
    cursor_ = 0;
}

bool IndexScanExecutor::Next(Tuple *tuple) {
    if (table_info_ == nullptr || table_info_->table_heap_ == nullptr) {
        return false;
    }
    
    // Loop through the results found by the B+Tree
    while (cursor_ < result_rids_.size()) {
        RID rid = result_rids_[cursor_];
        cursor_++;

        // Validate RID before using it
        if (rid.GetPageId() == INVALID_PAGE_ID) {
            continue; // Skip invalid RID
        }

        // FETCH THE ACTUAL TUPLE
        bool success = table_info_->table_heap_->GetTuple(rid, tuple, txn_);
        if (success) {
            // Found a valid (non-deleted) tuple
            return true;
        }
        // Tuple was deleted or invalid - skip to next RID
    }
    
    return false; // No more matches
}

const Schema *IndexScanExecutor::GetOutputSchema() {
    return &table_info_->schema_;
}

} // namespace francodb

