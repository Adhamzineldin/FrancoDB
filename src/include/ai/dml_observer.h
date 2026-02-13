#pragma once

#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace chronosdb {
namespace ai {

enum class DMLOperation : uint8_t { INSERT, UPDATE, DELETE_OP, SELECT };

/**
 * Event data passed from DMLExecutor to AI observers.
 * Populated before/after each DML operation.
 */
struct DMLEvent {
    DMLOperation operation;
    std::string table_name;
    std::string db_name;
    std::string user;
    uint32_t session_id = 0;
    uint32_t rows_affected = 0;
    uint64_t start_time_us = 0;
    uint64_t duration_us = 0;

    // SELECT-specific fields
    bool used_index_scan = false;
    size_t where_clause_count = 0;
    bool has_order_by = false;
    bool has_limit = false;
    int32_t result_row_count = 0;
};

/**
 * IDMLObserver - Interface for AI systems that observe DML operations.
 *
 * Both ImmuneSystem and LearningEngine implement this.
 * Follows the Observer pattern to decouple AI from the execution engine.
 */
class IDMLObserver {
public:
    virtual ~IDMLObserver() = default;

    /**
     * Called BEFORE DML execution.
     * Return false to block the operation (used by Immune System).
     */
    virtual bool OnBeforeDML(const DMLEvent& event) { (void)event; return true; }

    /**
     * Called AFTER DML execution completes.
     * Used for recording metrics and feedback.
     */
    virtual void OnAfterDML(const DMLEvent& event) { (void)event; }
};

/**
 * DMLObserverRegistry - Central registry for DML observers.
 *
 * The DMLExecutor calls NotifyBefore() and NotifyAfter() on every operation.
 * AI subsystems register/unregister themselves as observers.
 */
class DMLObserverRegistry {
public:
    static DMLObserverRegistry& Instance();

    void Register(IDMLObserver* observer);
    void Unregister(IDMLObserver* observer);

    // Returns false if ANY observer blocks the operation
    bool NotifyBefore(const DMLEvent& event);

    void NotifyAfter(const DMLEvent& event);

    size_t GetObserverCount() const;

private:
    DMLObserverRegistry() = default;
    ~DMLObserverRegistry() = default;
    DMLObserverRegistry(const DMLObserverRegistry&) = delete;
    DMLObserverRegistry& operator=(const DMLObserverRegistry&) = delete;

    mutable std::shared_mutex mutex_;
    std::vector<IDMLObserver*> observers_; // Non-owning
};

} // namespace ai
} // namespace chronosdb
