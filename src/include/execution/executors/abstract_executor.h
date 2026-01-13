#pragma once

#include "storage/table/tuple.h"
#include "execution/executor_context.h"
#include "storage/table/schema.h"

namespace francodb {

    class AbstractExecutor {
    public:
        explicit AbstractExecutor(ExecutorContext *exec_ctx) : exec_ctx_(exec_ctx) {}
    
        virtual ~AbstractExecutor() = default;

        /**
         * Setup the executor (e.g., open the file, go to index 0).
         */
        virtual void Init() = 0;

        /**
         * Get the next tuple.
         * @param[out] tuple The result tuple
         * @return true if successful, false if no more data
         */
        virtual bool Next(Tuple *tuple) = 0;

        /** @return The schema of the output tuples */
        virtual const Schema *GetOutputSchema() = 0;

    protected:
        ExecutorContext *exec_ctx_;
    };

} // namespace francodb