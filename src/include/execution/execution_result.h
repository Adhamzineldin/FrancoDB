#pragma once
#include <vector>
#include <string>
#include <memory>

namespace chronosdb {

    // Holds the data for a SELECT query
    struct ResultSet {
        std::vector<std::string> column_names;
        std::vector<std::vector<std::string>> rows;
        
        void AddRow(const std::vector<std::string>& row) {
            rows.push_back(row);
        }
    };

    // The universal response from the Engine
    struct ExecutionResult {
        bool success = true;
        std::string message;            
        std::shared_ptr<ResultSet> result_set = nullptr;
        size_t rows_affected = 0;  // Number of rows affected by INSERT/UPDATE/DELETE

        // Helper Constructors
        static ExecutionResult Message(std::string msg) {
            ExecutionResult res;
            res.message = std::move(msg);
            return res;
        }

        static ExecutionResult Error(std::string error_msg) {
            ExecutionResult res;
            res.success = false;
            res.message = std::move(error_msg);
            return res;
        }
        
        static ExecutionResult Data(std::shared_ptr<ResultSet> rs) {
            ExecutionResult res;
            res.result_set = rs;
            return res;
        }

        static ExecutionResult Affected(size_t count, std::string msg = "") {
            ExecutionResult res;
            res.rows_affected = count;
            res.message = std::move(msg);
            return res;
        }
    };

} // namespace chronosdb