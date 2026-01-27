#include "recovery/checkpoint_manager.h"
#include "catalog/catalog.h"
#include <fstream>
#include <iostream>
#include <chrono>

namespace chronosdb {

    // ========================================================================
    // CONSTRUCTOR / DESTRUCTOR
    // ========================================================================

    CheckpointManager::CheckpointManager(IBufferManager* bpm, LogManager* log_manager,
                                         const std::string& master_record_path)
        : bpm_(bpm), 
          log_manager_(log_manager), 
          catalog_(nullptr),
          master_record_path_(master_record_path),
          checkpoint_offset_(0),
          last_checkpoint_timestamp_(0),
          checkpoint_count_(0),
          background_checkpointing_enabled_(false),
          stop_background_thread_(false),
          checkpoint_interval_seconds_(300) {  // Default: 5 minutes
        
        // Ensure master record directory exists
        std::filesystem::path path(master_record_path_);
        std::filesystem::create_directories(path.parent_path());
        
        std::cout << "[CheckpointManager] Initialized with master record: " 
                  << master_record_path_ << std::endl;
    }

    CheckpointManager::~CheckpointManager() {
        StopBackgroundCheckpointing();
    }

    // ========================================================================
    // CORE CHECKPOINTING
    // ========================================================================

    void CheckpointManager::BeginCheckpoint() {
        std::unique_lock<std::mutex> lock(checkpoint_mutex_);
        
        std::cout << "[CHECKPOINT] ========================================" << std::endl;
        std::cout << "[CHECKPOINT] Starting checkpoint #" << (checkpoint_count_.load() + 1) << std::endl;
        
        auto start_time = std::chrono::high_resolution_clock::now();

        // 1. Write CHECKPOINT_BEGIN record
        std::cout << "[CHECKPOINT] Phase 1: Writing CHECKPOINT_BEGIN..." << std::endl;
        LogRecord begin_record(0, LogRecord::INVALID_LSN, LogRecordType::CHECKPOINT_BEGIN);
        log_manager_->AppendLogRecord(begin_record);
        
        // 2. Capture Active Transaction Table (ATT)
        std::cout << "[CHECKPOINT] Phase 2: Capturing Active Transaction Table..." << std::endl;
        std::vector<ActiveTransactionEntry> active_txns = log_manager_->GetActiveTransactions();
        std::cout << "[CHECKPOINT]   - Found " << active_txns.size() << " active transactions" << std::endl;
        
        // 3. Capture Dirty Page Table (DPT)
        std::cout << "[CHECKPOINT] Phase 3: Capturing Dirty Page Table..." << std::endl;
        std::vector<DirtyPageEntry> dirty_pages = CollectDirtyPages();
        std::cout << "[CHECKPOINT]   - Found " << dirty_pages.size() << " dirty pages" << std::endl;

        // 4. Flush all dirty pages to disk
        std::cout << "[CHECKPOINT] Phase 4: Flushing all dirty pages..." << std::endl;
        if (bpm_ != nullptr) {
            bpm_->FlushAllPages();
        }

        // 5. Get current file offset (before writing CHECKPOINT_END)
        std::streampos offset = log_manager_->GetCurrentOffset();

        // 6. Write CHECKPOINT_END record with ATT and DPT
        std::cout << "[CHECKPOINT] Phase 5: Writing CHECKPOINT_END..." << std::endl;
        LogRecord end_record(LogRecordType::CHECKPOINT_END, active_txns, dirty_pages);
        LogRecord::lsn_t checkpoint_lsn = log_manager_->AppendLogRecord(end_record);
        LogRecord::timestamp_t checkpoint_timestamp = end_record.GetTimestamp();

        // 7. Force log to disk
        std::cout << "[CHECKPOINT] Phase 6: Forcing log to disk..." << std::endl;
        log_manager_->Flush(true);

        // 8. Update checkpoint state
        checkpoint_offset_ = offset;
        last_checkpoint_timestamp_ = checkpoint_timestamp;

        // 9. Write master record (atomically)
        std::cout << "[CHECKPOINT] Phase 7: Updating master record..." << std::endl;
        WriteMasterRecord(checkpoint_lsn, offset, checkpoint_timestamp);
        
        // 10. Update checkpoint LSN on ALL tables (for O(delta) time travel - Bug #6 fix)
        if (catalog_ != nullptr) {
            std::cout << "[CHECKPOINT] Phase 8: Updating table checkpoint LSNs..." << std::endl;
            auto all_tables = catalog_->GetAllTables();
            for (auto* table : all_tables) {
                if (table) {
                    table->SetCheckpointLSN(checkpoint_lsn);
                    std::cout << "[CHECKPOINT]   - Table '" << table->name_ 
                              << "' checkpoint LSN set to " << checkpoint_lsn << std::endl;
                }
            }
            std::cout << "[CHECKPOINT]   - Updated " << all_tables.size() << " tables" << std::endl;
            
            // CRITICAL: Save catalog to persist checkpoint LSNs
            catalog_->SaveCatalog();
            std::cout << "[CHECKPOINT] Phase 9: Saved catalog with checkpoint LSNs" << std::endl;
        } else {
            std::cout << "[CHECKPOINT] WARNING: No catalog set - table checkpoint LSNs NOT updated!" << std::endl;
        }

        // 11. Update statistics
        checkpoint_count_++;

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "[CHECKPOINT] Complete!" << std::endl;
        std::cout << "[CHECKPOINT]   - Checkpoint LSN: " << checkpoint_lsn << std::endl;
        std::cout << "[CHECKPOINT]   - File Offset: " << offset << std::endl;
        std::cout << "[CHECKPOINT]   - Duration: " << duration.count() << "ms" << std::endl;
        std::cout << "[CHECKPOINT] ========================================" << std::endl;
    }

    void CheckpointManager::FuzzyCheckpoint() {
        // For now, fuzzy checkpoint is the same as blocking checkpoint
        // In a production system, this would use copy-on-write for ATT/DPT
        // and allow concurrent transactions during the flush phase
        BeginCheckpoint();
    }

    // ========================================================================
    // RECOVERY API
    // ========================================================================

    LogRecord::lsn_t CheckpointManager::GetLastCheckpointLSN() {
        MasterRecord record;
        if (ReadMasterRecord(record)) {
            std::lock_guard<std::mutex> lock(checkpoint_mutex_);
            checkpoint_offset_ = record.checkpoint_offset;
            last_checkpoint_timestamp_ = record.timestamp;
            std::cout << "[CHECKPOINT] Found last checkpoint at LSN: " << record.checkpoint_lsn << std::endl;
            return record.checkpoint_lsn;
        }
        return LogRecord::INVALID_LSN;
    }

    MasterRecord CheckpointManager::GetMasterRecord() {
        MasterRecord record;
        ReadMasterRecord(record);
        return record;
    }

    // ========================================================================
    // BACKGROUND CHECKPOINTING
    // ========================================================================

    void CheckpointManager::StartBackgroundCheckpointing(uint32_t interval_seconds) {
        if (background_checkpointing_enabled_.load()) {
            std::cout << "[CHECKPOINT] Background checkpointing already running" << std::endl;
            return;
        }

        checkpoint_interval_seconds_ = interval_seconds;
        stop_background_thread_ = false;
        background_checkpointing_enabled_ = true;
        
        background_thread_ = std::thread(&CheckpointManager::BackgroundCheckpointThread, this);
        
        std::cout << "[CHECKPOINT] Started background checkpointing (interval: " 
                  << interval_seconds << "s)" << std::endl;
    }

    void CheckpointManager::StopBackgroundCheckpointing() {
        if (!background_checkpointing_enabled_.load()) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(background_mutex_);
            stop_background_thread_ = true;
            background_cv_.notify_all();
        }

        if (background_thread_.joinable()) {
            background_thread_.join();
        }

        background_checkpointing_enabled_ = false;
        std::cout << "[CHECKPOINT] Stopped background checkpointing" << std::endl;
    }

    void CheckpointManager::BackgroundCheckpointThread() {
        std::cout << "[CHECKPOINT] Background thread started" << std::endl;
        
        while (!stop_background_thread_.load()) {
            {
                std::unique_lock<std::mutex> lock(background_mutex_);
                background_cv_.wait_for(lock, 
                    std::chrono::seconds(checkpoint_interval_seconds_),
                    [this] { return stop_background_thread_.load(); });
            }

            if (stop_background_thread_.load()) {
                break;
            }

            try {
                std::cout << "[CHECKPOINT] Background checkpoint triggered" << std::endl;
                BeginCheckpoint();
            } catch (const std::exception& e) {
                std::cerr << "[CHECKPOINT] Background checkpoint failed: " << e.what() << std::endl;
            }
        }

        std::cout << "[CHECKPOINT] Background thread stopped" << std::endl;
    }

    // ========================================================================
    // INTERNAL METHODS
    // ========================================================================

    void CheckpointManager::WriteMasterRecord(LogRecord::lsn_t checkpoint_lsn,
                                               std::streampos offset,
                                               LogRecord::timestamp_t timestamp) {
        try {
            // Write to temporary file first, then rename (atomic)
            std::string temp_path = master_record_path_ + ".tmp";
            
            std::ofstream temp_file(temp_path, std::ios::binary | std::ios::trunc);
            if (!temp_file.is_open()) {
                std::cerr << "[CHECKPOINT] ERROR: Cannot create temp master record" << std::endl;
                return;
            }

            // Write version
            uint32_t version = MasterRecord::CURRENT_VERSION;
            temp_file.write(reinterpret_cast<const char*>(&version), sizeof(uint32_t));
            
            // Write checkpoint LSN
            temp_file.write(reinterpret_cast<const char*>(&checkpoint_lsn), sizeof(LogRecord::lsn_t));
            
            // Write file offset
            temp_file.write(reinterpret_cast<const char*>(&offset), sizeof(std::streampos));
            
            // Write timestamp
            temp_file.write(reinterpret_cast<const char*>(&timestamp), sizeof(LogRecord::timestamp_t));
            
            temp_file.flush();
            temp_file.close();

            // Atomic rename
            std::filesystem::rename(temp_path, master_record_path_);

            std::cout << "[CHECKPOINT] Master record updated: LSN=" << checkpoint_lsn 
                      << ", Offset=" << offset 
                      << ", Timestamp=" << timestamp << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[CHECKPOINT] Error writing master record: " << e.what() << std::endl;
        }
    }

    bool CheckpointManager::ReadMasterRecord(MasterRecord& record) {
        try {
            if (!std::filesystem::exists(master_record_path_)) {
                return false;
            }

            std::ifstream master_file(master_record_path_, std::ios::binary);
            if (!master_file.is_open()) {
                return false;
            }

            // Read version
            master_file.read(reinterpret_cast<char*>(&record.version), sizeof(uint32_t));
            if (master_file.gcount() != sizeof(uint32_t)) {
                return false;
            }

            // Version check
            if (record.version > MasterRecord::CURRENT_VERSION) {
                std::cerr << "[CHECKPOINT] Warning: Master record version " << record.version 
                          << " is newer than supported version " << MasterRecord::CURRENT_VERSION << std::endl;
            }

            // Read checkpoint LSN
            master_file.read(reinterpret_cast<char*>(&record.checkpoint_lsn), sizeof(LogRecord::lsn_t));
            if (master_file.gcount() != sizeof(LogRecord::lsn_t)) {
                return false;
            }

            // Read file offset
            master_file.read(reinterpret_cast<char*>(&record.checkpoint_offset), sizeof(std::streampos));
            if (master_file.gcount() != sizeof(std::streampos)) {
                return false;
            }

            // Read timestamp
            master_file.read(reinterpret_cast<char*>(&record.timestamp), sizeof(LogRecord::timestamp_t));
            // Timestamp is optional for backwards compatibility

            return true;
        } catch (const std::exception& e) {
            std::cerr << "[CHECKPOINT] Error reading master record: " << e.what() << std::endl;
            return false;
        }
    }

    std::vector<DirtyPageEntry> CheckpointManager::CollectDirtyPages() {
        std::vector<DirtyPageEntry> dirty_pages;
        
        // In a production system, the BufferPoolManager would expose a method
        // to get the dirty page list. For now, we return an empty list.
        // The actual dirty pages are flushed by FlushAllPages() anyway.
        
        // TODO: When BPM exposes GetDirtyPages(), use it:
        // if (bpm_ != nullptr) {
        //     auto pages = bpm_->GetDirtyPages();
        //     for (const auto& page : pages) {
        //         DirtyPageEntry entry;
        //         entry.page_id = page.page_id;
        //         entry.recovery_lsn = page.first_dirty_lsn;
        //         dirty_pages.push_back(entry);
        //     }
        // }

        return dirty_pages;
    }

} // namespace chronosdb

