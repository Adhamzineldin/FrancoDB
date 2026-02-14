#include "ai/learning/execution_plan.h"

#include <sstream>

namespace chronosdb {
namespace ai {

std::string ExecutionPlan::ToString() const {
    std::ostringstream oss;
    oss << "Plan{scan=";
    oss << (scan_strategy == ScanStrategy::INDEX_SCAN ? "INDEX" : "SEQ");

    oss << ", filter=";
    switch (filter_strategy) {
        case FilterStrategy::ORIGINAL_ORDER: oss << "ORIGINAL"; break;
        case FilterStrategy::SELECTIVITY_ORDER: oss << "SELECTIVITY"; break;
        case FilterStrategy::COST_ORDER: oss << "COST"; break;
    }

    if (!filter_order.empty()) {
        oss << "[";
        for (size_t i = 0; i < filter_order.size(); ++i) {
            if (i > 0) oss << ",";
            oss << filter_order[i];
        }
        oss << "]";
    }

    oss << ", proj=";
    oss << (projection_strategy == ProjectionStrategy::EARLY_MATERIALIZATION
                ? "EARLY" : "LATE");

    oss << ", limit=";
    oss << (limit_strategy == LimitStrategy::EARLY_TERMINATION
                ? "EARLY_TERM" : "FULL_SCAN");

    oss << (ai_generated ? ", AI" : ", DEFAULT");
    oss << "}";
    return oss.str();
}

} // namespace ai
} // namespace chronosdb
