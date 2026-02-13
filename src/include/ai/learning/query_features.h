#pragma once

#include <array>
#include <string>

#include "ai/ai_config.h"

namespace chronosdb {

class Catalog;
class SelectStatement;

namespace ai {

/**
 * Numerical feature vector extracted from a SELECT query.
 * Used as context for the UCB1 bandit's strategy selection.
 */
struct QueryFeatures {
    double table_row_count_log;    // log2(estimated row count)
    double where_clause_count;     // Number of WHERE conditions
    double has_equality_predicate; // 1.0 if first predicate is "="
    double has_index_available;    // 1.0 if matching index exists
    double selectivity_estimate;   // Estimated fraction of rows matched (0.0 - 1.0)
    double column_count;           // Number of columns requested
    double has_order_by;           // 1.0 if ORDER BY present
    double has_limit;              // 1.0 if LIMIT present

    std::array<double, QUERY_FEATURE_DIMENSIONS> ToArray() const;
};

/**
 * QueryFeatureExtractor - Extracts numerical features from SELECT statements.
 *
 * Single responsibility: transform a SelectStatement into a QueryFeatures vector
 * for the bandit algorithm. No database modification, read-only access to Catalog.
 */
class QueryFeatureExtractor {
public:
    explicit QueryFeatureExtractor(Catalog* catalog);

    QueryFeatures Extract(const SelectStatement* stmt,
                          const std::string& table_name) const;

private:
    Catalog* catalog_;

    double EstimateSelectivity(const SelectStatement* stmt,
                               const std::string& table_name) const;
    double EstimateRowCount(const std::string& table_name) const;
    bool HasIndexForFirstPredicate(const SelectStatement* stmt,
                                   const std::string& table_name) const;
};

} // namespace ai
} // namespace chronosdb
