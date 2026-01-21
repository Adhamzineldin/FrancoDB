#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include "common/rid.h"
#include "storage/table/tuple.h"
#include "recovery/log_record.h" // [ACID] Needed for LogRecord::lsn_t

namespace francodb {

    /**
     * Transaction: Tracks all modifications for rollback support
     */
    class Transaction {
    public:
        // [ACID] Update Constructor to initialize LSN
        Transaction(int txn_id) 
            : txn_id_(txn_id), state_(TransactionState::RUNNING), 
              prev_lsn_(LogRecord::INVALID_LSN) {} // Start with -1
        
        int GetTransactionId() const { return txn_id_; }
        
        enum class TransactionState {
            RUNNING,
            COMMITTED,
            ABORTED
        };
        
        TransactionState GetState() const { return state_; }
        void SetState(TransactionState state) { state_ = state; }

        // [ACID] Methods to track the Log Chain
        void SetPrevLSN(LogRecord::lsn_t lsn) { prev_lsn_ = lsn; }
        LogRecord::lsn_t GetPrevLSN() const { return prev_lsn_; }
        
        // Track modified tuples for rollback
        struct TupleModification {
            RID rid;
            Tuple old_tuple;  // For rollback (empty for INSERT, contains old data for UPDATE/DELETE)
            bool is_deleted;  // true if DELETE, false if INSERT/UPDATE
            std::string table_name;  // Table name for index updates
        };
        
        void AddModifiedTuple(const RID &rid, const Tuple &old_tuple, bool is_deleted, const std::string &table_name = "") {
            modifications_[rid] = {rid, old_tuple, is_deleted, table_name};
        }
        
        const std::unordered_map<RID, TupleModification> &GetModifications() const {
            return modifications_;
        }
        
        void Clear() {
            modifications_.clear();
        }

    private:
        int txn_id_;
        TransactionState state_;
        
        // [ACID] The LSN of the last log record written by this transaction
        // This links all logs for this specific transaction together.
        LogRecord::lsn_t prev_lsn_;

        std::unordered_map<RID, TupleModification> modifications_; // Track all tuple changes
    };
}