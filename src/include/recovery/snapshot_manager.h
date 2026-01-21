#pragma once

#include "recovery/recovery_manager.h"
#include "recovery/log_manager.h"
#include "recovery/checkpoint_manager.h"
#include "storage/table/table_heap.h"
#include "catalog/catalog.h"
#include "buffer/buffer_pool_manager.h"
#include <memory>
#include <chrono>

namespace francodb {

    /**
     * Snapshot Manager - "Git Checkout --Detached" for Data
     * 
     * This class provides a clean interface for Time Travel queries (SELECT AS OF).
     * It builds read-only "Shadow Heaps" that represent the state of a table
     * at a specific point in time WITHOUT modifying the live database.
     * 
     * Mental Model:
     * =============
     * - The live database is like "main" branch in Git
     * - A snapshot is like "git checkout <commit> --detached"
     * - The snapshot is a temporary read-only view
     * - The live database remains unchanged
     * 
     * Usage:
     * ======
     * auto snapshot = SnapshotManager::BuildSnapshot("users", target_time, bpm, log_manager, catalog);
     * // Query the snapshot...
     * // When done, the unique_ptr automatically cleans up
     */
    class SnapshotManager {
    public:
        /**
         * Build a snapshot of a table at a specific timestamp.
         * 
         * This creates a NEW TableHeap in memory and replays log records
         * up to the target_time into it. The live database is not modified.
         * 
         * @param table_name Name of the table to snapshot
         * @param target_time Target timestamp (microseconds since epoch)
         * @param bpm Buffer pool manager
         * @param log_manager Log manager for reading logs
         * @param catalog Catalog for table metadata
         * @param db_name Database name to read logs from (optional, uses current if empty)
         * @return unique_ptr to the snapshot TableHeap (caller owns)
         */
        static std::unique_ptr<TableHeap> BuildSnapshot(
            const std::string& table_name,
            uint64_t target_time, 
            BufferPoolManager* bpm, 
            LogManager* log_manager,
            Catalog* catalog,
            const std::string& db_name = "") 
        {
            // 1. Create a FRESH, EMPTY Shadow Table
            // (Transaction = nullptr means no logging for this temp table)
            auto shadow_heap = std::make_unique<TableHeap>(bpm, nullptr); 

            // 2. Determine which database to read logs from
            std::string target_db = db_name;
            if (target_db.empty() && log_manager) {
                target_db = log_manager->GetCurrentDatabase();
            }
            
            std::cout << "[SnapshotManager] Building snapshot for table '" << table_name 
                      << "' from database '" << target_db << "'" << std::endl;

            // 3. Use RecoveryManager to fill it
            // We pass the Shadow Heap explicitly so RecoveryManager writes to IT, not the Catalog.
            // Note: CheckpointManager is not needed for snapshot operations (pass nullptr)
            RecoveryManager recovery(log_manager, catalog, bpm, nullptr);
            
            // This replays log history into the shadow heap
            recovery.ReplayIntoHeap(shadow_heap.get(), table_name, target_time, target_db);
            
            return shadow_heap;
        }

        /**
         * Build a snapshot from a human-readable timestamp string.
         * 
         * Supports formats:
         * - "2025-01-22 10:30:00" (ISO datetime)
         * - "1 hour ago"
         * - "5 minutes ago"
         * 
         * @param table_name Name of the table
         * @param timestamp_str Human-readable timestamp
         * @param bpm Buffer pool manager
         * @param log_manager Log manager
         * @param catalog Catalog
         * @return unique_ptr to the snapshot TableHeap
         */
        static std::unique_ptr<TableHeap> BuildSnapshotFromString(
            const std::string& table_name,
            const std::string& timestamp_str,
            BufferPoolManager* bpm,
            LogManager* log_manager,
            Catalog* catalog)
        {
            uint64_t target_time = ParseTimestamp(timestamp_str);
            return BuildSnapshot(table_name, target_time, bpm, log_manager, catalog);
        }

        /**
         * Build a snapshot at a relative time offset from now.
         * 
         * @param table_name Name of the table
         * @param seconds_ago Number of seconds in the past
         * @param bpm Buffer pool manager
         * @param log_manager Log manager
         * @param catalog Catalog
         * @return unique_ptr to the snapshot TableHeap
         */
        static std::unique_ptr<TableHeap> BuildSnapshotSecondsAgo(
            const std::string& table_name,
            uint64_t seconds_ago,
            BufferPoolManager* bpm,
            LogManager* log_manager,
            Catalog* catalog)
        {
            uint64_t current = LogRecord::GetCurrentTimestamp();
            uint64_t target = current - (seconds_ago * 1000000ULL);  // Convert to microseconds
            return BuildSnapshot(table_name, target, bpm, log_manager, catalog);
        }

        /**
         * Get the current timestamp in microseconds since epoch.
         * Useful for debugging and testing.
         */
        static uint64_t GetCurrentTimestamp() {
            return LogRecord::GetCurrentTimestamp();
        }

        /**
         * Convert a timestamp to a human-readable string.
         */
        static std::string TimestampToString(uint64_t timestamp) {
            // Convert microseconds to seconds
            time_t seconds = static_cast<time_t>(timestamp / 1000000ULL);
            char buffer[64];
            struct tm* tm_info = localtime(&seconds);
            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
            return std::string(buffer);
        }

    private:
        /**
         * Parse a timestamp string into microseconds since epoch.
         * 
         * Supports:
         * - ISO datetime: "2025-01-22 10:30:00"
         * - Relative: "1 hour ago", "5 minutes ago", "30 seconds ago"
         * - Unix timestamp: "1737549000"
         */
        static uint64_t ParseTimestamp(const std::string& timestamp_str) {
            // Check for relative time patterns
            if (timestamp_str.find("ago") != std::string::npos) {
                return ParseRelativeTime(timestamp_str);
            }
            
            // Check for Unix timestamp (all digits)
            bool all_digits = true;
            for (char c : timestamp_str) {
                if (!isdigit(c)) {
                    all_digits = false;
                    break;
                }
            }
            if (all_digits && !timestamp_str.empty()) {
                return std::stoull(timestamp_str) * 1000000ULL;
            }
            
            // Try to parse ISO datetime
            return ParseISODateTime(timestamp_str);
        }

        /**
         * Parse relative time like "5 minutes ago"
         */
        static uint64_t ParseRelativeTime(const std::string& str) {
            uint64_t current = LogRecord::GetCurrentTimestamp();
            uint64_t offset = 0;
            
            // Extract the number
            size_t num_end = str.find_first_not_of("0123456789 ");
            if (num_end == std::string::npos) {
                return current;
            }
            
            std::string num_str;
            for (char c : str) {
                if (isdigit(c)) num_str += c;
            }
            if (num_str.empty()) {
                return current;
            }
            
            uint64_t amount = std::stoull(num_str);
            
            // Determine unit
            if (str.find("second") != std::string::npos) {
                offset = amount * 1000000ULL;
            } else if (str.find("minute") != std::string::npos) {
                offset = amount * 60 * 1000000ULL;
            } else if (str.find("hour") != std::string::npos) {
                offset = amount * 3600 * 1000000ULL;
            } else if (str.find("day") != std::string::npos) {
                offset = amount * 86400 * 1000000ULL;
            }
            
            return current - offset;
        }

        /**
         * Parse ISO datetime like "2025-01-22 10:30:00"
         */
        static uint64_t ParseISODateTime(const std::string& str) {
            struct tm tm_info = {};
            
            // Try parsing with strptime equivalent
            int year, month, day, hour = 0, minute = 0, second = 0;
            if (sscanf(str.c_str(), "%d-%d-%d %d:%d:%d", 
                       &year, &month, &day, &hour, &minute, &second) >= 3) {
                tm_info.tm_year = year - 1900;
                tm_info.tm_mon = month - 1;
                tm_info.tm_mday = day;
                tm_info.tm_hour = hour;
                tm_info.tm_min = minute;
                tm_info.tm_sec = second;
                
                time_t epoch = mktime(&tm_info);
                return static_cast<uint64_t>(epoch) * 1000000ULL;
            }
            
            // Fallback: return current time
            return LogRecord::GetCurrentTimestamp();
        }
    };

} // namespace francodb

