#pragma once

#include <vector>
#include <memory>
#include <iostream>
#include <iomanip>
#include <sstream>
#include "execution/executor_context.h"
#include "parser/statement.h"
#include "execution/executors/insert_executor.h"
#include "execution/executors/seq_scan_executor.h"
#include "common/exception.h"
#include "executors/delete_executor.h"
#include "executors/index_scan_executor.h"
#include "executors/update_executor.h"

namespace francodb {
    class ExecutionEngine {
    public:
        ExecutionEngine(BufferPoolManager *bpm, Catalog *catalog)
            : catalog_(catalog), exec_ctx_(new ExecutorContext(catalog, bpm)) {
        }

        ~ExecutionEngine() { delete exec_ctx_; }

        void Execute(Statement *stmt) {
            if (stmt == nullptr) {
                return;
            }

            switch (stmt->GetType()) {
                case StatementType::CREATE_INDEX: {
                    auto *idx_stmt = dynamic_cast<CreateIndexStatement *>(stmt);
                    ExecuteCreateIndex(idx_stmt);
                    break;
                }

                case StatementType::CREATE: {
                    auto *create_stmt = dynamic_cast<CreateStatement *>(stmt);
                    ExecuteCreate(create_stmt);
                    break;
                }
                case StatementType::INSERT: {
                    auto *insert_stmt = dynamic_cast<InsertStatement *>(stmt);
                    ExecuteInsert(insert_stmt);
                    break;
                }
                case StatementType::SELECT: {
                    auto *select_stmt = dynamic_cast<SelectStatement *>(stmt);
                    ExecuteSelect(select_stmt);
                    break;
                }
                case StatementType::DROP: {
                    auto *drop_stmt = dynamic_cast<DropStatement *>(stmt);
                    ExecuteDrop(drop_stmt);
                    break;
                }
                case StatementType::DELETE_CMD: {
                    auto *del_stmt = dynamic_cast<DeleteStatement *>(stmt);
                    ExecuteDelete(del_stmt);
                    break;
                }
                case StatementType::UPDATE_CMD: {
                    auto *upd_stmt = dynamic_cast<UpdateStatement *>(stmt);
                    ExecuteUpdate(upd_stmt);
                    break;
                }
                default: {
                    throw Exception(ExceptionType::EXECUTION, "Unknown Statement Type.");
                }
            }
        }

    private:
        // Helper function to convert Value to string
        std::string ValueToString(const Value &v) {
            std::ostringstream oss;
            oss << v;
            return oss.str();
        }

        // Helper function to print PostgreSQL-style table
        void PrintPostgresTable(const Schema *schema, const std::vector<std::vector<std::string>> &rows) {
            if (schema->GetColumnCount() == 0) {
                return;
            }

            // Calculate column widths
            std::vector<size_t> col_widths;
            for (uint32_t i = 0; i < schema->GetColumnCount(); ++i) {
                size_t max_width = schema->GetColumns()[i].GetName().length();
                for (const auto &row : rows) {
                    max_width = std::max(max_width, row[i].length());
                }
                col_widths.push_back(max_width);
            }

            // Print column headers
            std::cout << " ";
            for (uint32_t i = 0; i < schema->GetColumnCount(); ++i) {
                std::cout << std::left << std::setw(col_widths[i]) << schema->GetColumns()[i].GetName();
                if (i < schema->GetColumnCount() - 1) {
                    std::cout << " | ";
                }
            }
            std::cout << std::endl;

            // Print separator line
            std::cout << "-";
            for (uint32_t i = 0; i < schema->GetColumnCount(); ++i) {
                std::cout << std::string(col_widths[i], '-');
                if (i < schema->GetColumnCount() - 1) {
                    std::cout << "-+-";
                }
            }
            std::cout << "-" << std::endl;

            // Print rows
            for (const auto &row : rows) {
                std::cout << " ";
                for (uint32_t i = 0; i < row.size(); ++i) {
                    std::cout << std::left << std::setw(col_widths[i]) << row[i];
                    if (i < row.size() - 1) {
                        std::cout << " | ";
                    }
                }
                std::cout << std::endl;
            }

            // Print footer
            std::cout << "(" << rows.size() << " row" << (rows.size() != 1 ? "s" : "") << ")" << std::endl;
        }

        void ExecuteCreate(CreateStatement *stmt) {
            Schema schema(stmt->columns_);
            bool success = catalog_->CreateTable(stmt->table_name_, schema);
            if (!success) {
                throw Exception(ExceptionType::EXECUTION, "Table already exists: " + stmt->table_name_);
            }
            std::cout << "[EXEC] Created Table: " << stmt->table_name_ << std::endl;
        }

        void ExecuteCreateIndex(CreateIndexStatement *stmt) {
            auto *index = catalog_->CreateIndex(stmt->index_name_, stmt->table_name_, stmt->column_name_);
            if (index == nullptr) {
                throw Exception(ExceptionType::EXECUTION, "Failed to create index (Table exists? Column exists?)");
            }
            std::cout << "[EXEC] Created Index: " << stmt->index_name_ << " on " << stmt->table_name_ << std::endl;
        }

        void ExecuteInsert(InsertStatement *stmt) {
            InsertExecutor executor(exec_ctx_, stmt);
            executor.Init();
            Tuple t;
            executor.Next(&t);
            std::cout << "[EXEC] Insert successful." << std::endl;
        }

        void ExecuteSelect(SelectStatement *stmt) {
            AbstractExecutor *executor = nullptr;

            // --- OPTIMIZER LOGIC START ---
            bool use_index = false;
            std::string index_col_name;
            Value index_search_value;

            if (!stmt->where_clause_.empty()) {
                auto &cond = stmt->where_clause_[0];
                if (cond.op == "=") {
                    auto indexes = catalog_->GetTableIndexes(stmt->table_name_);
                    for (auto *idx: indexes) {
                        if (idx->col_name_ == cond.column && idx->b_plus_tree_ != nullptr) {
                            // Try to use index, but fall back to seq scan if it fails
                            try {
                                use_index = true;
                                index_col_name = idx->name_;
                                index_search_value = cond.value;
                                executor = new IndexScanExecutor(exec_ctx_, stmt, idx, index_search_value);
                                std::cout << "[OPTIMIZER] Using Index: " << idx->name_ << std::endl;
                                break;
                            } catch (...) {
                                // If index scan creation fails, fall back to sequential scan
                                use_index = false;
                                executor = nullptr;
                                break;
                            }
                        }
                    }
                }
            }
            // --- OPTIMIZER LOGIC END ---

            if (!use_index) {
                executor = new SeqScanExecutor(exec_ctx_, stmt);
                std::cout << "[OPTIMIZER] Using Sequential Scan" << std::endl;
            }

            // --- EXECUTION WITH BEAUTIFUL FORMATTING ---
            try {
                executor->Init();
            } catch (...) {
                // If index scan Init() fails, fall back to sequential scan
                if (use_index) {
                    delete executor;
                    executor = new SeqScanExecutor(exec_ctx_, stmt);
                    std::cout << "[OPTIMIZER] Index scan failed, falling back to Sequential Scan" << std::endl;
                    executor->Init();
                } else {
                    throw; // Re-throw if it's not an index scan issue
                }
            }
            Tuple t;
            const Schema *output_schema = executor->GetOutputSchema();
            
            // Collect all rows first
            std::vector<std::vector<std::string>> rows;
            while (executor->Next(&t)) {
                std::vector<std::string> row;
                for (uint32_t i = 0; i < output_schema->GetColumnCount(); ++i) {
                    Value v = t.GetValue(*output_schema, i);
                    row.push_back(ValueToString(v));
                }
                rows.push_back(row);
            }

            // Print the beautiful table
            std::cout << std::endl;
            PrintPostgresTable(output_schema, rows);
            std::cout << std::endl;

            delete executor;
        }

        void ExecuteDrop(DropStatement *stmt) {
            bool success = catalog_->DropTable(stmt->table_name_);
            if (!success) {
                throw Exception(ExceptionType::EXECUTION, "Table not found: " + stmt->table_name_);
            }
            std::cout << "[EXEC] Dropped Table: " << stmt->table_name_ << std::endl;
        }

        void ExecuteDelete(DeleteStatement *stmt) {
            DeleteExecutor executor(exec_ctx_, stmt);
            executor.Init();
            Tuple t;
            executor.Next(&t);
        }

        void ExecuteUpdate(UpdateStatement *stmt) {
            UpdateExecutor executor(exec_ctx_, stmt);
            executor.Init();
            Tuple t;
            executor.Next(&t);
        }

        Catalog *catalog_;
        ExecutorContext *exec_ctx_;
    };
} // namespace francodb