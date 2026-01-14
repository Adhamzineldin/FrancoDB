#pragma once
#include <string>
#include <memory>
#include "storage/index/b_plus_tree.h"
#include "storage/table/schema.h"
#include "storage/index/index_key.h" 
#include "common/config.h"
#include "common/rid.h"

namespace francodb {

    struct IndexInfo {
        IndexInfo(std::string name, std::string table_name, std::string col_name, 
                  TypeId key_type, BufferPoolManager *bpm);

        std::string name_;
        std::string table_name_;
        std::string col_name_;
    
        // The B+Tree Instance
        std::unique_ptr<BPlusTree<GenericKey<8>, RID, GenericComparator<8>>> b_plus_tree_;
    };

} // namespace francodb