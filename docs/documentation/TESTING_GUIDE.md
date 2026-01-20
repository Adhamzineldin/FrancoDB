# FrancoDB Bug Fixes - Testing Guide

## Overview
This document provides comprehensive test cases to verify that all 4 bugs have been fixed.

---

## Bug 1: Shell DB Prompt Update on Failed Command

### Test Case 1.1: Failed USE Command (Database doesn't exist)
**Command:**
```sql
USE nonexistent_database;
```

**Expected Behavior:**
- Shell displays an ERROR message
- Prompt remains: `maayn@default>`
- Database NOT changed to "nonexistent_database"

**Expected Output:**
```
maayn@default> USE nonexistent_database;
[ERROR] Database not found or access denied: nonexistent_database
maayn@default>
```

---

### Test Case 1.2: Successful USE Command
**Command:**
```sql
USE default;
```

**Expected Behavior:**
- Shell displays success message (or no error)
- Prompt updates to: `maayn@default>`
- Database successfully changed

**Expected Output:**
```
maayn@default> USE default;
[SUCCESS] Database selected: default
maayn@default>
```

---

### Test Case 1.3: Alternative Syntax (2ESTA5DEM)
**Command:**
```sql
2ESTA5DEM testdb;
```

**Expected Behavior:**
- Same logic as USE command
- Prompt updates only on success
- Both commands share the same validation logic

**Expected Output:**
```
maayn@default> 2ESTA5DEM testdb;
[SUCCESS] Database selected: testdb
maayn@testdb>
```

---

## Bug 2: Schema Validation in INSERT and UPDATE

### Test Case 2.1: INSERT with Wrong Column Count

**Setup:**
```sql
2E3MEL GADWAL users (id RAKAM, name GOMLA, age RAKAM);
```

**Test Command:**
```sql
EMLA GOWA users ELKEYAM (100, 'Ahmed');
```

**Expected Behavior:**
- Query FAILS with error message
- Error indicates column count mismatch
- Data is NOT inserted

**Expected Output:**
```
[ERROR] Column count mismatch: expected 3 but got 2
```

---

### Test Case 2.2: INSERT with NULL Value

**Setup:**
```sql
2E3MEL GADWAL products (id RAKAM, name GOMLA);
```

**Test Command:**
```sql
EMLA GOWA products ELKEYAM (101, '');
```

**Expected Behavior:**
- Query FAILS with error
- Error indicates NULL not allowed
- Data is NOT inserted

**Expected Output:**
```
[ERROR] NULL values not allowed: column 'name'
```

---

### Test Case 2.3: INSERT with Type Mismatch

**Setup:**
```sql
2E3MEL GADWAL orders (order_id RAKAM, quantity RAKAM);
```

**Test Command:**
```sql
EMLA GOWA orders ELKEYAM ('not_a_number', 5);
```

**Expected Behavior:**
- Query FAILS with type mismatch error
- Data is NOT inserted

**Expected Output:**
```
[ERROR] Type mismatch for column 'order_id': expected INTEGER
```

---

### Test Case 2.4: UPDATE with Non-existent Column

**Setup:**
```sql
2E3MEL GADWAL accounts (account_id RAKAM, balance RAKAM);
EMLA GOWA accounts ELKEYAM (1, 1000);
```

**Test Command:**
```sql
HAGAYYAR accounts SET nonexistent_col = 500 HTTA account_id = 1;
```

**Expected Behavior:**
- Query FAILS with "column not found" error
- Data is NOT updated
- Original row unchanged

**Expected Output:**
```
[ERROR] Column not found: 'nonexistent_col'
```

---

### Test Case 2.5: UPDATE with Type Mismatch

**Setup:**
```sql
2E3MEL GADWAL accounts (account_id RAKAM, balance RAKAM);
EMLA GOWA accounts ELKEYAM (1, 1000);
```

**Test Command:**
```sql
HAGAYYAR accounts SET balance = 'invalid_amount' HTTA account_id = 1;
```

**Expected Behavior:**
- Query FAILS with type error
- Data is NOT updated
- Original row unchanged (balance = 1000)

**Expected Output:**
```
[ERROR] Type mismatch for column 'balance': expected DECIMAL
```

---

## Bug 3: Index Scan Returns Nothing

### Test Case 3.1: Create Index and Query Integer Column

**Setup:**
```sql
2E3MEL GADWAL students (student_id RAKAM, name GOMLA);
2E3MEL FEHRIS idx_student_id 3ALA students (student_id);
EMLA GOWA students ELKEYAM (1001, 'Ahmed');
EMLA GOWA students ELKEYAM (1002, 'Sara');
EMLA GOWA students ELKEYAM (1003, 'Ali');
```

**Test Command:**
```sql
AGDAAT student_id, name MN students HTTA student_id = 1001;
```

**Expected Behavior:**
- Index is used for the lookup
- Query returns the matching row
- Result: [1001, 'Ahmed']
- NO "results not found" error

**Expected Output:**
```
student_id | name
-----------+-------
1001       | Ahmed
```

---

### Test Case 3.2: Index on String Column

**Setup:**
```sql
2E3MEL GADWAL users (user_id RAKAM, username GOMLA);
2E3MEL FEHRIS idx_username 3ALA users (username);
EMLA GOWA users ELKEYAM (1, 'admin');
EMLA GOWA users ELKEYAM (2, 'guest');
EMLA GOWA users ELKEYAM (3, 'moderator');
```

**Test Command:**
```sql
AGDAAT * MN users HTTA username = 'admin';
```

**Expected Behavior:**
- Index lookup succeeds even for string column
- Query returns matching row
- Result: [1, 'admin']

**Expected Output:**
```
user_id | username
--------+----------
1       | admin
```

---

### Test Case 3.3: Index with Non-matching Value

**Setup:**
```sql
2E3MEL GADWAL products (product_id RAKAM, name GOMLA);
2E3MEL FEHRIS idx_product_id 3ALA products (product_id);
EMLA GOWA products ELKEYAM (100, 'Mouse');
EMLA GOWA products ELKEYAM (101, 'Keyboard');
```

**Test Command:**
```sql
AGDAAT * MN products HTTA product_id = 999;
```

**Expected Behavior:**
- Index correctly returns no results
- Query completes successfully
- Returns empty result set (not an error)

**Expected Output:**
```
(No rows returned)
```

---

## Bug 4: SELECT Returns Wrong Columns

### Test Case 4.1: SELECT Specific Column

**Setup:**
```sql
2E3MEL GADWAL employees (emp_id RAKAM, emp_name GOMLA, salary RAKAM);
EMLA GOWA employees ELKEYAM (1, 'John', 5000);
EMLA GOWA employees ELKEYAM (2, 'Jane', 6000);
```

**Test Command:**
```sql
AGDAAT emp_name MN employees;
```

**Expected Behavior:**
- Returns ONLY the emp_name column
- Does NOT return emp_id or salary
- Both rows are returned

**Expected Output:**
```
emp_name
--------
John
Jane
```

**NOT the incorrect output (all columns):**
```
emp_id | emp_name | salary
-------+----------+-------
1      | John     | 5000
2      | Jane     | 6000
```

---

### Test Case 4.2: SELECT Multiple Specific Columns

**Setup:**
```sql
2E3MEL GADWAL cars (car_id RAKAM, brand GOMLA, model GOMLA, year RAKAM);
EMLA GOWA cars ELKEYAM (1, 'Toyota', 'Camry', 2020);
EMLA GOWA cars ELKEYAM (2, 'Honda', 'Civic', 2021);
```

**Test Command:**
```sql
AGDAAT brand, model MN cars;
```

**Expected Behavior:**
- Returns ONLY brand and model columns
- Does NOT return car_id or year
- Columns appear in the order specified

**Expected Output:**
```
brand | model
-------+-------
Toyota | Camry
Honda  | Civic
```

---

### Test Case 4.3: SELECT All Columns (*)

**Setup:**
```sql
2E3MEL GADWAL books (book_id RAKAM, title GOMLA, author GOMLA);
EMLA GOWA books ELKEYAM (1, 'Book A', 'Author X');
EMLA GOWA books ELKEYAM (2, 'Book B', 'Author Y');
```

**Test Command:**
```sql
AGDAAT * MN books;
```

**Expected Behavior:**
- Returns ALL columns (book_id, title, author)
- Both rows returned

**Expected Output:**
```
book_id | title  | author
--------+--------+--------
1       | Book A | Author X
2       | Book B | Author Y
```

---

### Test Case 4.4: SELECT with WHERE and Column Projection

**Setup:**
```sql
2E3MEL GADWAL sales (sale_id RAKAM, product GOMLA, amount RAKAM);
EMLA GOWA sales ELKEYAM (1, 'Widget', 100);
EMLA GOWA sales ELKEYAM (2, 'Gadget', 200);
EMLA GOWA sales ELKEYAM (3, 'Widget', 150);
```

**Test Command:**
```sql
AGDAAT product, amount MN sales HTTA product = 'Widget';
```

**Expected Behavior:**
- Returns ONLY product and amount columns (not sale_id)
- Returns only rows where product = 'Widget'
- Two rows returned

**Expected Output:**
```
product | amount
---------+-------
Widget  | 100
Widget  | 150
```

---

## Quick Test Script

Run these commands in sequence to verify all bugs are fixed:

```sql
-- Setup
2E3MEL GADWAL test_table (id RAKAM, name GOMLA);
2E3MEL FEHRIS idx_id 3ALA test_table (id);

-- Bug 1: Test USE command
USE nonexistent;      -- Should fail, prompt unchanged
USE default;          -- Should succeed, prompt updates

-- Bug 2: Test validation
EMLA GOWA test_table ELKEYAM (1);  -- Should fail (missing column)
EMLA GOWA test_table ELKEYAM (1, '');  -- Should fail (NULL)
EMLA GOWA test_table ELKEYAM ('not_int', 'John');  -- Should fail (type)

-- Bug 3: Test index
EMLA GOWA test_table ELKEYAM (100, 'Alice');
EMLA GOWA test_table ELKEYAM (200, 'Bob');
AGDAAT * MN test_table HTTA id = 100;  -- Should use index, return row

-- Bug 4: Test column projection
AGDAAT name MN test_table;  -- Should return ONLY name column
AGDAAT id, name MN test_table;  -- Should return id and name, not all
```

---

## Summary

| Test # | Bug | Status | Notes |
|--------|-----|--------|-------|
| 1.1-1.3 | Shell prompt | PASS/FAIL | Check prompt only updates on success |
| 2.1-2.5 | Schema validation | PASS/FAIL | Check all validation errors appear |
| 3.1-3.3 | Index scanning | PASS/FAIL | Check index scans return results |
| 4.1-4.4 | Column projection | PASS/FAIL | Check only selected columns returned |

All tests should PASS for the fixes to be considered complete.

