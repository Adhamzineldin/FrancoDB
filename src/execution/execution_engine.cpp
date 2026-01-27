/**
 * execution_engine.cpp
 * 
 * Query Execution Coordinator - SOLID Compliant
 * 
 * This file has been completely refactored to follow SOLID principles:
 * - Single Responsibility: Each executor handles one category of operations
 * - Open/Closed: New statement types can be added via the dispatch map
 * - Dependency Inversion: Depends on executor abstractions
 * 
 * The ExecutionEngine now only handles:
 * 1. Concurrency gatekeeper (global lock management)
 * 2. Delegation via dispatch map (no switch/if-else chains)
 * 3. Recovery operations (CHECKPOINT, RECOVER)
 * 
 * @author ChronosDB Team
 */

#include "execution/execution_engine.h"

// Specialized Executors (SOLID - SRP)
#include "execution/ddl_executor.h"
#include "execution/dml_executor.h"
#include "execution/system_executor.h"
#include "execution/user_executor.h"
#include "execution/database_executor.h"
#include "execution/transaction_executor.h"

// Recovery
#include "recovery/checkpoint_manager.h"
#include "recovery/recovery_manager.h"
#include "recovery/time_travel_engine.h"

// Common
#include "common/exception.h"

#include <sstream>
#include <iostream>
#include <chrono>
#include <unordered_map>
#include <functional>

namespace chronosdb {
    std::shared_mutex ExecutionEngine::global_lock_;

    // ============================================================================
    // CONSTRUCTOR / DESTRUCTOR
    // ============================================================================

    ExecutionEngine::ExecutionEngine(IBufferManager *bpm, Catalog *catalog,
                                     AuthManager *auth_manager, DatabaseRegistry *db_registry,
                                     LogManager *log_manager)
        : bpm_(bpm),
          catalog_(catalog),
          auth_manager_(auth_manager),
          db_registry_(db_registry),
          log_manager_(log_manager),
          exec_ctx_(nullptr),
          next_txn_id_(1) {
        
        // Create LockManager for row-level locking (CONCURRENCY FIX)
        lock_manager_ = std::make_unique<LockManager>();
        
        // Create executor context with LockManager
        exec_ctx_ = new ExecutorContext(bpm_, catalog_, nullptr, log_manager_, lock_manager_.get());

        // Initialize all specialized executors (SOLID - SRP)
        ddl_executor_ = std::make_unique<DDLExecutor>(catalog_, log_manager_);
        dml_executor_ = std::make_unique<DMLExecutor>(bpm_, catalog_, log_manager_);
        system_executor_ = std::make_unique<SystemExecutor>(catalog_, auth_manager_, db_registry_);
        user_executor_ = std::make_unique<UserExecutor>(auth_manager_);
        database_executor_ = std::make_unique<DatabaseExecutor>(auth_manager_, db_registry_, log_manager_);
        transaction_executor_ = std::make_unique<TransactionExecutor>(log_manager_, catalog_);

        // Share the atomic counter with transaction executor
        transaction_executor_->SetNextTxnId(&next_txn_id_);

        // Initialize the dispatch map (OCP - Open/Closed Principle)
        InitializeDispatchMap();
    }

    ExecutionEngine::~ExecutionEngine() {
        delete exec_ctx_;
    }

    // ============================================================================
    // DISPATCH MAP INITIALIZATION (Open/Closed Principle)
    // ============================================================================
    // Adding a new statement type only requires adding one line to this map.
    // No modification to Execute() method needed!
    // ============================================================================

    void ExecutionEngine::InitializeDispatchMap() {
        // ----- DDL OPERATIONS -----
        dispatch_map_[StatementType::CREATE] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return ddl_executor_->CreateTable(dynamic_cast<CreateStatement *>(s));
        };
        dispatch_map_[StatementType::CREATE_TABLE] = dispatch_map_[StatementType::CREATE];

        dispatch_map_[StatementType::CREATE_INDEX] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return ddl_executor_->CreateIndex(dynamic_cast<CreateIndexStatement *>(s));
        };

        dispatch_map_[StatementType::DROP] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return ddl_executor_->DropTable(dynamic_cast<DropStatement *>(s));
        };

        dispatch_map_[StatementType::DESCRIBE_TABLE] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return ddl_executor_->DescribeTable(dynamic_cast<DescribeTableStatement *>(s));
        };

        dispatch_map_[StatementType::SHOW_CREATE_TABLE] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return ddl_executor_->ShowCreateTable(dynamic_cast<ShowCreateTableStatement *>(s));
        };

        dispatch_map_[StatementType::ALTER_TABLE] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return ddl_executor_->AlterTable(dynamic_cast<AlterTableStatement *>(s));
        };

        // ----- DML OPERATIONS -----
        dispatch_map_[StatementType::INSERT] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return dml_executor_->Insert(dynamic_cast<InsertStatement *>(s), t);
        };

        dispatch_map_[StatementType::SELECT] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return dml_executor_->Select(dynamic_cast<SelectStatement *>(s), ctx, t);
        };

        dispatch_map_[StatementType::UPDATE_CMD] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return dml_executor_->Update(dynamic_cast<UpdateStatement *>(s), t);
        };

        dispatch_map_[StatementType::DELETE_CMD] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return dml_executor_->Delete(dynamic_cast<DeleteStatement *>(s), t);
        };

        // ----- TRANSACTION OPERATIONS -----
        dispatch_map_[StatementType::BEGIN] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return transaction_executor_->Begin();
        };

        dispatch_map_[StatementType::COMMIT] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return transaction_executor_->Commit();
        };

        dispatch_map_[StatementType::ROLLBACK] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return transaction_executor_->Rollback();
        };

        // ----- DATABASE OPERATIONS -----
        dispatch_map_[StatementType::CREATE_DB] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return database_executor_->CreateDatabase(dynamic_cast<CreateDatabaseStatement *>(s), ctx);
        };

        dispatch_map_[StatementType::DROP_DB] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return database_executor_->DropDatabase(dynamic_cast<DropDatabaseStatement *>(s), ctx);
        };

        // ----- USER OPERATIONS -----
        dispatch_map_[StatementType::CREATE_USER] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return user_executor_->CreateUser(dynamic_cast<CreateUserStatement *>(s));
        };

        dispatch_map_[StatementType::ALTER_USER_ROLE] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return user_executor_->AlterUserRole(dynamic_cast<AlterUserRoleStatement *>(s));
        };

        dispatch_map_[StatementType::DELETE_USER] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return user_executor_->DeleteUser(dynamic_cast<DeleteUserStatement *>(s));
        };

        // ----- SYSTEM OPERATIONS -----
        dispatch_map_[StatementType::SHOW_DATABASES] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return system_executor_->ShowDatabases(dynamic_cast<ShowDatabasesStatement *>(s), ctx);
        };

        dispatch_map_[StatementType::SHOW_TABLES] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return system_executor_->ShowTables(dynamic_cast<ShowTablesStatement *>(s), ctx);
        };

        dispatch_map_[StatementType::SHOW_STATUS] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return system_executor_->ShowStatus(dynamic_cast<ShowStatusStatement *>(s), ctx);
        };

        dispatch_map_[StatementType::SHOW_USERS] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return system_executor_->ShowUsers(dynamic_cast<ShowUsersStatement *>(s));
        };

        dispatch_map_[StatementType::WHOAMI] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return system_executor_->WhoAmI(dynamic_cast<WhoAmIStatement *>(s), ctx);
        };

        // ----- RECOVERY OPERATIONS -----
        dispatch_map_[StatementType::CHECKPOINT] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return ExecuteCheckpoint();
        };

        dispatch_map_[StatementType::RECOVER] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return ExecuteRecover(dynamic_cast<RecoverStatement *>(s));
        };
        
        // ----- SERVER CONTROL -----
        dispatch_map_[StatementType::STOP_SERVER] = [this](Statement *s, SessionContext *ctx, Transaction *t) {
            return ExecuteStopServer(ctx);
        };
    }

    // ============================================================================
    // TRANSACTION ACCESS (Delegates to TransactionExecutor)
    // ============================================================================

    Transaction *ExecutionEngine::GetCurrentTransaction() {
        return transaction_executor_->GetCurrentTransaction();
    }

    Transaction *ExecutionEngine::GetCurrentTransactionForWrite() {
        return transaction_executor_->GetCurrentTransactionForWrite();
    }

    // ============================================================================
    // MAIN EXECUTE METHOD - Clean Dispatch (No Switch/If-Else!)
    // ============================================================================

    ExecutionResult ExecutionEngine::Execute(Statement *stmt, SessionContext *session) {
        if (stmt == nullptr) {
            return ExecutionResult::Error("Empty Statement");
        }

        // ==========================================================================
        // CONCURRENCY GATEKEEPER
        // ==========================================================================
        std::unique_lock<std::shared_mutex> exclusive_lock;
        std::shared_lock<std::shared_mutex> shared_lock;

        StatementType type = stmt->GetType();

        bool requires_exclusive = (type == StatementType::RECOVER || type == StatementType::CHECKPOINT);

        if (requires_exclusive) {
            exclusive_lock = std::unique_lock<std::shared_mutex>(global_lock_);
        } else {
            shared_lock = std::shared_lock<std::shared_mutex>(global_lock_);
        }

        // ==========================================================================
        // PERMISSION CHECKS (Before dispatch)
        // ==========================================================================
        if (auto error = CheckPermissions(type, session); !error.empty()) {
            return ExecutionResult::Error(error);
        }

        // ==========================================================================
        // DISPATCH TO HANDLER (No switch/if-else chain!)
        // ==========================================================================
        try {
            Transaction *txn = GetCurrentTransactionForWrite();

            // Special handling for USE DATABASE (needs to update engine state)
            if (type == StatementType::USE_DB) {
                return HandleUseDatabase(dynamic_cast<UseDatabaseStatement *>(stmt), session, txn);
            }

            // Look up handler in dispatch map
            auto it = dispatch_map_.find(type);
            if (it == dispatch_map_.end()) {
                return ExecutionResult::Error("Unknown Statement Type");
            }

            // Execute the handler
            ExecutionResult res = it->second(stmt, session, txn);

            // Auto-commit for single DML statements
            if (type == StatementType::INSERT ||
                type == StatementType::UPDATE_CMD ||
                type == StatementType::DELETE_CMD) {
                transaction_executor_->AutoCommitIfNeeded();
            }

            return res;
        } catch (const std::exception &e) {
            // Force rollback on error
            if (transaction_executor_->GetCurrentTransaction() &&
                transaction_executor_->GetCurrentTransaction()->GetState() == Transaction::TransactionState::RUNNING) {
                transaction_executor_->Rollback();
            }
            return ExecutionResult::Error(e.what());
        }
    }

    // ============================================================================
    // PERMISSION CHECKS (Extracted for cleanliness)
    // ============================================================================

    std::string ExecutionEngine::CheckPermissions(StatementType type, SessionContext *session) {
        if (!session) return "";

        // Reserved database protections
        bool is_reserved_db = (session->current_db == "chronosdb" || session->current_db == "system");
        bool is_superadmin = (session->role == UserRole::SUPERADMIN);

        if (is_reserved_db && !is_superadmin) {
            if (type == StatementType::CREATE || type == StatementType::CREATE_TABLE) {
                return "Cannot create tables in reserved database";
            }
            if (type == StatementType::DROP) {
                return "Cannot drop tables in system database";
            }
            if (type == StatementType::INSERT || type == StatementType::UPDATE_CMD || type ==
                StatementType::DELETE_CMD) {
                return "Cannot modify system database tables";
            }
        }

        return ""; // No error
    }

    // ============================================================================
    // SPECIAL HANDLERS (Complex operations that need extra logic)
    // ============================================================================

    ExecutionResult ExecutionEngine::HandleUseDatabase(UseDatabaseStatement *stmt,
                                                       SessionContext *session,
                                                       Transaction *txn) {
        IBufferManager *new_bpm = nullptr;
        Catalog *new_catalog = nullptr;

        ExecutionResult res = database_executor_->UseDatabase(stmt, session, &new_bpm, &new_catalog);

        // Update engine state after USE DATABASE
        if (res.success && new_bpm && new_catalog) {
            bpm_ = new_bpm;
            catalog_ = new_catalog;

            // Update executor context with LockManager
            delete exec_ctx_;
            exec_ctx_ = new ExecutorContext(bpm_, catalog_, txn, log_manager_, lock_manager_.get());

            // Update executors with new catalog
            transaction_executor_->SetCatalog(catalog_);

            // Reinitialize DDL/DML executors with new catalog
            ddl_executor_ = std::make_unique<DDLExecutor>(catalog_, log_manager_);
            dml_executor_ = std::make_unique<DMLExecutor>(bpm_, catalog_, log_manager_);
        }

        return res;
    }

    // ============================================================================
    // HELPER METHODS
    // ============================================================================

    std::string ExecutionEngine::ValueToString(const Value &v) {
        std::ostringstream oss;
        oss << v;
        return oss.str();
    }

    // ============================================================================
    // RECOVERY OPERATIONS
    // ============================================================================

    ExecutionResult ExecutionEngine::ExecuteCheckpoint() {
        CheckpointManager cp_mgr(bpm_, log_manager_);
        cp_mgr.BeginCheckpoint();
        return ExecutionResult::Message("CHECKPOINT SUCCESS");
    }

    ExecutionResult ExecutionEngine::ExecuteRecover(RecoverStatement *stmt) {
        std::cout << "[SYSTEM] Preparing for Time Travel (Reverse Delta Strategy)..." << std::endl;

        uint64_t target_time = stmt->timestamp_;

        // Special case: UINT64_MAX means "recover to latest"
        bool recover_to_latest = (target_time == UINT64_MAX);

        if (recover_to_latest) {
            std::cout << "[SYSTEM] Recovering to LATEST state..." << std::endl;
        } else {
            uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            // Allow timestamps up to 1 minute in the future (treat as "now")
            uint64_t one_minute = 60ULL * 1000000ULL;  // microseconds
            if (target_time > now + one_minute) {
                return ExecutionResult::Error("Cannot travel to the future! Use 'RECOVER TO LATEST' for current state.");
            }

            if (target_time == 0) {
                return ExecutionResult::Error("Invalid timestamp (0). Use 'RECOVER TO LATEST' for current state.");
            }

            std::cout << "[SYSTEM] Initiating Time Travel to: " << target_time << std::endl;
        }

        // Force Buffer Pool Flush before recovery
        bpm_->FlushAllPages();
        log_manager_->Flush(true);

        try {
            // ================================================================
            // USE REVERSE DELTA TIME TRAVEL ENGINE
            //
            // The TimeTravelEngine provides:
            // - ATOMIC recovery (all-or-nothing)
            // - REVERSE DELTA strategy for recent queries (O(delta) not O(N))
            // - Automatic fallback to FORWARD REPLAY for distant past
            // ================================================================

            CheckpointManager cp_mgr(bpm_, log_manager_);
            cp_mgr.SetCatalog(catalog_);

            TimeTravelEngine time_travel(log_manager_, catalog_, bpm_, &cp_mgr);

            std::string db_name = log_manager_->GetCurrentDatabase();
            auto result = time_travel.RecoverTo(target_time, db_name);

            if (!result.success) {
                return ExecutionResult::Error(std::string("Recovery Failed: ") + result.error_message);
            }

            std::cout << "[SYSTEM] Strategy used: "
                      << (result.strategy_used == TimeTravelEngine::Strategy::REVERSE_DELTA
                          ? "REVERSE_DELTA" : "FORWARD_REPLAY") << std::endl;
            std::cout << "[SYSTEM] Records processed: " << result.records_processed << std::endl;
            std::cout << "[SYSTEM] Time elapsed: " << result.elapsed_ms << "ms" << std::endl;

            // Flush after recovery to persist changes
            bpm_->FlushAllPages();
            log_manager_->Flush(true);

            // Save catalog to persist table metadata changes
            if (catalog_) {
                catalog_->SaveCatalog();
            }
        } catch (const std::exception &e) {
            return ExecutionResult::Error(std::string("Recovery Failed: ") + e.what());
        }

        std::cout << "[SYSTEM] Time Travel Complete. Resuming normal operations." << std::endl;

        if (recover_to_latest) {
            return ExecutionResult::Message("RECOVERED TO LATEST. System state restored to most recent.");
        }
        return ExecutionResult::Message("TIME TRAVEL COMPLETE. System state reverted using Reverse Delta strategy.");
    }
    
    ExecutionResult ExecutionEngine::ExecuteStopServer(SessionContext* session) {
        // Only SUPERADMIN can stop the server
        if (session && session->role != UserRole::SUPERADMIN) {
            return ExecutionResult::Error("Permission denied. Only SUPERADMIN can stop the server.");
        }
        
        std::cout << "[STOP] Server shutdown requested by user: " 
                  << (session ? session->current_user : "unknown") << std::endl;
        
        // Set the shutdown flag - this will be checked by the server
        shutdown_requested_ = true;
        
        return ExecutionResult::Message("SHUTDOWN INITIATED. Server will stop after completing current operations.");
    }
} // namespace chronosdb
