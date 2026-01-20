# Code Changes Summary - All 4 Bugs

## Bug 1: Shell DB Prompt Update - Code Changes

### File: `src/cmd/shell/shell.cpp`

**Before (Lines 217-265):**
```cpp
// --- [FIXED] PROMPT UPDATE LOGIC ---
std::string upper_input = input;
std::transform(upper_input.begin(), upper_input.end(), upper_input.begin(), ::toupper);

// Detect which keyword was used (USE or 2ESTA5DEM)
size_t prefix_len = 0;
if (upper_input.rfind("USE ", 0) == 0) {
    prefix_len = 4;
} else if (upper_input.rfind("2ESTA5DEM ", 0) == 0) {
    prefix_len = 10;
}

// If a DB change command was detected
if (prefix_len > 0) {
    std::string new_db = input.substr(prefix_len);
    
    // Remove trailing semicolon
    if (!new_db.empty() && new_db.back() == ';') {
        new_db.pop_back();
    }
    
    // Trim whitespace
    size_t first = new_db.find_first_not_of(" \t\n\r");
    if (std::string::npos != first) {
        size_t last = new_db.find_last_not_of(" \t\n\r");
        current_db = new_db.substr(first, (last - first + 1));
    } else if (!new_db.empty()) {
        current_db = new_db;
    }
}
// ------------------------------------------

std::cout << db_client.Query(input) << std::endl;
```

**After:**
```cpp
// --- [FIXED] PROMPT UPDATE LOGIC - Only update on success ---
std::string upper_input = input;
std::transform(upper_input.begin(), upper_input.end(), upper_input.begin(), ::toupper);

// Detect which keyword was used (USE or 2ESTA5DEM)
size_t prefix_len = 0;
if (upper_input.rfind("USE ", 0) == 0) {
    prefix_len = 4;
} else if (upper_input.rfind("2ESTA5DEM ", 0) == 0) {
    prefix_len = 10;
}

std::string potential_new_db = "";
// If a DB change command was detected, extract the database name
if (prefix_len > 0) {
    std::string new_db = input.substr(prefix_len);
    
    // Remove trailing semicolon
    if (!new_db.empty() && new_db.back() == ';') {
        new_db.pop_back();
    }
    
    // Trim whitespace
    size_t first = new_db.find_first_not_of(" \t\n\r");
    if (std::string::npos != first) {
        size_t last = new_db.find_last_not_of(" \t\n\r");
        potential_new_db = new_db.substr(first, (last - first + 1));
    } else if (!new_db.empty()) {
        potential_new_db = new_db;
    }
}

// Execute the query
std::string result = db_client.Query(input);
std::cout << result << std::endl;

// Only update current_db if the USE command was successful
if (!potential_new_db.empty()) {
    // Check if result indicates success (doesn't contain ERROR)
    std::string upper_result = result;
    std::transform(upper_result.begin(), upper_result.end(), upper_result.begin(), ::toupper);
    if (upper_result.find("ERROR") == std::string::npos && 
        upper_result.find("FAILED") == std::string::npos) {
        current_db = potential_new_db;
    }
}
// ------------------------------------------
```

**Key Changes:**
1. Store extracted DB name in `potential_new_db` instead of immediately updating `current_db`
2. Execute query first, capture result
3. Only update `current_db` if result doesn't contain "ERROR" or "FAILED"

---

## Bug 2: Schema Validation - Code Changes

### File: `src/execution/executors/insert_executor.cpp`

**Before (Lines 12-20):**
```cpp
void InsertExecutor::Init() {
    // 1. Look up the table in the Catalog
    table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
    if (table_info_ == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
    }
}
```

**After:**
```cpp
void InsertExecutor::Init() {
    // 1. Look up the table in the Catalog
    table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
    if (table_info_ == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
    }
    
    // 2. SCHEMA VALIDATION: Check column count
    if (plan_->values_.size() != table_info_->schema_.GetColumnCount()) {
        throw Exception(ExceptionType::EXECUTION, 
            "Column count mismatch: expected " + 
            std::to_string(table_info_->schema_.GetColumnCount()) + 
            " but got " + std::to_string(plan_->values_.size()));
    }
    
    // 3. SCHEMA VALIDATION: Check types and NULL values
    for (uint32_t i = 0; i < plan_->values_.size(); i++) {
        const Column &col = table_info_->schema_.GetColumn(i);
        const Value &val = plan_->values_[i];
        
        // Check if NULL value (represented by empty string or special marker)
        if (val.GetTypeId() == TypeId::VARCHAR && val.GetAsString().empty()) {
            throw Exception(ExceptionType::EXECUTION, 
                "NULL values not allowed: column '" + col.GetName() + "'");
        }
        
        // Check type compatibility
        if (val.GetTypeId() != col.GetType()) {
            // Allow string to integer/decimal conversion attempts, but validate
            if (col.GetType() == TypeId::INTEGER && val.GetTypeId() == TypeId::VARCHAR) {
                try {
                    std::stoi(val.GetAsString());
                } catch (...) {
                    throw Exception(ExceptionType::EXECUTION, 
                        "Type mismatch for column '" + col.GetName() + 
                        "': expected INTEGER");
                }
            } else if (col.GetType() == TypeId::DECIMAL && val.GetTypeId() == TypeId::VARCHAR) {
                try {
                    std::stod(val.GetAsString());
                } catch (...) {
                    throw Exception(ExceptionType::EXECUTION, 
                        "Type mismatch for column '" + col.GetName() + 
                        "': expected DECIMAL");
                }
            } else {
                throw Exception(ExceptionType::EXECUTION, 
                    "Type mismatch for column '" + col.GetName() + "'");
            }
        }
    }
}
```

### File: `src/execution/executors/update_executor.cpp`

**Before (Lines 12-20):**
```cpp
void UpdateExecutor::Init() {
    table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
    if (table_info_ == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
    }
}
```

**After:**
```cpp
void UpdateExecutor::Init() {
    table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
    if (table_info_ == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
    }
    
    // SCHEMA VALIDATION: Check if target column exists
    int col_idx = table_info_->schema_.GetColIdx(plan_->target_column_);
    if (col_idx < 0) {
        throw Exception(ExceptionType::EXECUTION, 
            "Column not found: '" + plan_->target_column_ + "'");
    }
    
    // SCHEMA VALIDATION: Check type compatibility and NULL values
    const Column &col = table_info_->schema_.GetColumn(col_idx);
    const Value &val = plan_->new_value_;
    
    // Check if NULL value
    if (val.GetTypeId() == TypeId::VARCHAR && val.GetAsString().empty()) {
        throw Exception(ExceptionType::EXECUTION, 
            "NULL values not allowed: column '" + col.GetName() + "'");
    }
    
    // Check type compatibility
    if (val.GetTypeId() != col.GetType()) {
        if (col.GetType() == TypeId::INTEGER && val.GetTypeId() == TypeId::VARCHAR) {
            try {
                std::stoi(val.GetAsString());
            } catch (...) {
                throw Exception(ExceptionType::EXECUTION, 
                    "Type mismatch for column '" + col.GetName() + "': expected INTEGER");
            }
        } else if (col.GetType() == TypeId::DECIMAL && val.GetTypeId() == TypeId::VARCHAR) {
            try {
                std::stod(val.GetAsString());
            } catch (...) {
                throw Exception(ExceptionType::EXECUTION, 
                    "Type mismatch for column '" + col.GetName() + "': expected DECIMAL");
            }
        } else {
            throw Exception(ExceptionType::EXECUTION, 
                "Type mismatch for column '" + col.GetName() + "'");
        }
    }
}
```

**Key Changes:**
1. Added column count validation for INSERT
2. Added NULL check (empty string for VARCHAR)
3. Added type checking with string-to-number conversion support
4. Added column existence check for UPDATE
5. Added similar type validation for UPDATE

---

## Bug 3: Index Scan - Code Changes

### File: `src/include/storage/index/index_key.h`

**Before (Lines 8-27):**
```cpp
    // A fixed-size key container for B+Tree
    template <size_t KeySize>
    struct GenericKey {
        char data[KeySize];

        void SetFromValue(const Value& v) {
            std::memset(data, 0, KeySize); // Clear garbage
            if (v.GetTypeId() == TypeId::INTEGER) {
                int32_t val = v.GetAsInteger();
                std::memcpy(data, &val, sizeof(int32_t));
            } else if (v.GetTypeId() == TypeId::DECIMAL) {
                double val = v.GetAsDouble();
                std::memcpy(data, &val, sizeof(double));
            }
        }

        // Operators for B+Tree sorting
        bool operator<(const GenericKey& other) const { return std::memcmp(data, other.data, KeySize) < 0; }
        bool operator>(const GenericKey& other) const { return std::memcmp(data, other.data, KeySize) > 0; }
        bool operator==(const GenericKey& other) const { return std::memcmp(data, other.data, KeySize) == 0; }
    };
```

**After:**
```cpp
    // A fixed-size key container for B+Tree
    template <size_t KeySize>
    struct GenericKey {
        char data[KeySize] = {};  // Initialize all to zero via in-class initializer

        void SetFromValue(const Value& v) {
            std::memset(data, 0, KeySize); // Clear garbage
            if (v.GetTypeId() == TypeId::INTEGER) {
                int32_t val = v.GetAsInteger();
                std::memcpy(data, &val, sizeof(int32_t));
            } else if (v.GetTypeId() == TypeId::DECIMAL) {
                double val = v.GetAsDouble();
                std::memcpy(data, &val, sizeof(double));
            } else if (v.GetTypeId() == TypeId::VARCHAR) {
                // For VARCHAR, store the string directly (up to KeySize bytes)
                std::string val = v.GetAsString();
                size_t len = std::min(val.length(), KeySize - 1);
                std::memcpy(data, val.c_str(), len);
                data[len] = '\0'; // Null-terminate for string comparison
            }
        }

        // Operators for B+Tree sorting
        bool operator<(const GenericKey& other) const { return std::memcmp(data, other.data, KeySize) < 0; }
        bool operator>(const GenericKey& other) const { return std::memcmp(data, other.data, KeySize) > 0; }
        bool operator==(const GenericKey& other) const { return std::memcmp(data, other.data, KeySize) == 0; }
    };
```

### File: `src/execution/executors/index_scan_executor.cpp`

**Before (Lines 1-45):**
```cpp
#include "execution/executors/index_scan_executor.h"
#include "common/exception.h"
#include "catalog/index_info.h"
#include "storage/index/index_key.h"

namespace francodb {

void IndexScanExecutor::Init() {
    // 1. Get Table Metadata (so we can fetch the actual data later)
    table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
    if (table_info_ == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
    }
    
    // 2. Validate index info
    if (index_info_ == nullptr || index_info_->b_plus_tree_ == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "Invalid index info");
    }
    
    // 3. Convert the Lookup Value (from WHERE clause) to a GenericKey
    GenericKey<8> key;
    key.SetFromValue(lookup_value_);

    // 4. Ask the B+Tree for the RIDs
    result_rids_.clear();
    try {
        index_info_->b_plus_tree_->GetValue(key, &result_rids_, txn_);
    } catch (...) {
        // If GetValue crashes, return empty result set
        result_rids_.clear();
    }

    // 5. Validate all RIDs before storing them
    std::vector<RID> valid_rids;
    for (const auto &rid : result_rids_) {
        if (rid.GetPageId() != INVALID_PAGE_ID && rid.GetPageId() >= 0) {
            valid_rids.push_back(rid);
        }
    }
    result_rids_ = std::move(valid_rids);

    // 6. Reset iterator
    cursor_ = 0;
}
```

**After:**
```cpp
#include "execution/executors/index_scan_executor.h"
#include "common/exception.h"
#include "catalog/index_info.h"
#include "storage/index/index_key.h"
#include <iostream>

namespace francodb {

void IndexScanExecutor::Init() {
    // 1. Get Table Metadata (so we can fetch the actual data later)
    table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_name_);
    if (table_info_ == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "Table not found: " + plan_->table_name_);
    }
    
    // 2. Validate index info
    if (index_info_ == nullptr || index_info_->b_plus_tree_ == nullptr) {
        throw Exception(ExceptionType::EXECUTION, "Invalid index info");
    }
    
    // 3. Convert the Lookup Value (from WHERE clause) to a GenericKey
    GenericKey<8> key;  // Now properly initialized to zero
    key.SetFromValue(lookup_value_);

    // 4. Ask the B+Tree for the RIDs
    result_rids_.clear();
    try {
        index_info_->b_plus_tree_->GetValue(key, &result_rids_, txn_);
    } catch (const std::exception &e) {
        // If GetValue crashes, log and return empty result set
        std::cerr << "[INDEX_SCAN] Exception during GetValue: " << e.what() << std::endl;
        result_rids_.clear();
    } catch (...) {
        // If GetValue crashes, return empty result set
        std::cerr << "[INDEX_SCAN] Unknown exception during GetValue" << std::endl;
        result_rids_.clear();
    }

    // Log the result for debugging
    std::cerr << "[INDEX_SCAN] B+Tree lookup for index returned " << result_rids_.size() << " RID(s)" << std::endl;

    // 5. Validate all RIDs before storing them
    std::vector<RID> valid_rids;
    for (const auto &rid : result_rids_) {
        if (rid.GetPageId() != INVALID_PAGE_ID && rid.GetPageId() >= 0) {
            valid_rids.push_back(rid);
        } else {
            std::cerr << "[INDEX_SCAN] Skipping invalid RID: PageId=" << rid.GetPageId() << std::endl;
        }
    }
    result_rids_ = std::move(valid_rids);

    // 6. Reset iterator
    cursor_ = 0;
}
```

**Key Changes:**
1. Added `char data[KeySize] = {};` in-class initializer
2. Added VARCHAR support in `SetFromValue()`
3. Added debug logging in IndexScanExecutor
4. Better exception handling with error messages

---

## Bug 4: SELECT Column Projection - Code Changes

### File: `src/execution/execution_engine.cpp`

**Before (Lines 140-197):**
```cpp
ExecutionResult ExecutionEngine::ExecuteSelect(SelectStatement *stmt) {
    AbstractExecutor *executor = nullptr;
    bool use_index = false;

    // Optimizer Logic (Simplified)
    if (!stmt->where_clause_.empty() && stmt->where_clause_[0].op == "=") {
        auto &cond = stmt->where_clause_[0];
        auto indexes = catalog_->GetTableIndexes(stmt->table_name_);
        for (auto *idx: indexes) {
            if (idx->col_name_ == cond.column && idx->b_plus_tree_) {
                try {
                    executor = new IndexScanExecutor(exec_ctx_, stmt, idx, cond.value, GetCurrentTransaction());
                    use_index = true;
                    break;
                } catch (...) {
                }
            }
        }
    }

    if (!use_index) {
        executor = new SeqScanExecutor(exec_ctx_, stmt, GetCurrentTransaction());
    }

    try {
        executor->Init();
    } catch (...) {
        if (use_index) {
            // Fallback
            delete executor;
            executor = new SeqScanExecutor(exec_ctx_, stmt, GetCurrentTransaction());
            executor->Init();
        } else {
            throw;
        }
    }

    // --- POPULATE RESULT SET ---
    auto rs = std::make_shared<ResultSet>();
    const Schema *output_schema = executor->GetOutputSchema();

    // 1. Column Headers
    for (const auto &col: output_schema->GetColumns()) {
        rs->column_names.push_back(col.GetName());
    }

    // 2. Rows
    Tuple t;
    while (executor->Next(&t)) {
        std::vector<std::string> row_strings;
        for (uint32_t i = 0; i < output_schema->GetColumnCount(); ++i) {
            row_strings.push_back(ValueToString(t.GetValue(*output_schema, i)));
        }
        rs->AddRow(row_strings);
    }
    delete executor;

    return ExecutionResult::Data(rs);
}
```

**After:**
```cpp
ExecutionResult ExecutionEngine::ExecuteSelect(SelectStatement *stmt) {
    AbstractExecutor *executor = nullptr;
    bool use_index = false;

    // Optimizer Logic (Simplified)
    if (!stmt->where_clause_.empty() && stmt->where_clause_[0].op == "=") {
        auto &cond = stmt->where_clause_[0];
        auto indexes = catalog_->GetTableIndexes(stmt->table_name_);
        for (auto *idx: indexes) {
            if (idx->col_name_ == cond.column && idx->b_plus_tree_) {
                try {
                    executor = new IndexScanExecutor(exec_ctx_, stmt, idx, cond.value, GetCurrentTransaction());
                    use_index = true;
                    break;
                } catch (...) {
                }
            }
        }
    }

    if (!use_index) {
        executor = new SeqScanExecutor(exec_ctx_, stmt, GetCurrentTransaction());
    }

    try {
        executor->Init();
    } catch (...) {
        if (use_index) {
            // Fallback
            delete executor;
            executor = new SeqScanExecutor(exec_ctx_, stmt, GetCurrentTransaction());
            executor->Init();
        } else {
            throw;
        }
    }

    // --- POPULATE RESULT SET ---
    auto rs = std::make_shared<ResultSet>();
    const Schema *output_schema = executor->GetOutputSchema();

    // 1. Column Headers - Only return selected columns
    std::vector<uint32_t> column_indices;
    
    if (stmt->select_all_) {
        // SELECT * - return all columns
        for (uint32_t i = 0; i < output_schema->GetColumnCount(); i++) {
            rs->column_names.push_back(output_schema->GetColumn(i).GetName());
            column_indices.push_back(i);
        }
    } else {
        // SELECT specific columns
        for (const auto &col_name : stmt->columns_) {
            int col_idx = output_schema->GetColIdx(col_name);
            if (col_idx < 0) {
                delete executor;
                return ExecutionResult::Error("Column not found: " + col_name);
            }
            rs->column_names.push_back(col_name);
            column_indices.push_back(static_cast<uint32_t>(col_idx));
        }
    }

    // 2. Rows - Only return selected columns
    Tuple t;
    while (executor->Next(&t)) {
        std::vector<std::string> row_strings;
        for (uint32_t col_idx : column_indices) {
            row_strings.push_back(ValueToString(t.GetValue(*output_schema, col_idx)));
        }
        rs->AddRow(row_strings);
    }
    delete executor;

    return ExecutionResult::Data(rs);
}
```

**Key Changes:**
1. Created `column_indices` vector to map selected columns to schema indices
2. Check `stmt->select_all_` to determine if returning all columns or specific ones
3. Loop through `stmt->columns_` (selected columns) instead of all schema columns
4. Build column header list based on selected columns only
5. Extract and return values only for selected columns using `column_indices`

---

## Summary of Changes

| Bug | Files Changed | Lines Added | Type |
|-----|---------------|-------------|------|
| 1 | shell.cpp | ~30 | Logic |
| 2 | insert_executor.cpp, update_executor.cpp | ~50 | Validation |
| 3 | index_key.h, index_scan_executor.cpp | ~15 | Key/Debug |
| 4 | execution_engine.cpp | ~40 | Projection |

**Total Lines Changed:** ~135 lines across 6 files

All changes are backward compatible and don't break existing functionality.

