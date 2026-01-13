#pragma once

#include <vector>
#include <memory>
#include <iostream>
#include "execution/executor_context.h"
#include "parser/statement.h"
#include "execution/executors/insert_executor.h"
#include "execution/executors/seq_scan_executor.h"
#include "common/exception.h"
#include "executors/delete_executor.h"
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
                case StatementType::CREATE: {
                    // Cast the generic Statement to a specific CreateStatement
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
        // --- 1. CREATE HANDLER ---
        void ExecuteCreate(CreateStatement *stmt) {
            Schema schema(stmt->columns_);
            bool success = catalog_->CreateTable(stmt->table_name_, schema);
            if (!success) {
                throw Exception(ExceptionType::EXECUTION, "Table already exists: " + stmt->table_name_);
            }
            std::cout << "[EXEC] Created Table: " << stmt->table_name_ << std::endl;
        }

        // --- 2. INSERT HANDLER ---
        void ExecuteInsert(InsertStatement *stmt) {
            InsertExecutor executor(exec_ctx_, stmt);
            executor.Init();
            Tuple t;
            executor.Next(&t);
            std::cout << "[EXEC] Insert successful." << std::endl;
        }

        // --- 3. SELECT HANDLER ---
        void ExecuteSelect(SelectStatement *stmt) {
            SeqScanExecutor executor(exec_ctx_, stmt);
            executor.Init();

            Tuple t;
            int count = 0;
            const Schema *output_schema = executor.GetOutputSchema();

            std::cout << "\n=== QUERY RESULT ===" << std::endl;

            // Print Header
            for (const auto &col: output_schema->GetColumns()) {
                std::cout << col.GetName() << "\t| ";
            }
            std::cout << "\n--------------------" << std::endl;

            // Fetch Rows Loop
            while (executor.Next(&t)) {
                // Print Columns
                for (uint32_t i = 0; i < output_schema->GetColumnCount(); ++i) {
                    Value v = t.GetValue(*output_schema, i);
                    std::cout << v << "\t\t| ";
                }
                std::cout << std::endl;
                count++;
            }
            std::cout << "====================" << std::endl;
            std::cout << "Rows returned: " << count << "\n" << std::endl;
        }

        // --- 4. DROP HANDLER ---
        void ExecuteDrop(DropStatement *stmt) {
            bool success = catalog_->DropTable(stmt->table_name_);
            if (!success) {
                throw Exception(ExceptionType::EXECUTION, "Table not found: " + stmt->table_name_);
            }
            std::cout << "[EXEC] Dropped Table: " << stmt->table_name_ << std::endl;
        }

        // --- 5. DELETE HANDLER ---
        void ExecuteDelete(DeleteStatement *stmt) {
            DeleteExecutor executor(exec_ctx_, stmt);
            executor.Init();
            Tuple t;
            executor.Next(&t);
        }

        // --- 6. UPDATE HANDLER ---
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
