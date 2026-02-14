/**
 * dml_executor.cpp
 * 
 * Production-Grade Implementation of Data Manipulation Language Operations
 * Extracted from ExecutionEngine to satisfy Single Responsibility Principle.
 * 
 * This class handles all data-modifying operations:
 * - INSERT with constraint validation (PK, FK, UNIQUE, NOT NULL)
 * - SELECT with optimizer integration (SeqScan, IndexScan, Join)
 * - UPDATE with index maintenance
 * - DELETE with referential integrity
 * 
 * Transaction Integration:
 * - All DML operations participate in transactions
 * - Supports both auto-commit and explicit transaction modes
 * - Proper logging for crash recovery
 * 
 * @author ChronosDB Team
 */

#include "execution/dml_executor.h"
#include "execution/executor_context.h"
#include "execution/executors/insert_executor.h"
#include "execution/executors/seq_scan_executor.h"
#include "execution/executors/delete_executor.h"
#include "execution/executors/update_executor.h"
#include "execution/executors/index_scan_executor.h"
#include "execution/executors/join_executor.h"
#include "recovery/snapshot_manager.h"
#include "storage/table/in_memory_table_heap.h"
#include "parser/statement.h"
#include "catalog/catalog.h"
#include "catalog/table_metadata.h"
#include "catalog/index_info.h"
#include "concurrency/transaction.h"
#include "network/session_context.h"
#include "common/exception.h"
#include "storage/table/schema.h"
#include "ai/dml_observer.h"
#include "ai/ai_manager.h"
#include "ai/learning/learning_engine.h"
#include "ai/learning/execution_plan.h"
#include "ai/learning/query_plan_optimizer.h"
#include "ai/temporal/temporal_index_manager.h"
#include <algorithm>
#include <chrono>
#include <sstream>
#include <set>

namespace chronosdb {

// ============================================================================
// INSERT - With Full Constraint Validation
// ============================================================================

ExecutionResult DMLExecutor::Insert(InsertStatement* stmt, Transaction* txn) {
    if (!stmt) {
        return ExecutionResult::Error("[DML] Invalid INSERT statement: null pointer");
    }
    
    if (!catalog_) {
        return ExecutionResult::Error("[DML] Internal error: Catalog not initialized");
    }
    
    // Validate table exists
    TableMetadata* table_info = catalog_->GetTable(stmt->table_name_);
    if (!table_info) {
        return ExecutionResult::Error("[DML] Table not found: " + stmt->table_name_);
    }
    
    // AI Observer: Notify before INSERT
    ai::DMLEvent ai_event;
    ai_event.operation = ai::DMLOperation::INSERT;
    ai_event.table_name = stmt->table_name_;
    ai_event.start_time_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    if (!ai::DMLObserverRegistry::Instance().NotifyBefore(ai_event)) {
        return ExecutionResult::Error("[IMMUNE] INSERT blocked on table '" + stmt->table_name_ + "'");
    }

    try {
        // Create executor context with all dependencies
        ExecutorContext ctx(bpm_, catalog_, txn, log_manager_);

        // Create and initialize the insert executor
        InsertExecutor executor(&ctx, stmt, txn);
        executor.Init();
        // Execute the insert - executor processes all rows in batches for efficiency
        Tuple result_tuple;
        while (executor.Next(&result_tuple)) {
            // executor.Next() handles batch insertion internally
        }

        // Get actual inserted count (supports multi-row insert)
        size_t insert_count = executor.GetInsertedCount();

        // AI Observer: Notify after INSERT
        auto end_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        ai_event.duration_us = end_us - ai_event.start_time_us;
        ai_event.rows_affected = static_cast<uint32_t>(insert_count);
        ai::DMLObserverRegistry::Instance().NotifyAfter(ai_event);

        return ExecutionResult::Message("INSERT " + std::to_string(insert_count));

    } catch (const Exception& e) {
        return ExecutionResult::Error("[DML] Insert failed: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return ExecutionResult::Error("[DML] Insert failed: " + std::string(e.what()));
    }
}

// ============================================================================
// SELECT - With Query Optimization and Time Travel
// ============================================================================

ExecutionResult DMLExecutor::Select(SelectStatement* stmt, SessionContext* session, Transaction* txn) {
    if (!stmt) {
        return ExecutionResult::Error("[DML] Invalid SELECT statement: null pointer");
    }

    if (!catalog_) {
        return ExecutionResult::Error("[DML] Internal error: Catalog not initialized");
    }

    // Validate table exists
    TableMetadata* table_info = catalog_->GetTable(stmt->table_name_);
    if (!table_info) {
        return ExecutionResult::Error("[DML] Table not found: " + stmt->table_name_);
    }

    // AI Observer: record SELECT start time
    ai::DMLEvent ai_event;
    ai_event.operation = ai::DMLOperation::SELECT;
    ai_event.table_name = stmt->table_name_;
    ai_event.where_clause_count = stmt->where_clause_.size();
    ai_event.has_order_by = !stmt->order_by_.empty();
    ai_event.has_limit = (stmt->limit_ > 0);
    ai_event.limit_value = stmt->limit_;
    if (session) {
        ai_event.db_name = session->current_db;
        ai_event.user = session->current_user;
        ai_event.session_id = session->session_id;
    }
    ai_event.start_time_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    // AI Temporal Index: notify on time-travel queries
    if (stmt->as_of_timestamp_ > 0) {
        auto& ai_mgr = ai::AIManager::Instance();
        if (ai_mgr.IsInitialized() && ai_mgr.GetTemporalIndexManager()) {
            ai_mgr.GetTemporalIndexManager()->OnTimeTravelQuery(
                stmt->table_name_, stmt->as_of_timestamp_, ai_event.db_name);
        }
    }

    try {
        ExecutorContext ctx(bpm_, catalog_, txn, log_manager_);
        AbstractExecutor* executor = nullptr;
        bool use_index = false;

        // Get the target table heap (will be overridden for time travel)
        TableHeap* target_heap = table_info->table_heap_.get();
        std::unique_ptr<TableHeap> snapshot_heap = nullptr;  // For legacy time travel
        std::unique_ptr<InMemoryTableHeap> in_memory_snapshot = nullptr;  // For fast time travel

        // =================================================================
        // TIME TRAVEL (AS OF) - Build In-Memory Snapshot (FAST)
        // =================================================================
        if (stmt->as_of_timestamp_ > 0) {
            // Determine the database to use for time travel
            // Priority: 1) session->current_db, 2) log_manager->GetCurrentDatabase(), 3) "system"
            std::string db_name;
            if (session && !session->current_db.empty()) {
                db_name = session->current_db;
            } else if (log_manager_) {
                db_name = log_manager_->GetCurrentDatabase();
            }
            if (db_name.empty()) {
                db_name = "system";
            }

            std::cout << "[TIME TRAVEL] Building in-memory snapshot as of " << stmt->as_of_timestamp_
                      << " for database '" << db_name << "'..." << std::endl;

            // **CRITICAL**: Flush the log to ensure all records are on disk
            if (log_manager_) {
                log_manager_->Flush(true);  // Force flush
            }

            // Use fast in-memory snapshot (bypasses buffer pool)
            in_memory_snapshot = SnapshotManager::BuildSnapshotInMemory(
                stmt->table_name_,
                stmt->as_of_timestamp_,
                log_manager_,
                catalog_,
                db_name
            );

            if (in_memory_snapshot) {
                std::cout << "[TIME TRAVEL] In-memory snapshot built successfully ("
                          << in_memory_snapshot->GetTupleCount() << " rows)" << std::endl;
            } else {
                // BUG FIX: Don't silently fall back to live table - this is a real error!
                std::cout << "[TIME TRAVEL] ERROR: Failed to build historical snapshot!" << std::endl;
                std::cout << "[TIME TRAVEL] Possible causes:" << std::endl;
                std::cout << "  1. WAL log file not found for database '" << db_name << "'" << std::endl;
                std::cout << "  2. Target timestamp is invalid or corrupted" << std::endl;
                std::cout << "  3. Database was created after the target timestamp" << std::endl;
                return ExecutionResult::Error("[TIME TRAVEL] Cannot build snapshot - WAL log required. Check server logs for details.");
            }
        }
        
        // =================================================================
        // RESULT SET BUILDING
        // =================================================================

        auto rs = std::make_shared<ResultSet>();
        const Schema& schema = table_info->schema_;
        std::vector<uint32_t> column_indices;
        std::vector<std::vector<std::string>> all_rows;

        // Determine which columns to output
        if (stmt->select_all_) {
            for (uint32_t i = 0; i < schema.GetColumnCount(); i++) {
                rs->column_names.push_back(schema.GetColumn(i).GetName());
                column_indices.push_back(i);
            }
        } else {
            for (const auto& col_name : stmt->columns_) {
                int col_idx = schema.GetColIdx(col_name);
                if (col_idx < 0) {
                    return ExecutionResult::Error("[DML] Column not found: " + col_name);
                }
                rs->column_names.push_back(col_name);
                column_indices.push_back(static_cast<uint32_t>(col_idx));
            }
        }

        // =================================================================
        // FAST PATH: In-Memory Snapshot (Time Travel) - Bypass Executor
        // =================================================================
        if (in_memory_snapshot) {
            // Direct iteration over in-memory snapshot (no buffer pool)
            auto iter = in_memory_snapshot->Begin();
            auto end = in_memory_snapshot->End();

            while (iter != end) {
                const Tuple& tuple = *iter;
                bool matches = true;

                // Apply WHERE clause filters
                for (const auto& cond : stmt->where_clause_) {
                    int col_idx = schema.GetColIdx(cond.column);
                    if (col_idx < 0) continue;

                    Value tuple_val = tuple.GetValue(schema, col_idx);
                    std::string tuple_str = tuple_val.ToString();
                    std::string cond_str = cond.value.ToString();

                    if (cond.op == "=") {
                        matches = (tuple_str == cond_str);
                    } else if (cond.op == "!=" || cond.op == "<>") {
                        matches = (tuple_str != cond_str);
                    } else if (cond.op == "<") {
                        try { matches = (std::stod(tuple_str) < std::stod(cond_str)); }
                        catch (...) { matches = (tuple_str < cond_str); }
                    } else if (cond.op == ">") {
                        try { matches = (std::stod(tuple_str) > std::stod(cond_str)); }
                        catch (...) { matches = (tuple_str > cond_str); }
                    } else if (cond.op == "<=") {
                        try { matches = (std::stod(tuple_str) <= std::stod(cond_str)); }
                        catch (...) { matches = (tuple_str <= cond_str); }
                    } else if (cond.op == ">=") {
                        try { matches = (std::stod(tuple_str) >= std::stod(cond_str)); }
                        catch (...) { matches = (tuple_str >= cond_str); }
                    }

                    if (!matches) break;
                }

                if (matches) {
                    std::vector<std::string> row_strings;
                    for (uint32_t col_idx : column_indices) {
                        row_strings.push_back(tuple.GetValue(schema, col_idx).ToString());
                    }
                    all_rows.push_back(row_strings);
                }

                ++iter;
            }
        }
        // =================================================================
        // NORMAL PATH: Use Executor (Live Table or Index Scan)
        // =================================================================
        else {
            // AI Learning Engine: get full execution plan
            ai::ExecutionPlan exec_plan;
            bool has_ai_plan = false;
            {
                auto& ai_mgr = ai::AIManager::Instance();
                if (ai_mgr.IsInitialized() && ai_mgr.GetLearningEngine()) {
                    exec_plan = ai_mgr.GetLearningEngine()->OptimizeQuery(
                        stmt, stmt->table_name_);
                    has_ai_plan = exec_plan.ai_generated;
                }
            }

            // ---- Scan Strategy Decision ----
            bool ai_recommended = false;

            // Check if AI recommends index scan
            if (has_ai_plan && exec_plan.scan_strategy == ai::ScanStrategy::INDEX_SCAN &&
                !stmt->where_clause_.empty() && stmt->where_clause_[0].op == "=") {
                const auto& cond = stmt->where_clause_[0];
                auto indexes = catalog_->GetTableIndexes(stmt->table_name_);
                for (auto* idx : indexes) {
                    if (idx->col_name_ == cond.column && idx->b_plus_tree_) {
                        try {
                            Value lookup_val(TypeId::INTEGER, std::stoi(cond.value.ToString()));
                            executor = new IndexScanExecutor(&ctx, stmt, idx, lookup_val, txn);
                            use_index = true;
                            ai_recommended = true;
                            break;
                        } catch (...) {}
                    }
                }
            } else if (has_ai_plan && exec_plan.scan_strategy == ai::ScanStrategy::SEQUENTIAL_SCAN) {
                ai_recommended = true; // AI says seq scan
            }

            // Fallback: original index scan check (if AI didn't decide)
            if (!ai_recommended && !stmt->where_clause_.empty() && stmt->where_clause_[0].op == "=") {
                const auto& cond = stmt->where_clause_[0];
                auto indexes = catalog_->GetTableIndexes(stmt->table_name_);
                for (auto* idx : indexes) {
                    if (idx->col_name_ == cond.column && idx->b_plus_tree_) {
                        try {
                            Value lookup_val(TypeId::INTEGER, std::stoi(cond.value.ToString()));
                            executor = new IndexScanExecutor(&ctx, stmt, idx, lookup_val, txn);
                            use_index = true;
                            break;
                        } catch (...) {}
                    }
                }
            }

            // Fall back to sequential scan if no index available
            if (!use_index) {
                executor = new SeqScanExecutor(&ctx, stmt, txn, target_heap);
            }

            // Initialize the executor
            try {
                executor->Init();
            } catch (...) {
                delete executor;
                return ExecutionResult::Error("[DML] Failed to initialize executor");
            }

            // ---- Fetch and Filter with AI-Optimized Plan ----
            Tuple tuple;
            const Schema* output_schema = executor->GetOutputSchema();
            uint32_t rows_scanned = 0;
            bool use_early_termination = has_ai_plan &&
                exec_plan.limit_strategy == ai::LimitStrategy::EARLY_TERMINATION &&
                stmt->limit_ > 0 && stmt->order_by_.empty();

            while (executor->Next(&tuple)) {
                rows_scanned++;
                std::vector<std::string> row_strings;
                for (uint32_t col_idx : column_indices) {
                    row_strings.push_back(tuple.GetValue(*output_schema, col_idx).ToString());
                }
                all_rows.push_back(row_strings);

                // Early termination: stop scanning if we have enough rows
                // Only when there's no ORDER BY (otherwise we need all rows to sort)
                if (use_early_termination &&
                    static_cast<int>(all_rows.size()) >= stmt->limit_) {
                    break;
                }
            }

            ai_event.total_rows_scanned = rows_scanned;

            // ---- Record Rich Feedback for Learning Engine ----
            if (has_ai_plan) {
                auto& ai_mgr = ai::AIManager::Instance();
                if (ai_mgr.IsInitialized() && ai_mgr.GetLearningEngine()) {
                    ai::ExecutionFeedback feedback;
                    feedback.table_name = stmt->table_name_;
                    feedback.duration_us = 0; // Will be filled after timing
                    feedback.total_rows_scanned = rows_scanned;
                    feedback.rows_after_filter = static_cast<uint32_t>(all_rows.size());
                    feedback.result_rows = static_cast<uint32_t>(all_rows.size());
                    feedback.used_index = use_index;
                    feedback.where_clause_count = stmt->where_clause_.size();
                    feedback.had_limit = (stmt->limit_ > 0);
                    feedback.limit_value = stmt->limit_;
                    feedback.had_order_by = !stmt->order_by_.empty();
                    feedback.plan_used = exec_plan;

                    // Timing will be computed at the end; record feedback then
                    auto end_feedback_us = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count());
                    feedback.duration_us = end_feedback_us - ai_event.start_time_us;

                    ai_mgr.GetLearningEngine()->RecordExecutionFeedback(feedback);
                }
            }

            delete executor;
        }
        
        // =================================================================
        // POST-PROCESSING (ORDER BY, LIMIT, DISTINCT)
        // =================================================================
        
        // ORDER BY
        if (!stmt->order_by_.empty()) {
            const auto& order = stmt->order_by_[0];
            
            // Find column index for sorting
            int sort_col_idx = -1;
            for (size_t i = 0; i < rs->column_names.size(); i++) {
                if (rs->column_names[i] == order.column) {
                    sort_col_idx = static_cast<int>(i);
                    break;
                }
            }
            
            if (sort_col_idx >= 0) {
                bool ascending = (order.direction != "DESC");
                
                std::sort(all_rows.begin(), all_rows.end(),
                    [sort_col_idx, ascending](const auto& a, const auto& b) {
                        // Try numeric comparison first
                        try {
                            double val_a = std::stod(a[sort_col_idx]);
                            double val_b = std::stod(b[sort_col_idx]);
                            return ascending ? (val_a < val_b) : (val_a > val_b);
                        } catch (...) {
                            // Fall back to string comparison
                            return ascending ? 
                                (a[sort_col_idx] < b[sort_col_idx]) : 
                                (a[sort_col_idx] > b[sort_col_idx]);
                        }
                    });
            }
        }
        
        // DISTINCT
        if (stmt->is_distinct_) {
            std::set<std::vector<std::string>> unique_rows(all_rows.begin(), all_rows.end());
            all_rows.assign(unique_rows.begin(), unique_rows.end());
        }
        
        // OFFSET
        if (stmt->offset_ > 0 && static_cast<size_t>(stmt->offset_) < all_rows.size()) {
            all_rows.erase(all_rows.begin(), all_rows.begin() + stmt->offset_);
        } else if (stmt->offset_ > 0) {
            all_rows.clear();
        }
        
        // LIMIT
        if (stmt->limit_ > 0 && static_cast<size_t>(stmt->limit_) < all_rows.size()) {
            all_rows.resize(stmt->limit_);
        }
        
        // Add rows to result set
        for (const auto& row : all_rows) {
            rs->AddRow(row);
        }

        // AI Observer: Notify after SELECT
        auto end_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        ai_event.duration_us = end_us - ai_event.start_time_us;
        ai_event.used_index_scan = use_index;
        ai_event.result_row_count = static_cast<int32_t>(all_rows.size());
        ai::DMLObserverRegistry::Instance().NotifyAfter(ai_event);

        return ExecutionResult::Data(rs);

    } catch (const Exception& e) {
        return ExecutionResult::Error("[DML] Select failed: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return ExecutionResult::Error("[DML] Select failed: " + std::string(e.what()));
    }
}

// ============================================================================
// UPDATE - With Index Maintenance
// ============================================================================

ExecutionResult DMLExecutor::Update(UpdateStatement* stmt, Transaction* txn) {
    if (!stmt) {
        return ExecutionResult::Error("[DML] Invalid UPDATE statement: null pointer");
    }

    if (!catalog_) {
        return ExecutionResult::Error("[DML] Internal error: Catalog not initialized");
    }

    // Validate table exists
    TableMetadata* table_info = catalog_->GetTable(stmt->table_name_);
    if (!table_info) {
        return ExecutionResult::Error("[DML] Table not found: " + stmt->table_name_);
    }

    // Validate target column exists
    int col_idx = table_info->schema_.GetColIdx(stmt->target_column_);
    if (col_idx < 0) {
        return ExecutionResult::Error("[DML] Column not found: " + stmt->target_column_);
    }

    // AI Observer: Notify before UPDATE
    ai::DMLEvent ai_event;
    ai_event.operation = ai::DMLOperation::UPDATE;
    ai_event.table_name = stmt->table_name_;
    ai_event.start_time_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    if (!ai::DMLObserverRegistry::Instance().NotifyBefore(ai_event)) {
        return ExecutionResult::Error("[IMMUNE] UPDATE blocked on table '" + stmt->table_name_ + "'");
    }

    try {
        ExecutorContext ctx(bpm_, catalog_, txn, log_manager_);
        UpdateExecutor executor(&ctx, stmt, txn);
        executor.Init();

        Tuple result_tuple;
        executor.Next(&result_tuple);

        int update_count = executor.GetUpdateCount();

        // AI Observer: Notify after UPDATE
        auto end_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        ai_event.duration_us = end_us - ai_event.start_time_us;
        ai_event.rows_affected = static_cast<uint32_t>(update_count);
        ai::DMLObserverRegistry::Instance().NotifyAfter(ai_event);

        return ExecutionResult::Message("UPDATE " + std::to_string(update_count));

    } catch (const Exception& e) {
        return ExecutionResult::Error("[DML] Update failed: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return ExecutionResult::Error("[DML] Update failed: " + std::string(e.what()));
    }
}

// ============================================================================
// DELETE - With Referential Integrity
// ============================================================================

ExecutionResult DMLExecutor::Delete(DeleteStatement* stmt, Transaction* txn) {
    if (!stmt) {
        return ExecutionResult::Error("[DML] Invalid DELETE statement: null pointer");
    }

    if (!catalog_) {
        return ExecutionResult::Error("[DML] Internal error: Catalog not initialized");
    }

    // Validate table exists
    TableMetadata* table_info = catalog_->GetTable(stmt->table_name_);
    if (!table_info) {
        return ExecutionResult::Error("[DML] Table not found: " + stmt->table_name_);
    }

    // AI Observer: Notify before DELETE
    ai::DMLEvent ai_event;
    ai_event.operation = ai::DMLOperation::DELETE_OP;
    ai_event.table_name = stmt->table_name_;
    ai_event.start_time_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    if (!ai::DMLObserverRegistry::Instance().NotifyBefore(ai_event)) {
        return ExecutionResult::Error("[IMMUNE] DELETE blocked on table '" + stmt->table_name_ + "'");
    }

    try {
        ExecutorContext ctx(bpm_, catalog_, txn, log_manager_);
        DeleteExecutor executor(&ctx, stmt, txn);
        executor.Init();

        Tuple result_tuple;
        executor.Next(&result_tuple);
        int delete_count = static_cast<int>(executor.GetDeletedCount());

        // AI Observer: Notify after DELETE
        auto end_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        ai_event.duration_us = end_us - ai_event.start_time_us;
        ai_event.rows_affected = static_cast<uint32_t>(delete_count);
        ai::DMLObserverRegistry::Instance().NotifyAfter(ai_event);

        return ExecutionResult::Message("DELETE " + std::to_string(delete_count));

    } catch (const Exception& e) {
        return ExecutionResult::Error("[DML] Delete failed: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return ExecutionResult::Error("[DML] Delete failed: " + std::string(e.what()));
    }
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

bool DMLExecutor::CanUseIndexScan(SelectStatement* stmt) const {
    if (!stmt || stmt->where_clause_.empty()) {
        return false;
    }
    
    // Check if first condition is equality on an indexed column
    const auto& first_cond = stmt->where_clause_[0];
    if (first_cond.op != "=") {
        return false;
    }
    
    // Check if there's an index on this column
    TableMetadata* table_info = catalog_->GetTable(stmt->table_name_);
    if (!table_info) {
        return false;
    }
    
    auto indexes = catalog_->GetTableIndexes(stmt->table_name_);
    for (auto* index : indexes) {
        if (index->col_name_ == first_cond.column && index->b_plus_tree_) {
            return true;
        }
    }
    
    return false;
}

ExecutionResult DMLExecutor::ExecuteSeqScan(SelectStatement* stmt, ExecutorContext* ctx, Transaction* txn) {
    // This method is now integrated into Select() above
    // Kept for potential future refactoring
    return ExecutionResult::Error("[DML] Internal error: ExecuteSeqScan called directly");
}

ExecutionResult DMLExecutor::ExecuteIndexScan(SelectStatement* stmt, ExecutorContext* ctx, Transaction* txn) {
    // This method is now integrated into Select() above
    return ExecutionResult::Error("[DML] Internal error: ExecuteIndexScan called directly");
}

ExecutionResult DMLExecutor::ExecuteJoin(SelectStatement* stmt, ExecutorContext* ctx, Transaction* txn) {
    // JOIN handling - would be called if stmt has joins
    // For now, joins are handled through the JoinExecutor directly
    return ExecutionResult::Error("[DML] JOIN queries should be processed through main Select path");
}

} // namespace chronosdb

