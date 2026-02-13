#pragma once

#include <atomic>
#include <memory>
#include <string>

namespace chronosdb {

// Forward declarations for existing engine types
class Catalog;
class IBufferManager;
class LogManager;
class CheckpointManager;

namespace ai {

// Forward declarations for AI subsystems
class LearningEngine;
class ImmuneSystem;
class TemporalIndexManager;

/**
 * AIManager - Top-level coordinator for the ChronosDB AI Layer.
 *
 * Owns all three AI subsystems. Provides the single initialization
 * and shutdown point. Entry point for SHOW AI STATUS.
 */
class AIManager {
public:
    static AIManager& Instance();

    // Lifecycle
    void Initialize(Catalog* catalog, IBufferManager* bpm,
                    LogManager* log_manager, CheckpointManager* checkpoint_mgr);
    void Shutdown();
    bool IsInitialized() const;

    // Access sub-systems
    LearningEngine* GetLearningEngine();
    ImmuneSystem* GetImmuneSystem();
    TemporalIndexManager* GetTemporalIndexManager();

    // Status for SHOW AI STATUS
    struct AIStatus {
        bool learning_engine_active;
        bool immune_system_active;
        bool temporal_index_active;
        size_t metrics_recorded;
        size_t scheduled_tasks;
        std::string learning_summary;
        std::string immune_summary;
        std::string temporal_summary;
    };
    AIStatus GetStatus() const;

private:
    AIManager() = default;
    ~AIManager();
    AIManager(const AIManager&) = delete;
    AIManager& operator=(const AIManager&) = delete;

    std::unique_ptr<LearningEngine> learning_engine_;
    std::unique_ptr<ImmuneSystem> immune_system_;
    std::unique_ptr<TemporalIndexManager> temporal_index_mgr_;

    std::atomic<bool> initialized_{false};

    // Non-owning references to engine dependencies
    Catalog* catalog_{nullptr};
    IBufferManager* bpm_{nullptr};
    LogManager* log_manager_{nullptr};
    CheckpointManager* checkpoint_mgr_{nullptr};
};

} // namespace ai
} // namespace chronosdb
