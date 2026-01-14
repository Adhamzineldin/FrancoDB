#include "catalog/catalog.h"
#include "storage/table/table_heap.h"
#include "storage/table/schema.h"
#include "catalog/index_info.h"
#include "buffer/buffer_pool_manager.h"
#include "storage/page/free_page_manager.h"
#include <iostream>
#include <sstream>

namespace francodb {

Catalog::Catalog(BufferPoolManager *bpm) : bpm_(bpm), next_table_oid_(0) {
    LoadCatalog();
}

Catalog::~Catalog() {
    SaveCatalog();
}

TableMetadata *Catalog::CreateTable(const std::string &table_name, const Schema &schema) {
    std::lock_guard<std::mutex> lock(latch_);
    if (names_to_oid_.count(table_name) > 0) return nullptr;

    uint32_t table_oid = next_table_oid_++;
    auto table_heap = std::make_unique<TableHeap>(bpm_);
    page_id_t first_page_id = table_heap->GetFirstPageId();
    
    auto metadata = std::make_unique<TableMetadata>(schema, table_name, std::move(table_heap), first_page_id, table_oid);
    TableMetadata *ptr = metadata.get();
    tables_[table_oid] = std::move(metadata);
    names_to_oid_[table_name] = table_oid;
    
    return ptr;
}

TableMetadata *Catalog::GetTable(const std::string &name) {
     std::lock_guard<std::mutex> lock(latch_);
     if (names_to_oid_.count(name) == 0) return nullptr;
     return tables_[names_to_oid_[name]].get();
}

IndexInfo *Catalog::CreateIndex(const std::string &index_name, const std::string &table_name, const std::string &col_name) {
    std::lock_guard<std::mutex> lock(latch_);
    if (names_to_oid_.find(table_name) == names_to_oid_.end()) return nullptr;
    if (index_names_.find(index_name) != index_names_.end()) return nullptr;

    auto *table = tables_[names_to_oid_[table_name]].get();
    int col_idx = table->schema_.GetColIdx(col_name);
    if (col_idx == -1) return nullptr;
    TypeId type = table->schema_.GetColumn(col_idx).GetType();

    auto index_info = std::make_unique<IndexInfo>(index_name, table_name, col_name, type, bpm_);
    IndexInfo *ptr = index_info.get();
    
    indexes_[index_name] = std::move(index_info);
    index_names_[index_name] = ptr;
    return ptr;
}

std::vector<IndexInfo*> Catalog::GetTableIndexes(const std::string &table_name) {
     std::vector<IndexInfo*> result;
     for (auto &pair : indexes_) {
         if (pair.second->table_name_ == table_name) {
             result.push_back(pair.second.get());
         }
     }
     return result;
}

bool Catalog::DropTable(const std::string &table_name) {
    std::lock_guard<std::mutex> lock(latch_);
    if (names_to_oid_.count(table_name) == 0) return false;
    uint32_t oid = names_to_oid_[table_name];
    names_to_oid_.erase(table_name);
    tables_.erase(oid);
    return true;
}

void Catalog::SaveCatalog() {
    // 1. Serialize everything to a string stream
    std::stringstream ss;

    // Serialize Tables
    for (auto &pair : tables_) {
        TableMetadata *t = pair.second.get();
        ss << "TABLE " << t->name_ << " " << t->first_page_id_ << " " << t->oid_ << " ";
        auto columns = t->schema_.GetColumns();
        ss << columns.size() << " ";
        for (auto &col : columns) {
            ss << col.GetName() << " " << static_cast<int>(col.GetType()) << " " 
               << (col.IsPrimaryKey() ? "1" : "0") << " ";
        }
        ss << "\n";
    }

    // Serialize Indexes
    for (auto &pair : indexes_) {
        IndexInfo *idx = pair.second.get();
        ss << "INDEX " << idx->name_ << " " << idx->table_name_ << " " << idx->col_name_ << " " << idx->b_plus_tree_->GetRootPageId() << "\n";
    }

    // 2. Handover string to DiskManager for Secure Write
    bpm_->GetDiskManager()->WriteMetadata(ss.str());
}

void Catalog::LoadCatalog() {
    std::string data;
    
    // 1. Request Secure Read from DiskManager
    bool success = bpm_->GetDiskManager()->ReadMetadata(data);
    if (!success) return; 

    std::stringstream in(data);
    std::string type;
    
    // 2. Parse (Reconstruct Objects)
    while (in >> type) {
        if (type == "TABLE") {
            std::string name;
            page_id_t first_page;
            uint32_t oid;
            int col_count;
            
            in >> name >> first_page >> oid >> col_count;
            
            std::vector<Column> cols;
            for (int i = 0; i < col_count; i++) {
                std::string col_name;
                int type_int;
                int is_pk_int = 0; // Default to 0 for backward compatibility
                in >> col_name >> type_int;
                // Try to read PRIMARY KEY flag (may not exist in old databases)
                if (in.peek() != EOF && in.peek() != '\n') {
                    in >> is_pk_int;
                }
                bool is_pk = (is_pk_int != 0);
                cols.emplace_back(col_name, static_cast<TypeId>(type_int), is_pk);
            }
            
            Schema schema(cols);
            // Reconnect to existing heap
            auto table_heap = std::make_unique<TableHeap>(bpm_, first_page); 
            auto metadata = std::make_unique<TableMetadata>(schema, name, std::move(table_heap), first_page, oid);
            
            tables_[oid] = std::move(metadata);
            names_to_oid_[name] = oid;
            
            if (oid >= next_table_oid_) next_table_oid_ = oid + 1;

        } else if (type == "INDEX") {
            std::string name, table, col;
            page_id_t root_page;
            in >> name >> table >> col >> root_page;

            if (names_to_oid_.find(table) != names_to_oid_.end()) {
                 TableMetadata *t = tables_[names_to_oid_[table]].get();
                 int col_idx = t->schema_.GetColIdx(col);
                 TypeId key_type = t->schema_.GetColumn(col_idx).GetType();

                 auto index_info = std::make_unique<IndexInfo>(name, table, col, key_type, bpm_);
                 // CRITICAL: Restore the B+Tree Root
                 index_info->b_plus_tree_->SetRootPageId(root_page);
                 
                 IndexInfo *ptr = index_info.get();
                 indexes_[name] = std::move(index_info);
                 index_names_[name] = ptr;
            }
        }
    }
    std::cout << "[SYSTEM] Database restored from disk." << std::endl;
}

IndexInfo *Catalog::GetIndex(const std::string &index_name) {
    std::lock_guard<std::mutex> lock(latch_);
    if (index_names_.find(index_name) == index_names_.end()) {
        return nullptr;
    }
    return index_names_[index_name];
}

} // namespace francodb

