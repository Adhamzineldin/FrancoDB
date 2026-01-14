// network/database_registry.h
#pragma once

#include <map>
#include <memory>
#include <string>
#include <filesystem>

#include "storage/disk/disk_manager.h"
#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "common/config.h"

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

    std::shared_ptr<DbEntry> GetOrCreate(const std::string &name, size_t pool_size = BUFFER_POOL_SIZE) {
        if (registry_.count(name)) return registry_[name];

        std::filesystem::create_directories("data");
        auto entry = std::make_shared<DbEntry>();
        entry->dm = std::make_unique<DiskManager>("data/" + name);
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

private:
    std::map<std::string, std::shared_ptr<DbEntry>> registry_;
    std::map<std::string, BufferPoolManager*> external_bpm_;
    std::map<std::string, Catalog*> external_catalog_;
};

} // namespace francodb

