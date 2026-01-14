#pragma once

#include <memory>
#include <vector>
#include "execution/executors/abstract_executor.h"
#include "parser/statement.h"
#include "storage/table/tuple.h"
#include "common/exception.h"
#include "catalog/index_info.h"

namespace francodb {

    class InsertExecutor : public AbstractExecutor {
    public:
        InsertExecutor(ExecutorContext *exec_ctx, InsertStatement *plan, Transaction *txn = nullptr)
            : AbstractExecutor(exec_ctx), 
              plan_(plan), 
              table_info_(nullptr),
              txn_(txn) {} // <--- FIX 2: Initialize pointer

        void Init() override {
            // 1. Look up the table in the Catalog
            table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
            if (table_info_ == nullptr) {
                throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
            }
        }

        // Insert executes everything in one go, then returns false.
        bool Next(Tuple *tuple) override {
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

        const Schema *GetOutputSchema() override { return &table_info_->schema_; }

    private:
        InsertStatement *plan_;
        TableMetadata *table_info_;
        bool is_finished_ = false;
        Transaction *txn_;
    };

} // namespace francodb