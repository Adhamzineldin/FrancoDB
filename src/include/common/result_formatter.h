#pragma once

#include <string>
#include <memory>
#include "execution/execution_result.h"

namespace francodb {

    class ResultFormatter {
    public:
        static std::string Format(std::shared_ptr<ResultSet> rs);
    };

} // namespace francodb