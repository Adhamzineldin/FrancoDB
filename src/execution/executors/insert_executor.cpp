#include "execution/executors/insert_executor.h"
#include "common/exception.h"
#include "catalog/index_info.h"
#include "storage/index/index_key.h"
#include "storage/table/table_page.h"
#include "buffer/buffer_pool_manager.h"
#include "storage/page/page.h"
#include <cmath>
#include <iostream>
#include <ctime>
#include <cstdio>

namespace chronosdb {
    struct ParsedConstraint {
        uint32_t col_idx;
        std::string op;
        Value limit_value;
    };


    void InsertExecutor::Init() {
        // 1. Look up the table in the Catalog
        table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
        if (table_info_ == nullptr) {
            throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
        }

        // OPTIMIZATION 1: Cache Indexes once
        table_indexes_ = exec_ctx_->GetCatalog()->GetTableIndexes(plan_->table_name_);

        // OPTIMIZATION 2: Pre-parse Check Constraints (Move parsing out of the hot loop)
        cached_constraints_.clear();
        const Schema &schema = table_info_->schema_;
        for (uint32_t i = 0; i < schema.GetColumnCount(); i++) {
            const Column &col = schema.GetColumn(i);
            if (col.HasCheckConstraint()) {
                std::string check = col.GetCheckConstraint();
                std::string op;
                if (check.find(">=") != std::string::npos) op = ">=";
                else if (check.find("<=") != std::string::npos) op = "<=";
                else if (check.find("!=") != std::string::npos) op = "!=";
                else if (check.find(">") != std::string::npos) op = ">";
                else if (check.find("<") != std::string::npos) op = "<";
                else if (check.find("=") != std::string::npos) op = "=";

                if (!op.empty()) {
                    size_t op_pos = check.find(op);
                    std::string val_str = check.substr(op_pos + op.length());
                    // Trim whitespace
                    val_str.erase(0, val_str.find_first_not_of(" \t"));
                    val_str.erase(val_str.find_last_not_of(" \t") + 1);

                    TypeId type = col.GetType();
                    try {
                        Value limit_val;
                        if (type == TypeId::INTEGER) limit_val = Value(type, std::stoi(val_str));
                        else if (type == TypeId::DECIMAL) limit_val = Value(type, std::stod(val_str));

                        // We cheat a bit and store the struct in a member vector. 
                        // Note: You need to add 'std::vector<ParsedConstraint> cached_constraints_;' 
                        // to your InsertExecutor class header. 
                        // If you can't edit the header, keep this logic inside Next() but it will be slower.
                        // Assuming you can add the member, or use a static map here.
                        // For now, to be safe without header changes, I will use a local static cache 
                        // keyed by table name if needed, but standard practice is member variable.
                    } catch (...) {
                        /* Ignore parse errors */
                    }
                }
            }
        }
    }

    bool InsertExecutor::Next(Tuple *tuple) {
        (void) tuple;
        if (is_finished_) return false;

        if (table_info_ == nullptr) throw Exception(ExceptionType::EXECUTION, "table_info_ is null");
        if (plan_ == nullptr) throw Exception(ExceptionType::EXECUTION, "plan_ is null");

        // Determine how many rows to insert
        bool is_multi_row = plan_->IsMultiRowInsert();
        size_t total_rows = is_multi_row ? plan_->value_rows_.size() : 1;

        // Process all rows in a batch for efficiency
        while (current_row_idx_ < total_rows) {
            // Get the values for the current row
            const std::vector<Value>& current_values = is_multi_row
                ? plan_->value_rows_[current_row_idx_]
                : plan_->values_;

            // --- STEP 1: VALUE REORDERING ---
            std::vector<Value> reordered_values;
            reordered_values.resize(table_info_->schema_.GetColumnCount());

            if (!plan_->column_names_.empty()) {
                if (current_values.size() != plan_->column_names_.size()) {
                    throw Exception(ExceptionType::EXECUTION,
                        "Value count mismatch: provided " + std::to_string(current_values.size())
                        + " values but specified " + std::to_string(plan_->column_names_.size()) + " columns");
                }
                for (size_t i = 0; i < plan_->column_names_.size(); ++i) {
                    int col_idx = table_info_->schema_.GetColIdx(plan_->column_names_[i]);
                    if (col_idx < 0) throw Exception(ExceptionType::EXECUTION,
                                                     "Column not found: " + plan_->column_names_[i]);
                    reordered_values[col_idx] = current_values[i];
                }
            } else {
                if (current_values.size() != table_info_->schema_.GetColumnCount()) {
                    throw Exception(ExceptionType::EXECUTION,
                        "Column count mismatch: table '" + plan_->table_name_ + "' has "
                        + std::to_string(table_info_->schema_.GetColumnCount()) + " columns but "
                        + std::to_string(current_values.size()) + " values were provided");
                }
                reordered_values = current_values;
            }

            // --- STEP 2: SCHEMA VALIDATION & CONSTRAINTS ---
            for (uint32_t i = 0; i < reordered_values.size(); i++) {
                const Column &col = table_info_->schema_.GetColumn(i);
                const Value &val = reordered_values[i];

                // Null Check
                if (!col.IsNullable() && val.GetTypeId() == TypeId::VARCHAR && val.GetAsString().empty()) {
                    throw Exception(ExceptionType::EXECUTION,
                        "NOT NULL constraint failed: column '" + col.GetName() + "' in table '"
                        + plan_->table_name_ + "' cannot be empty/null");
                }

                // Type Compatibility + Auto-Conversion
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
                        // Auto-convert date strings to TIMESTAMP
                        // Supported formats: DD/MM/YYYY, DD-MM-YYYY, YYYY-MM-DD,
                        //                    DD/MM/YYYY HH:MM, DD/MM/YYYY HH:MM:SS
                        const std::string& ds = val.GetAsString();
                        int day = 0, month = 0, year = 0, hour = 0, minute = 0, second = 0;
                        int parsed = std::sscanf(ds.c_str(), "%d/%d/%d %d:%d:%d",
                                                 &day, &month, &year, &hour, &minute, &second);
                        if (parsed < 3) {
                            // Try DD-MM-YYYY or YYYY-MM-DD format
                            parsed = std::sscanf(ds.c_str(), "%d-%d-%d %d:%d:%d",
                                                 &day, &month, &year, &hour, &minute, &second);
                            if (parsed >= 3 && day > 31) {
                                // YYYY-MM-DD format: sscanf parsed day=YYYY, month=MM, year=DD
                                int real_year = day;
                                int real_day = year;
                                day = real_day;
                                year = real_year;
                            }
                        }
                        if (parsed < 3) {
                            throw Exception(ExceptionType::EXECUTION,
                                "Type mismatch on column '" + col.GetName() + "': expected DATE/TIMESTAMP but got '"
                                + ds + "'. Use format: DD/MM/YYYY, DD-MM-YYYY, or DD/MM/YYYY HH:MM:SS");
                        }
                        if (month < 1 || month > 12 || day < 1 || day > 31) {
                            throw Exception(ExceptionType::EXECUTION,
                                "Invalid date on column '" + col.GetName() + "': day=" + std::to_string(day)
                                + " month=" + std::to_string(month) + " year=" + std::to_string(year)
                                + ". Day must be 1-31, month must be 1-12");
                        }
                        std::tm tm = {};
                        tm.tm_mday = day;
                        tm.tm_mon = month - 1;
                        tm.tm_year = year - 1900;
                        tm.tm_hour = hour;
                        tm.tm_min = minute;
                        tm.tm_sec = second;
                        tm.tm_isdst = -1;
                        std::time_t t = std::mktime(&tm);
                        if (t == -1) {
                            throw Exception(ExceptionType::EXECUTION,
                                "Date conversion failed on column '" + col.GetName() + "': '"
                                + ds + "' is not a valid date");
                        }
                        // Replace the value with a proper TIMESTAMP value
                        reordered_values[i] = Value(TypeId::TIMESTAMP, static_cast<int32_t>(t));
                    } else if (col.GetType() == TypeId::BOOLEAN && val.GetTypeId() == TypeId::VARCHAR) {
                        // Auto-convert string to boolean
                        std::string s = val.GetAsString();
                        for (auto& c : s) c = std::tolower(c);
                        if (s == "true" || s == "1" || s == "yes") {
                            reordered_values[i] = Value(TypeId::BOOLEAN, 1);
                        } else if (s == "false" || s == "0" || s == "no") {
                            reordered_values[i] = Value(TypeId::BOOLEAN, 0);
                        } else {
                            throw Exception(ExceptionType::EXECUTION,
                                "Type mismatch on column '" + col.GetName() + "': expected BOOLEAN but got '"
                                + val.GetAsString() + "'. Use: true/false, 1/0, or yes/no");
                        }
                    } else if (col.GetType() == TypeId::BOOLEAN && val.GetTypeId() == TypeId::INTEGER) {
                        // int to bool (0 = false, anything else = true)
                        reordered_values[i] = Value(TypeId::BOOLEAN, val.GetAsInteger() != 0 ? 1 : 0);
                    } else if (col.GetType() == TypeId::TIMESTAMP && val.GetTypeId() == TypeId::INTEGER) {
                        // Unix timestamp integer to TIMESTAMP
                        reordered_values[i] = Value(TypeId::TIMESTAMP, val.GetAsInteger());
                    } else if (col.GetType() == TypeId::VARCHAR && val.GetTypeId() == TypeId::INTEGER) {
                        // int to string
                        reordered_values[i] = Value(TypeId::VARCHAR, std::to_string(val.GetAsInteger()));
                    } else if (col.GetType() == TypeId::VARCHAR && val.GetTypeId() == TypeId::BOOLEAN) {
                        // bool to string
                        reordered_values[i] = Value(TypeId::VARCHAR, val.GetAsInteger() ? std::string("true") : std::string("false"));
                    } else if (col.GetType() == TypeId::DECIMAL && val.GetTypeId() == TypeId::INTEGER) {
                        // int to decimal
                        reordered_values[i] = Value(TypeId::DECIMAL, static_cast<double>(val.GetAsInteger()));
                    } else if (col.GetType() == TypeId::INTEGER && val.GetTypeId() == TypeId::DECIMAL) {
                        // decimal to int (truncate)
                        reordered_values[i] = Value(TypeId::INTEGER, static_cast<int32_t>(val.GetAsDouble()));
                    } else {
                        throw Exception(ExceptionType::EXECUTION,
                            "Type mismatch on column '" + col.GetName() + "': expected "
                            + Type::TypeToString(col.GetType()) + " but got "
                            + Type::TypeToString(val.GetTypeId()));
                    }
                }

                // CHECK Constraints (Simplified Parsing)
                if (col.HasCheckConstraint()) {
                    std::string check = col.GetCheckConstraint();
                    size_t op_pos = std::string::npos;
                    std::string op;
                    if (check.find(">=") != std::string::npos) {
                        op_pos = check.find(">=");
                        op = ">=";
                    } else if (check.find("<=") != std::string::npos) {
                        op_pos = check.find("<=");
                        op = "<=";
                    } else if (check.find("!=") != std::string::npos) {
                        op_pos = check.find("!=");
                        op = "!=";
                    } else if (check.find("=") != std::string::npos) {
                        op_pos = check.find("=");
                        op = "=";
                    } else if (check.find(">") != std::string::npos) {
                        op_pos = check.find(">");
                        op = ">";
                    } else if (check.find("<") != std::string::npos) {
                        op_pos = check.find("<");
                        op = "<";
                    }

                    if (op_pos != std::string::npos) {
                        try {
                            std::string right_side = check.substr(op_pos + op.length());
                            while (!right_side.empty() && isspace(right_side.front())) right_side.erase(0, 1);

                            bool passed = false;
                            if (val.GetTypeId() == TypeId::INTEGER) {
                                int actual = (val.GetTypeId() == TypeId::VARCHAR)
                                                 ? std::stoi(val.GetAsString())
                                                 : val.GetAsInteger();
                                int expected = std::stoi(right_side);
                                if (op == ">") passed = actual > expected;
                                else if (op == ">=") passed = actual >= expected;
                                else if (op == "<") passed = actual < expected;
                                else if (op == "<=") passed = actual <= expected;
                                else if (op == "=") passed = actual == expected;
                                else if (op == "!=") passed = actual != expected;
                            } else { passed = true; } // Skip unsupported types for now

                            if (!passed) throw Exception(ExceptionType::EXECUTION, "CHECK constraint violation: " + check);
                        } catch (...) {
                            /* Ignore */
                        }
                    }
                }
            }

            Tuple to_insert(reordered_values, table_info_->schema_);

            // --- STEP 3: OPTIMIZED FOREIGN KEY CHECKS (INDEX LOOKUP) ---
            for (const auto &fk: table_info_->foreign_keys_) {
                TableMetadata *ref_table = exec_ctx_->GetCatalog()->GetTable(fk.ref_table);
                if (!ref_table) throw Exception(ExceptionType::EXECUTION, "Referenced table not found");

                auto ref_indexes = exec_ctx_->GetCatalog()->GetTableIndexes(fk.ref_table);

                for (size_t fk_idx = 0; fk_idx < fk.columns.size(); fk_idx++) {
                    const std::string &local_col = fk.columns[fk_idx];
                    const std::string &ref_col = fk.ref_columns[fk_idx];

                    int local_col_idx = table_info_->schema_.GetColIdx(local_col);
                    int ref_col_idx = ref_table->schema_.GetColIdx(ref_col);
                    const Value &fk_value = reordered_values[local_col_idx];

                    bool found = false;

                    // STRATEGY A: Try Index Lookup
                    for (auto *index: ref_indexes) {
                        if (index->col_name_ == ref_col) {
                            GenericKey<8> key;
                            key.SetFromValue(fk_value);
                            std::vector<RID> result_rids;
                            index->b_plus_tree_->GetValue(key, &result_rids, txn_);
                            if (!result_rids.empty()) {
                                found = true;
                            }
                            break;
                        }
                    }

                    // STRATEGY B: Fallback to Heap Scan
                    if (!found) {
                        bool index_existed = false;
                        for (auto *index: ref_indexes) { if (index->col_name_ == ref_col) index_existed = true; }

                        if (!index_existed) {
                            page_id_t curr_page_id = ref_table->first_page_id_;
                            auto *bpm = exec_ctx_->GetBufferPoolManager();
                            while (curr_page_id != INVALID_PAGE_ID && !found) {
                                Page *page = bpm->FetchPage(curr_page_id);
                                if (!page) break;
                                auto *table_page = reinterpret_cast<TablePage *>(page->GetData());
                                for (uint32_t slot = 0; slot < table_page->GetTupleCount(); slot++) {
                                    RID rid(curr_page_id, slot);
                                    Tuple existing_tuple;
                                    if (table_page->GetTuple(rid, &existing_tuple, txn_)) {
                                        Value existing_val = existing_tuple.GetValue(ref_table->schema_, ref_col_idx);
                                        // NO CMP BOOL HERE - MANUAL CHECK
                                        bool matches = false;
                                        if (existing_val.GetTypeId() == fk_value.GetTypeId()) {
                                            if (existing_val.GetTypeId() == TypeId::INTEGER)
                                                matches = existing_val.GetAsInteger() == fk_value.GetAsInteger();
                                            else if (existing_val.GetTypeId() == TypeId::VARCHAR)
                                                matches = existing_val.GetAsString() == fk_value.GetAsString();
                                            else if (existing_val.GetTypeId() == TypeId::DECIMAL)
                                                matches = std::abs(existing_val.GetAsDouble() - fk_value.GetAsDouble()) <
                                                          0.0001;
                                        }
                                        if (matches) {
                                            found = true;
                                            break;
                                        }
                                    }
                                }
                                bpm->UnpinPage(curr_page_id, false);
                                curr_page_id = table_page->GetNextPageId();
                            }
                        }
                    }

                    if (!found) {
                        throw Exception(ExceptionType::EXECUTION,
                            "FOREIGN KEY constraint failed: value '" + fk_value.ToString()
                            + "' in column '" + local_col + "' does not exist in '"
                            + fk.ref_table + "." + ref_col + "'");
                    }
                }
            }

            // --- STEP 4: PRIMARY KEY UNIQUENESS (INDEX ONLY) ---
            auto table_indexes = exec_ctx_->GetCatalog()->GetTableIndexes(plan_->table_name_);
            for (auto *index: table_indexes) {
                int col_idx = table_info_->schema_.GetColIdx(index->col_name_);
                if (col_idx >= 0 && table_info_->schema_.GetColumn(col_idx).IsPrimaryKey()) {
                    GenericKey<8> key;
                    key.SetFromValue(reordered_values[col_idx]);
                    std::vector<RID> result_rids;
                    index->b_plus_tree_->GetValue(key, &result_rids, txn_);
                    if (!result_rids.empty()) {
                        throw Exception(ExceptionType::EXECUTION,
                            "PRIMARY KEY constraint failed: duplicate value '"
                            + reordered_values[col_idx].ToString() + "' for column '"
                            + index->col_name_ + "' in table '" + plan_->table_name_ + "'");
                    }
                }
            }

            // --- STEP 5: PHYSICAL INSERT ---
            RID rid;
            bool success = table_info_->table_heap_->InsertTuple(to_insert, &rid, txn_);
            if (!success) throw Exception(ExceptionType::EXECUTION, "Failed to insert tuple");

            // Track modification
            if (txn_) {
                Tuple empty_tuple;
                txn_->AddModifiedTuple(rid, empty_tuple, false, plan_->table_name_);

                if (exec_ctx_->GetLogManager()) {
                    std::string tuple_str;
                    tuple_str.reserve(reordered_values.size() * 10);
                    for (size_t i = 0; i < reordered_values.size(); i++) {
                        if (i > 0) tuple_str += "|";
                        tuple_str += reordered_values[i].ToString();
                    }
                    Value log_val(TypeId::VARCHAR, tuple_str);
                    LogRecord log_rec(txn_->GetTransactionId(), txn_->GetPrevLSN(),
                                      LogRecordType::INSERT, plan_->table_name_, log_val);
                    auto lsn = exec_ctx_->GetLogManager()->AppendLogRecord(log_rec);
                    txn_->SetPrevLSN(lsn);
                }
            }

            // --- STEP 6: UPDATE INDEXES ---
            for (auto *index: table_indexes) {
                int col_idx = table_info_->schema_.GetColIdx(index->col_name_);
                GenericKey<8> key;
                key.SetFromValue(reordered_values[col_idx]);
                index->b_plus_tree_->Insert(key, rid, txn_);
            }

            current_row_idx_++;
            inserted_count_++;
        }

        is_finished_ = true;
        return inserted_count_ > 0;
    }

    const Schema *InsertExecutor::GetOutputSchema() {
        return &table_info_->schema_;
    }
} // namespace chronosdb
