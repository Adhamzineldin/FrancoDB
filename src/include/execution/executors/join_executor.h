#pragma once

#include <vector>
#include <memory>
#include "execution/executors/abstract_executor.h"
#include "parser/advanced_statements.h"
#include "storage/table/tuple.h"
#include "common/config.h"

namespace francodb {

/**
 * JoinExecutor: Executes JOIN operations between two or more tables
 * 
 * SOLID Principles Applied:
 * - Single Responsibility: Only handles JOIN logic
 * - Open/Closed: Extensible for new join types
 * - Liskov Substitution: Properly implements AbstractExecutor
 * - Interface Segregation: Uses minimal dependencies
 * - Dependency Inversion: Depends on abstractions (AbstractExecutor)
 * 
 * Supports:
 * - INNER JOIN
 * - LEFT OUTER JOIN
 * - RIGHT OUTER JOIN
 * - FULL OUTER JOIN
 * - CROSS JOIN
 */
class JoinExecutor : public AbstractExecutor {
public:
    /**
     * Constructor following Dependency Injection pattern
     * @param exec_ctx Executor context
     * @param left_executor Left table executor
     * @param right_executor Right table executor
     * @param join_type Type of join
     * @param conditions Join conditions
     */
    JoinExecutor(ExecutorContext *exec_ctx,
                 std::unique_ptr<AbstractExecutor> left_executor,
                 std::unique_ptr<AbstractExecutor> right_executor,
                 JoinType join_type,
                 const std::vector<JoinCondition> &conditions,
                 Transaction *txn = nullptr);

    void Init() override;
    bool Next(Tuple *tuple) override;
    const Schema *GetOutputSchema() override;

private:
    /**
     * Strategy pattern: Different join algorithms for different join types
     */
    bool ExecuteInnerJoin(Tuple *result_tuple);
    bool ExecuteLeftJoin(Tuple *result_tuple);
    bool ExecuteRightJoin(Tuple *result_tuple);
    bool ExecuteFullJoin(Tuple *result_tuple);
    bool ExecuteCrossJoin(Tuple *result_tuple);

    /**
     * Evaluates a join condition between two tuples
     */
    bool EvaluateJoinCondition(const Tuple &left_tuple, const Tuple &right_tuple);

    /**
     * Combines two tuples into one result tuple
     */
    Tuple CombineTuples(const Tuple &left, const Tuple &right);

    // Plan and executors
    std::unique_ptr<AbstractExecutor> left_executor_;
    std::unique_ptr<AbstractExecutor> right_executor_;
    
    // Join metadata
    JoinType join_type_;
    std::vector<JoinCondition> conditions_;
    
    // Schema management
    std::unique_ptr<Schema> output_schema_;
    
    // State management for join operations
    Tuple left_tuple_;
    Tuple right_tuple_;
    bool left_exhausted_ = false;
    bool right_exhausted_ = false;
    
    // Cached data for outer joins
    std::vector<Tuple> left_cache_;
    std::vector<Tuple> right_cache_;
    std::vector<bool> left_matched_;
    std::vector<bool> right_matched_;
    size_t left_index_ = 0;
    size_t right_index_ = 0;

    Transaction *txn_;
};

} // namespace francodb
