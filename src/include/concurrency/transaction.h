#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include "common/rid.h"
#include "storage/table/tuple.h"

namespace francodb {

    /**
     * Transaction: Tracks all modifications for rollback support
     */
    class Transaction {
    public:
        Transaction(int txn_id) : txn_id_(txn_id), state_(TransactionState::RUNNING) {}
        
        int GetTransactionId() const { return txn_id_; }
        
        enum class TransactionState {
            RUNNING,
            COMMITTED,
            ABORTED
        };
        
        TransactionState GetState() const { return state_; }
        void SetState(TransactionState state) { state_ = state; }
        
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
        std::unordered_map<RID, TupleModification> modifications_; // Track all tuple changes
    };
}