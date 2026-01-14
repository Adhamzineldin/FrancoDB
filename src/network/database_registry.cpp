// database_registry.cpp
#include "network/database_registry.h"

namespace francodb {

void DatabaseRegistry::FlushAllDatabases() {
    // Flush all registered databases (both owned and external)
    for (const auto& [name, entry] : registry_) {
        if (entry->catalog) {
            entry->catalog->SaveCatalog();
        }
        if (entry->bpm) {
            entry->bpm->FlushAllPages();
        }
    }
    // Also flush external databases
    for (const auto& [name, catalog] : external_catalog_) {
        if (catalog) {
            catalog->SaveCatalog();
        }
    }
    for (const auto& [name, bpm] : external_bpm_) {
        if (bpm) {
            bpm->FlushAllPages();
        }
    }
}

} // namespace francodb
