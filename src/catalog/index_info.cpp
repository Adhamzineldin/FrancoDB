#include "catalog/index_info.h"
#include "storage/index/b_plus_tree.h"
#include "storage/index/index_key.h"
#include "common/config.h"
#include "common/rid.h"

namespace francodb {

IndexInfo::IndexInfo(std::string name, std::string table_name, std::string col_name, 
              TypeId key_type, BufferPoolManager *bpm)
    : name_(std::move(name)), table_name_(std::move(table_name)), col_name_(std::move(col_name)) {

    // Calculate max_size for leaf and internal pages
    // Leaf page: Header (24B) + NextPageId (4B) = 28B overhead
    // Each entry: GenericKey<8> (8B) + RID (8B) = 16B
    // Available space: PAGE_SIZE - 28 = 4096 - 28 = 4068 bytes
    // Max leaf entries: 4068 / 16 = 254
    const int leaf_max_size = (PAGE_SIZE - 28) / (sizeof(GenericKey<8>) + sizeof(RID));
    
    // Internal page: Header (24B) overhead
    // Each entry: GenericKey<8> (8B) + page_id_t (4B) = 12B
    // Available space: PAGE_SIZE - 24 = 4096 - 24 = 4072 bytes
    // Max internal entries: 4072 / 12 = 339
    const int internal_max_size = (PAGE_SIZE - 24) / (sizeof(GenericKey<8>) + sizeof(page_id_t));
    
    // Initialize B+Tree with calculated max sizes
    b_plus_tree_ = std::make_unique<BPlusTree<GenericKey<8>, RID, GenericComparator<8>>>(
        name_, bpm, GenericComparator<8>(key_type), leaf_max_size, internal_max_size);
}

} // namespace francodb

