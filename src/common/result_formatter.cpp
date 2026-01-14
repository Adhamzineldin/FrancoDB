#include "common/result_formatter.h"
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace francodb {

    std::string ResultFormatter::Format(std::shared_ptr<ResultSet> rs) {
        if (!rs || rs->column_names.empty()) return "(No Data)\n";

        std::ostringstream out;
    
        // 1. Calculate Column Widths
        std::vector<size_t> col_widths;
        for (size_t i = 0; i < rs->column_names.size(); ++i) {
            size_t max_width = rs->column_names[i].length();
            for (const auto &row : rs->rows) {
                if (i < row.size()) max_width = std::max(max_width, row[i].length());
            }
            col_widths.push_back(max_width);
        }

        // 2. Print Header
        out << " ";
        for (size_t i = 0; i < rs->column_names.size(); ++i) {
            out << std::left << std::setw(col_widths[i]) << rs->column_names[i];
            if (i < rs->column_names.size() - 1) out << " | ";
        }
        out << "\n";

        // 3. Print Separator
        out << "-";
        for (size_t i = 0; i < rs->column_names.size(); ++i) {
            out << std::string(col_widths[i], '-');
            if (i < rs->column_names.size() - 1) out << "-+-";
        }
        out << "-\n";

        // 4. Print Rows
        for (const auto &row : rs->rows) {
            out << " ";
            for (size_t i = 0; i < row.size(); ++i) {
                out << std::left << std::setw(col_widths[i]) << row[i];
                if (i < row.size() - 1) out << " | ";
            }
            out << "\n";
        }

        // 5. Footer
        out << "(" << rs->rows.size() << " row" << (rs->rows.size() != 1 ? "s" : "") << ")\n";
    
        return out.str();
    }

} // namespace francodb