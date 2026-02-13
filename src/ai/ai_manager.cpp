#include "ai/ai_manager.h"
#include "ai/ai_scheduler.h"
#include "ai/dml_observer.h"
#include "ai/metrics_store.h"
#include "ai/learning/learning_engine.h"
#include "ai/immune/immune_system.h"
#include "ai/temporal/temporal_index_manager.h"
#include "common/logger.h"

namespace chronosdb {
namespace ai {

AIManager& AIManager::Instance() {
    static AIManager instance;
    return instance;
}

AIManager::~AIManager() {
    if (initialized_.load()) {
        Shutdown();
    }
}

void AIManager::Initialize(Catalog* catalog, IBufferManager* bpm,
                            LogManager* log_manager,
                            CheckpointManager* checkpoint_mgr) {
    if (initialized_.load()) return;

    catalog_ = catalog;
    bpm_ = bpm;
    log_manager_ = log_manager;
    checkpoint_mgr_ = checkpoint_mgr;

    LOG_INFO("AIManager", "Initializing ChronosDB AI Layer...");

    // Start the shared scheduler
    AIScheduler::Instance().Start();

    // Phase 1: Self-Learning Execution Engine
    learning_engine_ = std::make_unique<LearningEngine>(catalog_);
    DMLObserverRegistry::Instance().Register(learning_engine_.get());
    learning_engine_->Start();

    // Phase 2: Immune System
    immune_system_ = std::make_unique<ImmuneSystem>(
        log_manager_, catalog_, bpm_, checkpoint_mgr_);
    DMLObserverRegistry::Instance().Register(immune_system_.get());
    immune_system_->Start();

    // Phase 3: Temporal Index Manager
    temporal_index_mgr_ = std::make_unique<TemporalIndexManager>(
        log_manager_, catalog_, bpm_, checkpoint_mgr_);
    temporal_index_mgr_->Start();

    initialized_ = true;
    LOG_INFO("AIManager", "AI Layer initialized: Learning Engine, "
             "Immune System, Temporal Index Manager");
}

void AIManager::Shutdown() {
    if (!initialized_.load()) return;

    LOG_INFO("AIManager", "Shutting down AI Layer...");

    // Stop in reverse order
    if (temporal_index_mgr_) {
        temporal_index_mgr_->Stop();
    }
    if (immune_system_) {
        DMLObserverRegistry::Instance().Unregister(immune_system_.get());
        immune_system_->Stop();
    }
    if (learning_engine_) {
        DMLObserverRegistry::Instance().Unregister(learning_engine_.get());
        learning_engine_->Stop();
    }

    AIScheduler::Instance().Stop();

    temporal_index_mgr_.reset();
    immune_system_.reset();
    learning_engine_.reset();

    initialized_ = false;
    LOG_INFO("AIManager", "AI Layer shut down");
}

bool AIManager::IsInitialized() const {
    return initialized_.load();
}

LearningEngine* AIManager::GetLearningEngine() {
    return learning_engine_.get();
}

ImmuneSystem* AIManager::GetImmuneSystem() {
    return immune_system_.get();
}

TemporalIndexManager* AIManager::GetTemporalIndexManager() {
    return temporal_index_mgr_.get();
}

AIManager::AIStatus AIManager::GetStatus() const {
    AIStatus status{};
    status.metrics_recorded = MetricsStore::Instance().GetTotalRecorded();
    status.scheduled_tasks = AIScheduler::Instance().GetScheduledTasks().size();

    if (learning_engine_) {
        status.learning_engine_active = true;
        status.learning_summary = learning_engine_->GetSummary();
    }
    if (immune_system_) {
        status.immune_system_active = true;
        status.immune_summary = immune_system_->GetSummary();
    }
    if (temporal_index_mgr_) {
        status.temporal_index_active = true;
        status.temporal_summary = temporal_index_mgr_->GetSummary();
    }

    return status;
}

} // namespace ai
} // namespace chronosdb
