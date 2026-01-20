# FrancoDB Bug Fixes Summary

## Bug 1: Shell Auto-Updates DB Name on Failed USE Command ❌ FIXED ✅

### Issue
When using `USE database_name` or `2ESTA5DEM database_name`, the shell prompt would update the database name IMMEDIATELY without checking if the command was successful. If the database didn't exist or the command failed, the prompt would still show the new database name, causing confusion.

### Root Cause
In `src/cmd/shell/shell.cpp`, the database name was extracted and updated before executing the query and checking the result.

### Fix Applied
Modified the logic in `shell.cpp` to:
1. Extract the potential new database name from the command
2. Execute the query first
3. Only update `current_db` if the query result does NOT contain "ERROR" or "FAILED"

**Files Modified:**
- `src/cmd/shell/shell.cpp` - Lines 217-265: Updated shell loop logic

---

## Bug 2: Missing Schema Validation in INSERT and UPDATE ❌ FIXED ✅

### Issue
You could INSERT or UPDATE with:
- Wrong number of columns
- NULL values (empty strings)
- Mismatched data types (e.g., inserting a string into an INTEGER column)

The system would accept invalid data without validation, violating schema constraints.

### Root Cause
- `InsertExecutor::Init()` had only an assert, which doesn't work in release builds
- `UpdateExecutor::Init()` had no validation at all
- No type checking or column count verification

### Fix Applied
Enhanced both executors with proper schema validation:

**INSERT Executor** - Added validation in `Init()`:
1. Check column count matches schema
2. Validate each column type
3. Prevent NULL values (empty strings)
4. Type compatibility checking with conversion support

**UPDATE Executor** - Added validation in `Init()`:
1. Verify target column exists in schema
2. Type checking against column definition
3. NULL value prevention
4. Proper error messages

**Files Modified:**
- `src/execution/executors/insert_executor.cpp` - Lines 12-50: Added Init() validation
- `src/execution/executors/update_executor.cpp` - Lines 12-56: Added Init() validation

---

## Bug 3: Index Scan Returns Nothing When Using Indexed Columns ❌ FIXED ✅

### Issue
When creating an index on a column and then searching using that indexed column in a WHERE clause:
- The index would appear to work (index created successfully)
- But queries using `WHERE column = value` would return NO RESULTS
- Sequential scans worked fine, but index scans returned empty

### Root Causes
1. **GenericKey not handling VARCHAR types**: The `SetFromValue()` method only handled INTEGER and DECIMAL, not VARCHAR. String columns would use uninitialized keys.
2. **Uninitialized GenericKey data**: The `data` field wasn't guaranteed to be zeroed out before use, causing garbage values in key comparisons.
3. **Missing debug output**: Made it impossible to diagnose index scan failures.

### Fix Applied

**Step 1: Enhanced GenericKey** in `src/include/storage/index/index_key.h`:
- Added in-class initializer `= {}` to ensure `data` field is always zeroed
- Extended `SetFromValue()` to handle VARCHAR type
- VARCHAR values are stored as null-terminated strings in the key

**Step 2: Improved IndexScanExecutor** in `src/execution/executors/index_scan_executor.cpp`:
- Added debug logging to identify when index lookups fail
- Better exception handling with error messages
- Logs the number of RIDs returned from B+Tree
- Helps diagnose validation issues

**Files Modified:**
- `src/include/storage/index/index_key.h` - Lines 8-31: Enhanced GenericKey
- `src/execution/executors/index_scan_executor.cpp` - Lines 1-56: Added debug logging

---

## Bug 4: SELECT Returns All Columns Instead of Selected Columns ❌ FIXED ✅

### Issue
When running `SELECT Name FROM table_name`, the query would return ALL columns from the table, not just the `Name` column. Column projection wasn't working.

### Root Cause
In `ExecuteSelect()` in `execution_engine.cpp`:
- The code was iterating through `output_schema->GetColumns()` (ALL columns) instead of respecting `stmt->columns_` (SELECTED columns)
- The result set was populated with all columns regardless of what was in the SELECT clause

### Fix Applied
Rewrote the SELECT result set population logic in `execution_engine.cpp`:

1. **Column Headers**: Check if `select_all_` is true
   - If yes: Add all columns
   - If no: Only add columns from `stmt->columns_`

2. **Column Index Mapping**: Create a `column_indices` vector to map selected column names to actual schema indices

3. **Row Population**: Only extract and return values for the selected columns, not all columns

**Files Modified:**
- `src/execution/execution_engine.cpp` - Lines 140-211: Complete rewrite of ExecuteSelect()

---

## Testing Recommendations

### Bug 1 Test
```sql
USE nonexistent_db;  -- Should show error, prompt unchanged
USE default;         -- Should show success, prompt updates to "default"
```

### Bug 2 Tests
```sql
-- Should fail: wrong column count
INSERT INTO users ELKEYAM (100);

-- Should fail: NULL value (empty string)
INSERT INTO users ELKEYAM ('', 'John');

-- Should fail: type mismatch
INSERT INTO users ELKEYAM ('abc', 'John');

-- Should succeed
INSERT INTO users ELKEYAM (100, 'John');
```

### Bug 3 Tests
```sql
CREATE INDEX idx_id ON users(id);
INSERT INTO users ELKEYAM (100, 'Ahmed');
SELECT * FROM users WHERE id = 100;  -- Should return row (uses index)
```

### Bug 4 Tests
```sql
SELECT Name FROM users;      -- Should show ONLY Name column
SELECT * FROM users;         -- Should show ALL columns
SELECT Name, id FROM users;  -- Should show Name and id only
```

---

## Summary

| Bug | Type | Severity | Status |
|-----|------|----------|--------|
| 1. USE DB prompt updates on error | Logic | High | ✅ FIXED |
| 2. Missing schema validation | Data integrity | High | ✅ FIXED |
| 3. Index scans return nothing | Query execution | Critical | ✅ FIXED |
| 4. SELECT returns all columns | Query projection | High | ✅ FIXED |

All 4 bugs have been successfully identified and fixed!

