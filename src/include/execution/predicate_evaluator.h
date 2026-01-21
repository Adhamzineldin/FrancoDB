#pragma once

#include <vector>
#include <cmath>
#include <string>
#include "common/value.h"
#include "common/type.h"
#include "storage/tuple.h"
#include "catalog/schema.h"

namespace francodb {

/**
 * WhereCondition structure (should match your parser's definition)
 */
struct WhereCondition;  // Forward declaration - use your existing parser definition

/**
 * LogicType for chaining conditions
 */
enum class LogicType { NONE, AND, OR };

/**
 * PredicateEvaluator - Shared utility for evaluating WHERE clauses.
 * 
 * PROBLEM SOLVED:
 * - Eliminates ~180 lines of duplicated code across:
 *   - SeqScanExecutor::EvaluatePredicate()
 *   - DeleteExecutor::EvaluatePredicate()
 *   - UpdateExecutor::EvaluatePredicate()
 * 
 * USAGE:
 *   if (PredicateEvaluator::Evaluate(tuple, schema, where_clause)) {
 *       // Tuple matches predicate
 *   }
 */
class PredicateEvaluator {
public:
    /**
     * Generic condition structure for evaluation.
     */
    struct Condition {
        std::string column;
        std::string op;
        Value value;
        std::vector<Value> in_values;  // For IN operator
        LogicType next_logic = LogicType::NONE;
    };

    /**
     * Evaluate a tuple against a list of conditions.
     * 
     * @param tuple The tuple to evaluate
     * @param schema Schema for column lookup
     * @param conditions List of WHERE conditions
     * @return true if tuple matches all conditions
     */
    static bool Evaluate(const Tuple& tuple, 
                         const Schema& schema,
                         const std::vector<Condition>& conditions) {
        if (conditions.empty()) {
            return true;
        }

        bool result = true;
        
        for (size_t i = 0; i < conditions.size(); ++i) {
            const auto& cond = conditions[i];
            
            int col_idx = schema.GetColIdx(cond.column);
            if (col_idx < 0) {
                // Column not found - condition fails
                return false;
            }
            
            Value tuple_val = tuple.GetValue(schema, col_idx);
            bool match = EvaluateCondition(tuple_val, cond);
            
            // Logic chaining (AND/OR)
            if (i == 0) {
                result = match;
            } else {
                LogicType logic = conditions[i - 1].next_logic;
                if (logic == LogicType::AND) {
                    result = result && match;
                } else if (logic == LogicType::OR) {
                    result = result || match;
                }
            }
        }
        
        return result;
    }

private:
    /**
     * Evaluate a single condition against a value.
     */
    static bool EvaluateCondition(const Value& tuple_val, const Condition& cond) {
        // Handle IN operator
        if (cond.op == "IN") {
            return EvaluateIn(tuple_val, cond.in_values);
        }
        
        // Handle standard comparison operators
        return CompareValues(tuple_val, cond.value, cond.op);
    }
    
    /**
     * Evaluate IN operator.
     */
    static bool EvaluateIn(const Value& tuple_val, const std::vector<Value>& in_values) {
        for (const auto& in_val : in_values) {
            if (ValuesEqual(tuple_val, in_val)) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * Compare two values with a given operator.
     */
    static bool CompareValues(const Value& left, const Value& right, const std::string& op) {
        TypeId type = left.GetTypeId();
        
        if (op == "=") {
            return ValuesEqual(left, right);
        } else if (op == "!=" || op == "<>") {
            return !ValuesEqual(left, right);
        } else if (op == ">") {
            return CompareGreater(left, right, type);
        } else if (op == "<") {
            return CompareLess(left, right, type);
        } else if (op == ">=") {
            return CompareGreater(left, right, type) || ValuesEqual(left, right);
        } else if (op == "<=") {
            return CompareLess(left, right, type) || ValuesEqual(left, right);
        } else if (op == "LIKE") {
            return EvaluateLike(left.GetAsString(), right.GetAsString());
        }
        
        return false;
    }
    
    /**
     * Check if two values are equal.
     */
    static bool ValuesEqual(const Value& left, const Value& right) {
        TypeId type = left.GetTypeId();
        
        switch (type) {
            case TypeId::INTEGER:
                return left.GetAsInteger() == right.GetAsInteger();
            case TypeId::DECIMAL:
                return std::abs(left.GetAsDouble() - right.GetAsDouble()) < 0.0001;
            case TypeId::VARCHAR:
            default:
                return left.GetAsString() == right.GetAsString();
        }
    }
    
    /**
     * Check if left > right.
     */
    static bool CompareGreater(const Value& left, const Value& right, TypeId type) {
        switch (type) {
            case TypeId::INTEGER:
                return left.GetAsInteger() > right.GetAsInteger();
            case TypeId::DECIMAL:
                return left.GetAsDouble() > right.GetAsDouble();
            case TypeId::VARCHAR:
            default:
                return left.GetAsString() > right.GetAsString();
        }
    }
    
    /**
     * Check if left < right.
     */
    static bool CompareLess(const Value& left, const Value& right, TypeId type) {
        switch (type) {
            case TypeId::INTEGER:
                return left.GetAsInteger() < right.GetAsInteger();
            case TypeId::DECIMAL:
                return left.GetAsDouble() < right.GetAsDouble();
            case TypeId::VARCHAR:
            default:
                return left.GetAsString() < right.GetAsString();
        }
    }
    
    /**
     * Evaluate SQL LIKE pattern matching.
     * Supports % (any characters) and _ (single character).
     */
    static bool EvaluateLike(const std::string& text, const std::string& pattern) {
        size_t t = 0, p = 0;
        size_t star_idx = std::string::npos;
        size_t match_idx = 0;
        
        while (t < text.size()) {
            if (p < pattern.size() && (pattern[p] == text[t] || pattern[p] == '_')) {
                t++;
                p++;
            } else if (p < pattern.size() && pattern[p] == '%') {
                star_idx = p;
                match_idx = t;
                p++;
            } else if (star_idx != std::string::npos) {
                p = star_idx + 1;
                match_idx++;
                t = match_idx;
            } else {
                return false;
            }
        }
        
        while (p < pattern.size() && pattern[p] == '%') {
            p++;
        }
        
        return p == pattern.size();
    }
};

} // namespace francodb

