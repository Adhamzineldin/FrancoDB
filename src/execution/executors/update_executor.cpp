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
#include <ctime>
#include <cstdio>
#include <unordered_set>

namespace chronosdb {
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

        // Check type compatibility (validation only - actual conversion in CreateUpdatedTuple)
        if (val.GetTypeId() != col.GetType()) {
            if (col.GetType() == TypeId::INTEGER && val.GetTypeId() == TypeId::VARCHAR) {
                try { std::stoi(val.GetAsString()); } catch (...) {
                    throw Exception(ExceptionType::EXECUTION,
                                    "Type mismatch on column '" + col.GetName() + "': expected INTEGER but got '"
                                    + val.GetAsString() + "' (cannot convert to integer)");
                }
            } else if (col.GetType() == TypeId::DECIMAL && val.GetTypeId() == TypeId::VARCHAR) {
                try { std::stod(val.GetAsString()); } catch (...) {
                    throw Exception(ExceptionType::EXECUTION,
                                    "Type mismatch on column '" + col.GetName() + "': expected DECIMAL but got '"
                                    + val.GetAsString() + "' (cannot convert to number)");
                }
            } else if (col.GetType() == TypeId::TIMESTAMP && val.GetTypeId() == TypeId::VARCHAR) {
                // Will be auto-converted in CreateUpdatedTuple
                const std::string& ds = val.GetAsString();
                int d = 0, m = 0, y = 0;
                if (std::sscanf(ds.c_str(), "%d/%d/%d", &d, &m, &y) < 3 &&
                    std::sscanf(ds.c_str(), "%d-%d-%d", &d, &m, &y) < 3) {
                    throw Exception(ExceptionType::EXECUTION,
                        "Type mismatch on column '" + col.GetName() + "': expected DATE/TIMESTAMP but got '"
                        + ds + "'. Use format: DD/MM/YYYY, DD-MM-YYYY, or YYYY-MM-DD");
                }
            } else if (col.GetType() == TypeId::BOOLEAN && val.GetTypeId() == TypeId::VARCHAR) {
                std::string s = val.GetAsString();
                for (auto& c : s) c = std::tolower(c);
                if (s != "true" && s != "false" && s != "1" && s != "0" && s != "yes" && s != "no") {
                    throw Exception(ExceptionType::EXECUTION,
                        "Type mismatch on column '" + col.GetName() + "': expected BOOLEAN but got '"
                        + val.GetAsString() + "'. Use: true/false, 1/0, or yes/no");
                }
            } else if (col.GetType() == TypeId::BOOLEAN && val.GetTypeId() == TypeId::INTEGER) {
                // int to bool is fine
            } else if (col.GetType() == TypeId::TIMESTAMP && val.GetTypeId() == TypeId::INTEGER) {
                // Unix timestamp is fine
            } else if (col.GetType() == TypeId::VARCHAR) {
                // Anything can become a string
            } else if (col.GetType() == TypeId::DECIMAL && val.GetTypeId() == TypeId::INTEGER) {
                // int to decimal is fine
            } else if (col.GetType() == TypeId::INTEGER && val.GetTypeId() == TypeId::DECIMAL) {
                // decimal to int (truncate) is fine
            } else {
                throw Exception(ExceptionType::EXECUTION,
                                "Type mismatch on column '" + col.GetName() + "': expected "
                                + Type::TypeToString(col.GetType()) + " but got "
                                + Type::TypeToString(val.GetTypeId()));
            }
        }
    }

    bool UpdateExecutor::Next(Tuple *tuple) {
        if (is_finished_) return false;
        is_finished_ = true;

        struct UpdateInfo {
            RID old_rid;
            Tuple old_tuple;
            Tuple new_tuple;
        };
        std::vector<UpdateInfo> updates_to_apply;
        LockManager *lock_mgr = exec_ctx_->GetLockManager();
        auto bpm = exec_ctx_->GetBufferPoolManager();

        // OPTIMIZATION: Try to use index scan if WHERE clause has equality on indexed column
        bool used_index = false;
        if (!plan_->where_clause_.empty() && plan_->where_clause_[0].op == "=") {
            const auto& cond = plan_->where_clause_[0];
            auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(plan_->table_name_);

            for (auto* index : indexes) {
                if (index->col_name_ == cond.column && index->b_plus_tree_) {
                    // Found matching index - use index scan instead of full table scan
                    try {
                        Value lookup_val(TypeId::INTEGER, std::stoi(cond.value.ToString()));
                        GenericKey<8> search_key;
                        search_key.SetFromValue(lookup_val);

                        std::vector<RID> result_rids;
                        index->b_plus_tree_->GetValue(search_key, &result_rids, txn_);

                        for (const RID& rid : result_rids) {
                            Tuple old_tuple;
                            if (table_info_->table_heap_->GetTuple(rid, &old_tuple, txn_)) {
                                if (EvaluatePredicate(old_tuple)) {
                                    if (lock_mgr && txn_) {
                                        if (!lock_mgr->LockRow(txn_->GetTransactionId(), rid, LockMode::EXCLUSIVE)) {
                                            throw Exception(ExceptionType::EXECUTION, "Lock failed");
                                        }
                                    }
                                    Tuple locked_tuple;
                                    if (!table_info_->table_heap_->GetTuple(rid, &locked_tuple, txn_)) continue;
                                    if (!EvaluatePredicate(locked_tuple)) continue;

                                    Tuple new_tuple = CreateUpdatedTuple(locked_tuple);
                                    updates_to_apply.push_back({rid, locked_tuple, new_tuple});
                                }
                            }
                        }
                        used_index = true;
                        break;
                    } catch (...) {
                        // Failed to use index, fall back to full scan
                    }
                }
            }
        }

        // 1. SCAN PHASE (only if index scan not used)
        if (!used_index) {
            page_id_t curr_page_id = table_info_->first_page_id_;
            while (curr_page_id != INVALID_PAGE_ID) {
                PageGuard guard(bpm, curr_page_id, false);
                if (!guard.IsValid()) break;
                auto *table_page = guard.As<TablePage>();

                for (uint32_t i = 0; i < table_page->GetTupleCount(); ++i) {
                    RID rid(curr_page_id, i);
                    Tuple old_tuple;
                    if (table_page->GetTuple(rid, &old_tuple, txn_)) {
                        if (EvaluatePredicate(old_tuple)) {
                            if (lock_mgr && txn_) {
                                if (!lock_mgr->LockRow(txn_->GetTransactionId(), rid, LockMode::EXCLUSIVE)) {
                                    throw Exception(ExceptionType::EXECUTION, "Lock failed");
                                }
                            }
                            Tuple locked_tuple;
                            if (!table_page->GetTuple(rid, &locked_tuple, txn_)) continue;
                            if (!EvaluatePredicate(locked_tuple)) continue;

                            Tuple new_tuple = CreateUpdatedTuple(locked_tuple);
                            updates_to_apply.push_back({rid, locked_tuple, new_tuple});
                        }
                    }
                }
                curr_page_id = table_page->GetNextPageId();
            }
        }

        // 2. APPLY PHASE
        auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(plan_->table_name_);

        for (const auto &update: updates_to_apply) {
            if (txn_) txn_->AddModifiedTuple(update.old_rid, update.old_tuple, false, plan_->table_name_);

            // A. Remove old tuple from indexes (OPTIMIZED)
            for (auto *index: indexes) {
                int col_idx = table_info_->schema_.GetColIdx(index->col_name_);
                Value old_val = update.old_tuple.GetValue(table_info_->schema_, col_idx);
                Value new_val = update.new_tuple.GetValue(table_info_->schema_, col_idx);

                // NO CMP BOOL - MANUAL EQUALITY CHECK
                bool matches = false;
                if (old_val.GetTypeId() == new_val.GetTypeId()) {
                    if (old_val.GetTypeId() == TypeId::INTEGER)
                        matches = old_val.GetAsInteger() == new_val.GetAsInteger();
                    else if (old_val.GetTypeId() == TypeId::VARCHAR)
                        matches = old_val.GetAsString() == new_val.GetAsString();
                    else if (old_val.GetTypeId() == TypeId::DECIMAL)
                        matches = std::abs(old_val.GetAsDouble() - new_val.GetAsDouble()) < 0.0001;
                }

                if (matches) {
                    continue; // Skip index update if value is same
                }

                GenericKey<8> old_key;
                old_key.SetFromValue(old_val);
                index->b_plus_tree_->Remove(old_key, txn_);
            }

            // B. Update Table
            bool deleted = table_info_->table_heap_->MarkDelete(update.old_rid, txn_);
            if (!deleted) continue;

            RID new_rid;
            bool inserted = table_info_->table_heap_->InsertTuple(update.new_tuple, &new_rid, txn_);
            if (!inserted) continue;

            // Logging
            if (txn_ && exec_ctx_->GetLogManager()) {
                std::string old_str, new_str;
                old_str.reserve(64);
                new_str.reserve(64);
                for (uint32_t i = 0; i < table_info_->schema_.GetColumnCount(); i++) {
                    if (i > 0) {
                        old_str += "|";
                        new_str += "|";
                    }
                    old_str += update.old_tuple.GetValue(table_info_->schema_, i).ToString();
                    new_str += update.new_tuple.GetValue(table_info_->schema_, i).ToString();
                }
                Value old_val(TypeId::VARCHAR, old_str);
                Value new_val(TypeId::VARCHAR, new_str);
                LogRecord log_rec(txn_->GetTransactionId(), txn_->GetPrevLSN(),
                                  LogRecordType::UPDATE, plan_->table_name_, old_val, new_val);
                auto lsn = exec_ctx_->GetLogManager()->AppendLogRecord(log_rec);
                txn_->SetPrevLSN(lsn);
            }

            // C. Add new tuple to indexes
            for (auto *index: indexes) {
                int col_idx = table_info_->schema_.GetColIdx(index->col_name_);
                Value old_val = update.old_tuple.GetValue(table_info_->schema_, col_idx);
                Value new_val = update.new_tuple.GetValue(table_info_->schema_, col_idx);

                // SAME MANUAL CHECK TO SKIP
                bool matches = false;
                if (old_val.GetTypeId() == new_val.GetTypeId()) {
                    if (old_val.GetTypeId() == TypeId::INTEGER)
                        matches = old_val.GetAsInteger() == new_val.GetAsInteger();
                    else if (old_val.GetTypeId() == TypeId::VARCHAR)
                        matches = old_val.GetAsString() == new_val.GetAsString();
                    else if (old_val.GetTypeId() == TypeId::DECIMAL)
                        matches = std::abs(old_val.GetAsDouble() - new_val.GetAsDouble()) < 0.0001;
                }
                if (matches) continue;

                GenericKey<8> new_key;
                new_key.SetFromValue(new_val);
                index->b_plus_tree_->Insert(new_key, new_rid, txn_);
            }
        }
        count_ = updates_to_apply.size();
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
                    } else {
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
} // namespace chronosdb
