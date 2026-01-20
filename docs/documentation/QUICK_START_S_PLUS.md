# FrancoDB S+ Features - Quick Start Guide

## 🎯 Quick Navigation

- [Nullable Columns](#nullable-columns)
- [JOIN Operations](#join-operations)
- [Foreign Keys](#foreign-keys)
- [GROUP BY & Aggregates](#group-by--aggregates)
- [ORDER BY & LIMIT](#order-by--limit)
- [DISTINCT](#distinct)
- [ALTER TABLE](#alter-table)

---

## Nullable Columns

### Define Nullable Columns

```sql
-- Explicitly nullable with DEFAULT
CREATE TABLE employees (
    emp_id INT PRIMARY KEY,
    emp_name VARCHAR(100) NOT NULL,
    department VARCHAR(50) NULLABLE,
    phone VARCHAR(20) NULLABLE DEFAULT '000-0000',
    address VARCHAR(255) NULLABLE
);
```

### Code Usage

```cpp
Column phone("phone", TypeId::VARCHAR, 20);
phone.SetNullable(true)           // Allow NULL
      .SetDefaultValue(Value(TypeId::VARCHAR, "555-0000"));

Column email("email", TypeId::VARCHAR, 255);
email.SetNullable(false)          // NOT NULL
      .SetUnique(true);
```

### Key Points
- ✅ Primary keys are automatically NOT NULL
- ✅ Default NOT NULL for most columns
- ✅ Use `NULLABLE` keyword for optional columns
- ✅ Pair NULLABLE with DEFAULT for better UX

---

## JOIN Operations

### 1. INNER JOIN

**SQL:**
```sql
SELECT o.order_id, c.customer_name, o.total
FROM orders o
INNER JOIN customers c ON o.customer_id = c.customer_id;
```

**C++ Code:**
```cpp
JoinCondition cond("orders", "customer_id", 
                   "customers", "customer_id", "=");

auto join = std::make_unique<JoinExecutor>(
    exec_ctx,
    std::move(left_executor),
    std::move(right_executor),
    JoinType::INNER,
    std::vector<JoinCondition>{cond}
);
```

**Result:**
```
order_id | customer_name | total
---------|---------------|---------
1001     | Alice         | 150.00
1002     | Bob           | 200.50
```

### 2. LEFT OUTER JOIN

**SQL:**
```sql
SELECT u.user_id, u.name, o.order_id
FROM users u
LEFT JOIN orders o ON u.user_id = o.user_id;
```

**Result:** All users + their orders (NULL if no orders)
```
user_id | name  | order_id
--------|-------|----------
1       | Alice | 1001
1       | Alice | 1005
2       | Bob   | NULL      ← Bob has no orders
3       | Carol | 1003
```

### 3. RIGHT OUTER JOIN

**SQL:**
```sql
SELECT u.user_id, o.order_id, o.total
FROM users u
RIGHT JOIN orders o ON u.user_id = o.user_id;
```

**Result:** All orders + matching users (NULL if orphaned orders)

### 4. FULL OUTER JOIN

**SQL:**
```sql
SELECT u.user_id, u.name, o.order_id
FROM users u
FULL OUTER JOIN orders o ON u.user_id = o.user_id;
```

**Result:** All rows from both tables

### 5. CROSS JOIN

**SQL:**
```sql
SELECT * FROM sizes CROSS JOIN colors;
```

**Result:** All combinations (Cartesian product)
```
size | color
------|-------
S    | Red
S    | Blue
M    | Red
M    | Blue
L    | Red
L    | Blue
```

### Multiple JOINs

```sql
SELECT 
    o.order_id, 
    c.customer_name, 
    p.product_name,
    od.quantity
FROM orders o
INNER JOIN customers c ON o.customer_id = c.customer_id
INNER JOIN order_details od ON o.order_id = od.order_id
INNER JOIN products p ON od.product_id = p.product_id;
```

---

## Foreign Keys

### Define Foreign Keys

```sql
CREATE TABLE orders (
    order_id INT PRIMARY KEY,
    customer_id INT NOT NULL,
    order_date TIMESTAMP,
    
    FOREIGN KEY (customer_id) 
        REFERENCES customers(customer_id)
        ON DELETE CASCADE
        ON UPDATE CASCADE
);
```

### Foreign Key Actions

| Action | Effect |
|--------|--------|
| **CASCADE** | Delete/update related rows |
| **RESTRICT** | Prevent delete/update |
| **SET NULL** | Set FK to NULL |
| **SET DEFAULT** | Set FK to default value |

### Example: ON DELETE CASCADE

```sql
-- Customers table
| customer_id | name  |
|-------------|-------|
| 1           | Alice |
| 2           | Bob   |

-- Orders table (with FK)
| order_id | customer_id |
|----------|-------------|
| 1001     | 1           |
| 1002     | 1           |
| 1003     | 2           |

-- DELETE customers WHERE customer_id = 1;
-- Result: Orders 1001 and 1002 are AUTOMATICALLY deleted
-- Orders with customer_id=2 remain intact
```

### Code Usage

```cpp
ForeignKeyConstraint fk("fk_order_customer");
fk.SetColumns("customer_id", "customer_id")
  .SetReferencedTable("customers")
  .SetOnDelete(ForeignKeyConstraint::Action::CASCADE)
  .SetOnUpdate(ForeignKeyConstraint::Action::CASCADE);
```

---

## GROUP BY & Aggregates

### Basic GROUP BY

```sql
SELECT 
    department,
    COUNT(*) as emp_count,
    AVG(salary) as avg_salary
FROM employees
GROUP BY department;
```

**Result:**
```
department | emp_count | avg_salary
-----------|-----------|------------
IT         | 5         | 75000.00
HR         | 3         | 55000.00
Sales      | 7         | 65000.00
```

### GROUP BY with Multiple Columns

```sql
SELECT 
    department,
    job_title,
    COUNT(*) as emp_count
FROM employees
GROUP BY department, job_title;
```

### GROUP BY with HAVING

```sql
SELECT 
    department,
    COUNT(*) as emp_count,
    AVG(salary) as avg_salary
FROM employees
GROUP BY department
HAVING COUNT(*) > 3 AND AVG(salary) > 60000;
```

### Supported Aggregates

```sql
COUNT(*)           -- Total count
COUNT(column)      -- Non-null count
SUM(column)        -- Sum of values
AVG(column)        -- Average value
MIN(column)        -- Minimum value
MAX(column)        -- Maximum value
```

### Code Example

```cpp
auto groupby = std::make_unique<GroupByExecutor>(
    exec_ctx,
    std::move(seq_scan_executor),
    std::vector<std::string>{"department", "job_title"},
    std::vector<std::string>{"COUNT(*)", "AVG(salary)"}
);
```

---

## ORDER BY & LIMIT

### ORDER BY Single Column

```sql
-- Ascending (default)
SELECT * FROM employees ORDER BY salary;

-- Descending
SELECT * FROM employees ORDER BY salary DESC;
```

### ORDER BY Multiple Columns

```sql
SELECT * FROM employees
ORDER BY department ASC, salary DESC;
```

**Result:**
```
department | name  | salary
-----------|-------|-------
HR         | Carol | 60000
HR         | David | 55000
IT         | Alice | 80000
IT         | Bob   | 75000
```

### LIMIT with OFFSET

```sql
-- Get 10 rows starting from row 1
SELECT * FROM employees LIMIT 10;

-- Get 10 rows starting from row 21 (pagination)
SELECT * FROM employees LIMIT 10 OFFSET 20;

-- Equivalent
SELECT * FROM employees OFFSET 20 LIMIT 10;
```

### Code Example

```cpp
OrderByExecutor::SortColumn col1{"salary", false};      // DESC
OrderByExecutor::SortColumn col2{"name", true};         // ASC

auto order_by = std::make_unique<OrderByExecutor>(
    exec_ctx,
    std::move(seq_scan),
    std::vector<OrderByExecutor::SortColumn>{col1, col2}
);

auto limit = std::make_unique<LimitExecutor>(
    exec_ctx,
    std::move(order_by),
    10,   // limit
    20    // offset
);
```

---

## DISTINCT

### Remove Duplicates

```sql
-- All departments (with duplicates)
SELECT department FROM employees;
-- Result: IT, HR, IT, Sales, IT, HR, ...

-- Unique departments
SELECT DISTINCT department FROM employees;
-- Result: HR, IT, Sales
```

### DISTINCT with Multiple Columns

```sql
SELECT DISTINCT department, job_title FROM employees;
```

### DISTINCT in Complex Query

```sql
SELECT DISTINCT e.department, e.job_title
FROM employees e
INNER JOIN departments d ON e.department = d.dept_name
WHERE e.salary > 50000
ORDER BY department;
```

### Code Example

```cpp
auto distinct = std::make_unique<DistinctExecutor>(
    exec_ctx,
    std::move(seq_scan_executor)
);
```

---

## ALTER TABLE

### Add Column

```sql
ALTER TABLE users ADD COLUMN last_login TIMESTAMP NULLABLE;
ALTER TABLE users ADD COLUMN age INT NULLABLE DEFAULT 0;
```

### Drop Column

```sql
ALTER TABLE users DROP COLUMN last_login;
```

### Rename Table

```sql
ALTER TABLE employees RENAME TO staff;
```

### Rename Column

```sql
ALTER TABLE users RENAME COLUMN phone_number TO phone;
```

### Add Foreign Key

```sql
ALTER TABLE orders 
ADD FOREIGN KEY (customer_id) 
REFERENCES customers(customer_id)
ON DELETE CASCADE;
```

### Drop Foreign Key

```sql
ALTER TABLE orders DROP FOREIGN KEY fk_order_customer;
```

---

## Complex Query Example

### Full Query with All Features

```sql
SELECT DISTINCT 
    c.customer_name,
    COUNT(o.order_id) as order_count,
    SUM(o.total) as total_spent
FROM customers c
LEFT JOIN orders o ON c.customer_id = o.customer_id
WHERE c.registration_date > '2024-01-01'
GROUP BY c.customer_id, c.customer_name
HAVING COUNT(o.order_id) > 2
ORDER BY total_spent DESC
LIMIT 10;
```

### Execution Pipeline

```
1. SeqScan customers           ← Load customers table
2. Join orders                 ← LEFT JOIN on customer_id
3. Filter                      ← WHERE registration_date > '2024-01-01'
4. GroupBy                     ← GROUP BY customer, aggregate
5. Having                      ← HAVING COUNT > 2
6. OrderBy                     ← ORDER BY total_spent DESC
7. Limit                       ← LIMIT 10
8. Distinct                    ← SELECT DISTINCT
9. Result Set                  ← Return to client
```

---

## Performance Tips

### 1. Use Indexes for JOINs

```sql
-- Create indexes on join columns
CREATE INDEX idx_orders_customer ON orders(customer_id);
CREATE INDEX idx_customers_id ON customers(customer_id);

-- Now your JOINs will be much faster
```

### 2. Filter Before JOIN

```sql
-- GOOD: Filter first
SELECT * FROM orders o
INNER JOIN customers c ON o.customer_id = c.customer_id
WHERE o.order_date > '2024-01-01';

-- LESS GOOD: Join all, then filter
SELECT * FROM orders o
INNER JOIN customers c
WHERE o.order_date > '2024-01-01';
```

### 3. Use Appropriate Data Types

```sql
-- GOOD: INT for ID (small)
customer_id INT

-- BAD: VARCHAR for ID (inefficient)
customer_id VARCHAR(100)
```

### 4. Set NOT NULL Where Possible

```sql
-- Better: NOT NULL allows optimization
customer_id INT NOT NULL

-- Worse: NULLABLE adds overhead
customer_id INT NULLABLE
```

---

## Common Errors & Solutions

### Error: Foreign Key Violation

```
ERROR: Foreign key constraint violation: No matching row in customers
```

**Solution:** Ensure referenced row exists
```sql
-- Before inserting
INSERT INTO orders (customer_id, order_date, total) 
VALUES (999, NOW(), 100.00);  -- ERROR: customer 999 doesn't exist

-- Do this instead
INSERT INTO orders (customer_id, order_date, total) 
VALUES (1, NOW(), 100.00);    -- customer 1 exists
```

### Error: NOT NULL Violation

```
ERROR: NULL values not allowed: column 'email'
```

**Solution:** Provide value for NOT NULL column
```sql
-- Wrong
INSERT INTO users (name, email) VALUES ('John', NULL);

-- Right
INSERT INTO users (name, email) VALUES ('John', 'john@example.com');
```

### Error: Ambiguous Column in JOIN

```
ERROR: Ambiguous column: 'id'
```

**Solution:** Qualify column with table alias
```sql
-- Wrong
SELECT id FROM users u JOIN orders o ON ...

-- Right
SELECT u.user_id, o.order_id FROM users u JOIN orders o ON ...
```

---

## 🎓 Quick Reference

| Feature | SQL | Time |  Space |
|---------|-----|------|--------|
| INNER JOIN | `... INNER JOIN ...` | O(n*m) | O(1) |
| LEFT JOIN | `... LEFT JOIN ...` | O(n*m) | O(max) |
| GROUP BY | `GROUP BY col` | O(n) | O(groups) |
| ORDER BY | `ORDER BY col` | O(n log n) | O(n) |
| LIMIT/OFFSET | `LIMIT 10 OFFSET 5` | O(offset+limit) | O(1) |
| DISTINCT | `SELECT DISTINCT` | O(n) | O(unique) |

---

**Ready to build S+ grade queries!** 🌟

