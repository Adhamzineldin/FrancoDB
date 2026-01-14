#pragma once

#include <vector>
#include "execution/executors/abstract_executor.h"
#include "parser/statement.h"
#include "storage/table/tuple.h"
#include "catalog/index_info.h"
#include "storage/index/index_key.h"
#include "common/config.h"
#include "common/exception.h"

namespace francodb {

class IndexScanExecutor : public AbstractExecutor {
public:
    IndexScanExecutor(ExecutorContext *exec_ctx, SelectStatement *plan, IndexInfo *index_info, Value lookup_value, Transaction *txn = nullptr)
        : AbstractExecutor(exec_ctx), 
          plan_(plan), 
          index_info_(index_info), 
          lookup_value_(lookup_value),
          table_info_(nullptr),
          txn_(txn) {}

    void Init() override {
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
        GenericKey<8> key;
        key.SetFromValue(lookup_value_);

        // 4. Ask the B+Tree for the RIDs
        // The tree returns a list of RIDs matching this key.
        result_rids_.clear();
        try {
            index_info_->b_plus_tree_->GetValue(key, &result_rids_, txn_);
        } catch (...) {
            // If GetValue crashes, return empty result set
            result_rids_.clear();
        }

        // 5. Validate all RIDs before storing them
        // Remove any invalid RIDs that might cause crashes later
        std::vector<RID> valid_rids;
        for (const auto &rid : result_rids_) {
            if (rid.GetPageId() != INVALID_PAGE_ID && rid.GetPageId() >= 0) {
                valid_rids.push_back(rid);
            }
        }
        result_rids_ = std::move(valid_rids);

        // 6. Reset iterator
        cursor_ = 0;
    }

    bool Next(Tuple *tuple) override {
        if (table_info_ == nullptr || table_info_->table_heap_ == nullptr) {
            return false;
        }
        
        // Loop through the results found by the B+Tree
        // Skip deleted tuples and continue to next RID
        while (cursor_ < result_rids_.size()) {
            RID rid = result_rids_[cursor_];
            cursor_++;

            // Validate RID before using it
            if (rid.GetPageId() == INVALID_PAGE_ID) {
                continue; // Skip invalid RID
            }

            // FETCH THE ACTUAL TUPLE
            // We have the address (RID), now go get the data from the Heap.
            bool success = table_info_->table_heap_->GetTuple(rid, tuple, txn_);
            if (success) {
                // Found a valid (non-deleted) tuple
                return true;
            }
            // Tuple was deleted or invalid - skip to next RID
            // This can happen in concurrent scenarios where index hasn't been updated yet
        }
        
        return false; // No more matches
    }

    const Schema *GetOutputSchema() override { return &table_info_->schema_; }

private:
    SelectStatement *plan_;
    IndexInfo *index_info_;
    Value lookup_value_;     // The value we are searching for (e.g., 100)
    
    TableMetadata *table_info_;
    std::vector<RID> result_rids_; // The "Hit List" returned by the Index
    size_t cursor_ = 0;
    Transaction *txn_;
};

} // namespace francodb