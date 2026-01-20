// network/database_registry.h
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>

#include "storage/disk/disk_manager.h"
#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "common/config.h"
#include "common/config_manager.h"

namespace francodb {

struct DbEntry {
    std::unique_ptr<DiskManager> dm;
    std::unique_ptr<BufferPoolManager> bpm;
    std::unique_ptr<Catalog> catalog;
};

class DatabaseRegistry {
public:
    DatabaseRegistry() = default;

    void RegisterExternal(const std::string &name, BufferPoolManager *bpm, Catalog *catalog) {
        auto entry = std::make_shared<DbEntry>();
        entry->dm = nullptr;
        entry->bpm.reset();
        entry->catalog.reset();
        external_bpm_[name] = bpm;
        external_catalog_[name] = catalog;
        registry_[name] = entry;
    }

    std::shared_ptr<DbEntry> Get(const std::string &name) {
        auto it = registry_.find(name);
        if (it != registry_.end()) return it->second;
        return nullptr;
    }

    // Get all registered database names (both external and owned)
    std::vector<std::string> GetAllDatabaseNames() const {
        std::vector<std::string> names;
        for (const auto &[name, entry] : registry_) {
            names.push_back(name);
        }
        return names;
    }

        std::shared_ptr<DbEntry> GetOrCreate(const std::string &name, size_t pool_size = BUFFER_POOL_SIZE) {
        if (registry_.count(name)) return registry_[name];

        // Use configured data directory
        auto& config = ConfigManager::GetInstance();
        std::string data_dir = config.GetDataDirectory();
        
        auto entry = std::make_shared<DbEntry>();
        
        // Create database DIRECTORY structure: data/dbname/
        std::filesystem::path db_dir = std::filesystem::path(data_dir) / name;
        std::filesystem::create_directories(db_dir);
        
        // Database file inside its directory: data/dbname/dbname.francodb
        std::filesystem::path db_file = db_dir / (name + ".francodb");
        entry->dm = std::make_unique<DiskManager>(db_file.string());
        
        // Apply encryption if enabled
        if (config.IsEncryptionEnabled() && !config.GetEncryptionKey().empty()) {
            entry->dm->SetEncryptionKey(config.GetEncryptionKey());
        }
        
        entry->bpm = std::make_unique<BufferPoolManager>(pool_size, entry->dm.get());
        entry->catalog = std::make_unique<Catalog>(entry->bpm.get());
        registry_[name] = entry;
        return entry;
    }

    BufferPoolManager* ExternalBpm(const std::string &name) const {
        auto it = external_bpm_.find(name);
        return (it == external_bpm_.end()) ? nullptr : it->second;
    }

    Catalog* ExternalCatalog(const std::string &name) const {
        auto it = external_catalog_.find(name);
        return (it == external_catalog_.end()) ? nullptr : it->second;
    }
    
    
    bool Remove(const std::string &name) {
        auto it = registry_.find(name);
        if (it == registry_.end()) return false;
        
        // Flush before removing
        if (it->second->bpm) {
            it->second->bpm->FlushAllPages();
        }
        if (it->second->catalog) {
            it->second->catalog->SaveCatalog();
        }
        // Release file handles to allow directory deletion on Windows
        it->second->catalog.reset();
        it->second->bpm.reset();
        it->second->dm.reset();
        
        registry_.erase(it);
        external_bpm_.erase(name);
        external_catalog_.erase(name);
        return true;
    }

    // Flush all databases (save all catalogs and unpin all pages)
    void FlushAllDatabases();

private:
    std::map<std::string, std::shared_ptr<DbEntry>> registry_;
    std::map<std::string, BufferPoolManager*> external_bpm_;
    std::map<std::string, Catalog*> external_catalog_;
};

} // namespace francodb

