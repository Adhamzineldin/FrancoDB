#include "storage/table/column.h"
#include "common/type.h"

namespace francodb {

Column::Column(std::string name, TypeId type, bool is_primary_key)
    : name_(std::move(name)), type_(type), length_(Type::GetTypeSize(type)), is_primary_key_(is_primary_key) {}

Column::Column(std::string name, TypeId type, uint32_t length, bool is_primary_key)
    : name_(std::move(name)), type_(type), length_(length), is_primary_key_(is_primary_key) {}

std::string Column::ToString() const {
    return name_ + ":" + Type::TypeToString(type_) + (is_primary_key_ ? " (PRIMARY KEY)" : "");
}

} // namespace francodb

