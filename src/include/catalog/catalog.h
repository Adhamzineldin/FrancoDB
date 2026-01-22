#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include <sstream>
#include <iostream>

#include "catalog/table_metadata.h"
#include "catalog/index_info.h"
#include "storage/storage_interface.h"  // For IBufferManager
#include "storage/page/free_page_manager.h"

namespace francodb {

    // Forward declaration
    class BufferPoolManager;

    class Catalog {
    public:
        // Accept IBufferManager interface for polymorphic buffer pool usage
        // Allows both BufferPoolManager and PartitionedBufferPoolManager
        Catalog(IBufferManager *bpm);
        ~Catalog();

        TableMetadata *CreateTable(const std::string &table_name, const Schema &schema);
        TableMetadata *GetTable(const std::string &name);
        IndexInfo *CreateIndex(const std::string &index_name, const std::string &table_name, const std::string &col_name);
        std::vector<IndexInfo*> GetTableIndexes(const std::string &table_name);
        bool DropTable(const std::string &table_name);
        void SaveCatalog();
        void LoadCatalog();
        IndexInfo *GetIndex(const std::string &index_name);
        std::vector<std::string> GetAllTableNames();
        std::vector<TableMetadata*> GetAllTables();

    private:
        IBufferManager *bpm_;
        std::mutex latch_;
        std::atomic<uint32_t> next_table_oid_;
        std::unordered_map<uint32_t, std::unique_ptr<TableMetadata>> tables_;
        std::unordered_map<std::string, uint32_t> names_to_oid_;
        
        std::unordered_map<std::string, std::unique_ptr<IndexInfo>> indexes_;
        std::unordered_map<std::string, IndexInfo*> index_names_;
    };
}