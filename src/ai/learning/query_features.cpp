#include "ai/learning/query_features.h"
#include "catalog/catalog.h"
#include "parser/statement.h"

#include <cmath>

namespace chronosdb {
namespace ai {

std::array<double, QUERY_FEATURE_DIMENSIONS> QueryFeatures::ToArray() const {
    return {table_row_count_log, where_clause_count, has_equality_predicate,
            has_index_available, selectivity_estimate, column_count,
            has_order_by, has_limit};
}

QueryFeatureExtractor::QueryFeatureExtractor(Catalog* catalog)
    : catalog_(catalog) {}

QueryFeatures QueryFeatureExtractor::Extract(const SelectStatement* stmt,
                                              const std::string& table_name) const {
    QueryFeatures features{};

    // Feature 1: Table size (log2 of estimated row count)
    double row_count = EstimateRowCount(table_name);
    features.table_row_count_log = row_count > 0 ? std::log2(row_count) : 0.0;

    // Feature 2: WHERE clause complexity
    features.where_clause_count =
        static_cast<double>(stmt->where_clause_.size());

    // Feature 3: Equality predicate on first condition
    if (!stmt->where_clause_.empty() && stmt->where_clause_[0].op == "=") {
        features.has_equality_predicate = 1.0;
    }

    // Feature 4: Index availability for first predicate column
    features.has_index_available =
        HasIndexForFirstPredicate(stmt, table_name) ? 1.0 : 0.0;

    // Feature 5: Selectivity estimate
    features.selectivity_estimate = EstimateSelectivity(stmt, table_name);

    // Feature 6: Column count
    if (stmt->select_all_) {
        auto* table_meta = catalog_->GetTable(table_name);
        features.column_count = table_meta
            ? static_cast<double>(table_meta->schema_.GetColumnCount())
            : 1.0;
    } else {
        features.column_count = static_cast<double>(stmt->columns_.size());
    }

    // Feature 7: ORDER BY presence
    features.has_order_by = stmt->order_by_.empty() ? 0.0 : 1.0;

    // Feature 8: LIMIT presence
    features.has_limit = stmt->limit_ > 0 ? 1.0 : 0.0;

    return features;
}

double QueryFeatureExtractor::EstimateSelectivity(
    const SelectStatement* stmt, const std::string& table_name) const {
    if (stmt->where_clause_.empty()) {
        return 1.0; // No filter = full table
    }

    // Heuristic selectivity based on operator type
    double selectivity = 1.0;
    for (const auto& cond : stmt->where_clause_) {
        if (cond.op == "=") {
            selectivity *= 0.1;  // Equality: ~10% of rows
        } else if (cond.op == ">" || cond.op == "<" ||
                   cond.op == ">=" || cond.op == "<=") {
            selectivity *= 0.33; // Range: ~33% of rows
        } else if (cond.op == "!=" || cond.op == "<>") {
            selectivity *= 0.9;  // Not equal: ~90% of rows
        } else if (cond.op == "LIKE") {
            selectivity *= 0.25; // LIKE: ~25% of rows
        }
    }
    return selectivity;
}

double QueryFeatureExtractor::EstimateRowCount(
    const std::string& table_name) const {
    auto* table_meta = catalog_->GetTable(table_name);
    if (!table_meta || !table_meta->table_heap_) {
        return 0.0;
    }
    return static_cast<double>(table_meta->table_heap_->CountAllTuples());
}

bool QueryFeatureExtractor::HasIndexForFirstPredicate(
    const SelectStatement* stmt, const std::string& table_name) const {
    if (stmt->where_clause_.empty()) {
        return false;
    }

    const std::string& col_name = stmt->where_clause_[0].column;
    auto indexes = catalog_->GetTableIndexes(table_name);
    for (const auto* idx : indexes) {
        if (idx->col_name_ == col_name) {
            return true;
        }
    }
    return false;
}

} // namespace ai
} // namespace chronosdb
