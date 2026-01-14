#include "common/rid.h"
#include <sstream>

namespace francodb {

RID::RID() : page_id_(INVALID_PAGE_ID), slot_num_(-1) {}

RID::RID(page_id_t page_id, uint32_t slot_num) 
    : page_id_(page_id), slot_num_(slot_num) {}

void RID::Set(page_id_t page_id, uint32_t slot_num) {
    page_id_ = page_id;
    slot_num_ = slot_num;
}

bool RID::operator==(const RID &other) const {
    return page_id_ == other.page_id_ && slot_num_ == other.slot_num_;
}

std::string RID::ToString() const {
    std::stringstream os;
    os << "RID(" << page_id_ << ", " << slot_num_ << ")";
    return os.str();
}

} // namespace francodb

