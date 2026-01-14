# FrancoDB File Structure Explained

## Overview

FrancoDB uses a **two-file system** for each database:
1. **`.francodb`** - The main data file (stores actual table data)
2. **`.francodb.meta`** - The metadata file (stores schema information)

Additionally, there's a special **system database** for authentication:
- **`system.francodb`** - System database (stores user accounts)
- **`system.francodb.meta`** - System metadata (stores system catalog)

---

## 1. `francodb.db.francodb` (Main Database File)

### Purpose
This is the **main data storage file** where all your table data (rows) is physically stored.

### Structure

#### **Page 0: Magic Header**
- Contains the string `"FRANCODB"` (8 bytes)
- Used to verify the file is a valid FrancoDB database
- Prevents corruption from opening wrong files

#### **Page 1: Reserved**
- Reserved for future use

#### **Page 2: Free Page Bitmap**
- Tracks which pages are used/free
- Binary bitmap: `1` = page is used, `0` = page is free
- First 3 bits: `00000111` (0x07) marks pages 0, 1, 2 as used
- Used for page allocation and recycling

#### **Page 3+ : Table Data**
- Each page is **4KB (4096 bytes)**
- Contains actual table rows (tuples)
- Pages are managed by the Buffer Pool Manager
- Data can be encrypted if encryption is enabled

### What Gets Stored Here?
- ✅ All table rows (INSERT data)
- ✅ Index data (B+ tree pages)
- ✅ Page allocation metadata
- ❌ NOT schema information (that's in `.meta`)

### Example
If you create a table `users` and insert rows:
```sql
CREATE TABLE users (id INTEGER, name VARCHAR);
INSERT INTO users VALUES (1, 'Alice');
INSERT INTO users VALUES (2, 'Bob');
```
The actual data `(1, 'Alice')` and `(2, 'Bob')` are stored in `francodb.db.francodb`.

---

## 2. `francodb.db.francodb.meta` (Metadata File)

### Purpose
Stores **schema information** (table structure, column definitions, indexes) - NOT the actual data.

### Structure

#### **Header: Magic String**
- First 11 bytes: `"FRANCO_META"` (magic header)
- Used to verify the file is valid metadata

#### **Size Field**
- Next 8 bytes: Size of the metadata content

#### **Content: Serialized Catalog**
The actual metadata is stored as text (can be encrypted):
```
TABLE users 3 1 2 id 1 1 name 4 0
INDEX idx_users_id users id 5
```

**Format:**
- `TABLE <name> <first_page_id> <oid> <num_columns> <col1_name> <col1_type> <col1_pk> ...`
- `INDEX <name> <table_name> <column_name> <root_page_id>`

### What Gets Stored Here?
- ✅ Table names
- ✅ Column definitions (name, type, primary key)
- ✅ Index definitions
- ✅ Table OIDs (Object IDs)
- ✅ First page ID for each table
- ❌ NOT actual row data (that's in `.francodb`)

### Example
For the `users` table above, the `.meta` file contains:
```
TABLE users 3 1 2 id 1 1 name 4 0
```
This tells FrancoDB:
- Table name: `users`
- Starts at page 3
- Has OID 1
- Has 2 columns: `id` (INTEGER, primary key) and `name` (VARCHAR, not primary key)

---

## 3. `system.francodb` (System Database)

### Purpose
Special database that stores **authentication and user management data**.

### Structure
Same structure as regular `.francodb` files:
- Page 0: Magic header `"FRANCODB"`
- Page 2: Free page bitmap
- Page 3+: System tables

### What Gets Stored Here?
- ✅ **`franco_users` table** - Contains all user accounts
  - Columns: `username`, `password_hash`, `db_name`, `role`
  - Example row: `('maayn', 'hashed_password', 'default', 'SUPERADMIN')`

### Example Data
```
username | password_hash                    | db_name | role
---------|----------------------------------|---------|----------
maayn    | a1b2c3d4e5f6... (hashed)        | default | SUPERADMIN
adham    | x9y8z7w6v5u4... (hashed)        | test    | ADMIN
john     | m1n2o3p4q5r6... (hashed)        | test    | READONLY
```

---

## 4. `system.francodb.meta` (System Metadata)

### Purpose
Stores the **schema of system tables** (like `franco_users`).

### Structure
Same as regular `.meta` files, but contains:
```
TABLE franco_users 3 1 4 username 4 1 password_hash 4 0 db_name 4 0 role 4 0
```

This defines the `franco_users` table structure.

---

## File Relationships

```
francodb.db.francodb          ← Your actual data (rows)
    ↓
francodb.db.francodb.meta     ← Schema info (how to read the data)

system.francodb                ← User accounts (authentication)
    ↓
system.francodb.meta          ← System schema (how to read users)
```

---

## Why Two Files?

### Separation of Concerns
1. **`.francodb`** = **Data** (can be large, frequently updated)
2. **`.meta`** = **Schema** (small, rarely changes)

### Benefits
- ✅ **Fast schema loading** - Don't need to scan all pages to know table structure
- ✅ **Easy schema changes** - Update metadata without touching data
- ✅ **Corruption isolation** - If data file corrupts, schema is safe
- ✅ **Encryption** - Can encrypt metadata separately

---

## File Locations

By default, files are stored in:
```
data/
├── francodb.db.francodb          (default database)
├── francodb.db.francodb.meta
├── system.francodb                (system database)
└── system.francodb.meta
```

You can change this in `francodb.conf`:
```ini
data_directory = ./data
```

---

## Encryption

If encryption is enabled in `francodb.conf`:
- **`.francodb`** pages are encrypted (except page 0)
- **`.meta`** files are encrypted (except magic header)

Both use XOR encryption with a key from the config file.

---

## Summary Table

| File | Contains | Size | Updates |
|------|----------|------|---------|
| `*.francodb` | Table rows, indexes | Large (grows with data) | Frequently (on INSERT/UPDATE/DELETE) |
| `*.francodb.meta` | Table schemas, indexes | Small (few KB) | Rarely (on CREATE/ALTER TABLE) |
| `system.francodb` | User accounts | Small-Medium | On user management |
| `system.francodb.meta` | System table schemas | Very small | Rarely |

---

## Example: Creating a Database

When you run:
```sql
CREATE DATABASE mydb;
```

FrancoDB creates:
1. `mydb.francodb` - Empty data file (just magic header + bitmap)
2. `mydb.francodb.meta` - Empty metadata file (just magic header)

When you create a table:
```sql
USE mydb;
CREATE TABLE products (id INTEGER, name VARCHAR);
```

FrancoDB:
1. Updates `mydb.francodb.meta` with table schema
2. Allocates pages in `mydb.francodb` for future data

When you insert data:
```sql
INSERT INTO products VALUES (1, 'Laptop');
```

FrancoDB:
1. Writes the row `(1, 'Laptop')` to a page in `mydb.francodb`
2. Does NOT update `.meta` (schema unchanged)

---

## Recovery & Persistence

- **Auto-save**: Every 30 seconds, all data is flushed to disk
- **Crash handler**: On shutdown (Ctrl+C), all data is saved
- **Buffer Pool**: Pages are cached in memory, then written to `.francodb`
- **Catalog Save**: Schema changes are written to `.meta` immediately

---

## Important Notes

⚠️ **Don't manually edit these files** - They're binary/formatted and can corrupt your database!

⚠️ **Always keep `.francodb` and `.meta` together** - They're a pair!

⚠️ **Backup both files** - You need both to restore a database!
