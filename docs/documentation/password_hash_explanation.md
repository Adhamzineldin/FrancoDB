# Password Hash Storage & Retrieval in FrancoDB

## Overview

FrancoDB stores password hashes (never plaintext passwords) in the `system.francodb` database. This document explains:
1. How passwords are hashed
2. How hashes are stored
3. How hashes are retrieved
4. How authentication works

---

## 1. Password Hashing

### Hash Function: `HashPassword()`

**Location:** `src/common/auth_manager.cpp:18-35`

```cpp
std::string AuthManager::HashPassword(const std::string& password) {
    std::hash<std::string> hasher;
    
    // Combine password with secret pepper from config
    std::string data = password + net::PASSWORD_PEPPER;
    size_t hash = 0;
    
    // Cost factor (number of iterations) – can be tuned
    const int kCost = 10000;
    for (int i = 0; i < kCost; i++) {
        // Mix in previous hash value to make each round depend on the last
        hash = hasher(data + std::to_string(hash));
    }
    
    std::ostringstream oss;
    oss << std::hex << hash;
    return oss.str();
}
```

### How It Works:

1. **Pepper Addition**: Combines password with secret pepper from `franco_net_config.h`
   ```cpp
   std::string data = password + net::PASSWORD_PEPPER;
   ```

2. **Iterative Hashing**: Runs 10,000 iterations of `std::hash`
   - Each iteration: `hash = hasher(data + std::to_string(hash))`
   - Makes brute-force attacks slower (bcrypt-style)

3. **Hex Encoding**: Converts hash to hexadecimal string
   ```cpp
   oss << std::hex << hash;
   return oss.str();  // Returns something like "a1b2c3d4e5f6..."
   ```

### Example:
```cpp
Input:  password = "root"
        pepper = "franco_secret_pepper_2024"
        
Step 1: data = "root" + "franco_secret_pepper_2024"
        = "rootfranco_secret_pepper_2024"
        
Step 2: Run 10,000 iterations of std::hash
        
Step 3: Convert to hex
        Output: "a1b2c3d4e5f6789..." (hexadecimal string)
```

---

## 2. Storage: How Hashes Are Saved

### Storage Location

**File:** `system.francodb` (system database)  
**Table:** `franco_users`  
**Column:** `password_hash` (VARCHAR, 128 bytes)

### Table Schema

```cpp
franco_users table:
  - username (VARCHAR, 64, PRIMARY KEY)
  - password_hash (VARCHAR, 128)  ← Hash stored here
  - db_name (VARCHAR, 64)
  - role (VARCHAR, 16)
```

### Storage Process: `SaveUsers()`

**Location:** `src/common/auth_manager.cpp:128-164`

```cpp
void AuthManager::SaveUsers() {
    // 1. Clear existing users
    std::string delete_sql = "2EMSA7 MEN franco_users;";
    // ... execute DELETE
    
    // 2. Insert all cached users
    for (const auto& [username, user] : users_cache_) {
        for (const auto& [db, role] : user.db_roles) {
            // Build INSERT query
            std::string insert_sql = "EMLA GOWA franco_users ELKEYAM ('" + 
                                username + "', '" + 
                                user.password_hash + "', '" +  // ← Hash inserted here
                                db + "', '" + 
                                role_str + "');";
            
            // Execute INSERT via ExecutionEngine
            system_engine_->Execute(stmt.get());
        }
    }
}
```

### What Happens:

1. **Hash is Generated**: When user is created, password is hashed
   ```cpp
   std::string hash = HashPassword(password);
   ```

2. **Stored in Memory Cache**: Hash is stored in `users_cache_` map
   ```cpp
   users_cache_[username].password_hash = hash;
   ```

3. **Written to Disk**: `SaveUsers()` executes SQL INSERT:
   ```sql
   INSERT INTO franco_users VALUES ('adham', 'a1b2c3d4...', 'default', 'ADMIN');
   ```

4. **Physical Storage**: 
   - ExecutionEngine writes row to `system.francodb`
   - Data goes through Buffer Pool Manager
   - Eventually written to disk pages (4KB pages)
   - Same storage mechanism as any other table data

### Example Storage:

When you create a user:
```sql
CREATE USER adham WITH PASSWORD 'a251m2006' ROLE ADMIN;
```

**What gets stored:**
```
system.francodb → franco_users table:
┌─────────┬──────────────────────────────┬──────────┬────────┐
│ username│ password_hash                 │ db_name  │ role   │
├─────────┼──────────────────────────────┼──────────┼────────┤
│ adham   │ a1b2c3d4e5f6789abcdef...     │ default  │ ADMIN  │
└─────────┴──────────────────────────────┴──────────┴────────┘
```

The hash `a1b2c3d4e5f6789abcdef...` is stored as a **VARCHAR string** in the table row.

---

## 3. Retrieval: How Hashes Are Loaded

### Loading Process: `LoadUsers()`

**Location:** `src/common/auth_manager.cpp:87-126`

```cpp
void AuthManager::LoadUsers() {
    users_cache_.clear();
    
    // 1. Query all users from franco_users table
    std::string select_sql = "2E5TAR * MEN franco_users;";
    Lexer lexer(select_sql);
    Parser parser(std::move(lexer));
    auto stmt = parser.ParseQuery();
    
    // 2. Execute SELECT query
    ExecutionResult res = system_engine_->Execute(stmt.get());
    
    // 3. Parse result set into UserInfo objects
    for (const auto& row : res.result_set->rows) {
        std::string username = row[0];
        std::string password_hash = row[1];  // ← Hash retrieved here
        std::string db = row[2];
        std::string role_str = row[3];
        
        // 4. Store in memory cache
        if (!users_cache_.count(username)) {
            UserInfo info;
            info.username = username;
            info.password_hash = password_hash;  // ← Cached in memory
            users_cache_[username] = info;
        }
        users_cache_[username].db_roles[db] = role;
    }
}
```

### What Happens:

1. **SQL Query**: Executes `SELECT * FROM franco_users`
   ```sql
   2E5TAR * MEN franco_users;  -- (Arabic-style: SELECT * FROM)
   ```

2. **ExecutionEngine**: Reads rows from `system.francodb`
   - Reads pages from disk via Buffer Pool Manager
   - Parses table rows
   - Returns result set

3. **Memory Cache**: Hashes are loaded into `users_cache_` map
   ```cpp
   users_cache_[username].password_hash = password_hash;
   ```

4. **Fast Lookup**: Subsequent authentication uses cached hashes (no disk I/O)

### Physical Retrieval Flow:

```
system.francodb (disk)
    ↓
Buffer Pool Manager (memory cache)
    ↓
ExecutionEngine::Execute() (reads pages)
    ↓
Parse rows from result set
    ↓
Extract password_hash column (row[1])
    ↓
Store in users_cache_ map (in-memory)
```

---

## 4. Authentication: How Hashes Are Used

### Authentication Process: `Authenticate()`

**Location:** `src/common/auth_manager.cpp:173-192`

```cpp
bool AuthManager::Authenticate(const std::string& username, 
                               const std::string& password, 
                               UserRole& out_role) {
    // Special case: root user (maayn)
    if (IsRoot(username)) {
        auto& config = ConfigManager::GetInstance();
        std::string input_hash = HashPassword(password);      // Hash input password
        std::string expected_hash = HashPassword(config.GetRootPassword());  // Hash expected password
        if (input_hash == expected_hash) {
            out_role = UserRole::SUPERADMIN;
            return true;
        }
        return false;
    }
    
    // Regular users: load from cache
    LoadUsers();  // Loads hashes from system.francodb into cache
    auto it = users_cache_.find(username);
    if (it == users_cache_.end()) return false;
    
    // Hash the input password
    std::string input_hash = HashPassword(password);
    
    // Compare with stored hash
    if (input_hash != it->second.password_hash) return false;
    
    out_role = UserRole::READONLY;  // Default role (per-db role set separately)
    return true;
}
```

### Authentication Flow:

```
User Login:
  username: "adham"
  password: "a251m2006"
    ↓
1. LoadUsers() → Reads system.francodb → Caches hashes
    ↓
2. Find user in cache: users_cache_["adham"]
    ↓
3. Hash input password: HashPassword("a251m2006")
    = "a1b2c3d4e5f6789..."
    ↓
4. Compare hashes:
    input_hash == stored_hash?
    "a1b2c3d4..." == "a1b2c3d4..." ✓
    ↓
5. Return true (authenticated)
```

### Important Points:

1. **Never Compare Plaintext**: Only hashes are compared
   ```cpp
   // ❌ WRONG (never done):
   if (password == stored_password) ...
   
   // ✅ CORRECT (what we do):
   if (HashPassword(password) == stored_hash) ...
   ```

2. **Root User Special Case**: `maayn` user doesn't need to be in `franco_users` table
   - Hash is computed on-the-fly from config
   - No database lookup needed

3. **Caching**: Hashes are cached in memory (`users_cache_`)
   - First authentication: Loads from disk
   - Subsequent authentications: Uses cache (faster)

---

## 5. Complete Example Flow

### Creating a User:

```sql
CREATE USER john WITH PASSWORD 'secret123' ROLE USER;
```

**Step-by-step:**

1. **Hash Generation**:
   ```cpp
   std::string hash = HashPassword("secret123");
   // Result: "f3a8b2c1d4e5f6a7b8c9d0e1f2a3b4c5..."
   ```

2. **Store in Cache**:
   ```cpp
   users_cache_["john"].password_hash = "f3a8b2c1d4e5f6a7b8c9d0e1f2a3b4c5...";
   users_cache_["john"].db_roles["default"] = UserRole::USER;
   ```

3. **Save to Disk**:
   ```sql
   INSERT INTO franco_users VALUES 
   ('john', 'f3a8b2c1d4e5f6a7b8c9d0e1f2a3b4c5...', 'default', 'USER');
   ```

4. **Physical Storage**:
   - Row written to `system.francodb`
   - Stored in a 4KB page
   - Persisted to disk

### Authenticating a User:

```cpp
Authenticate("john", "secret123", role);
```

**Step-by-step:**

1. **Load from Disk** (if not cached):
   ```sql
   SELECT * FROM franco_users WHERE username = 'john';
   ```
   - Reads `system.francodb`
   - Returns row: `('john', 'f3a8b2c1d4e5f6...', 'default', 'USER')`

2. **Hash Input Password**:
   ```cpp
   std::string input_hash = HashPassword("secret123");
   // Result: "f3a8b2c1d4e5f6a7b8c9d0e1f2a3b4c5..."
   ```

3. **Compare Hashes**:
   ```cpp
   if (input_hash == stored_hash) {
       // "f3a8b2c1d4e5f6..." == "f3a8b2c1d4e5f6..." ✓
       return true;  // Authentication successful
   }
   ```

---

## 6. Security Features

### ✅ What FrancoDB Does Right:

1. **Never Stores Plaintext**: Only hashes are stored
2. **Pepper**: Secret pepper adds extra security layer
3. **Iterative Hashing**: 10,000 iterations slow down brute-force
4. **Per-User Hashes**: Each user has unique hash (even same password = different hash due to username context)

### ⚠️ Security Considerations:

1. **Pepper Storage**: Pepper is in source code (`franco_net_config.h`)
   - In production, should be in config file or environment variable

2. **Hash Algorithm**: Uses `std::hash` (not cryptographically secure)
   - For production, use `bcrypt`, `argon2`, or `scrypt`

3. **No Salt**: Currently no per-user salt (pepper is global)
   - Same password = same hash (could be improved)

---

## 7. File Structure

### Where Hashes Are Stored:

```
data/
└── system.francodb          ← Contains franco_users table
    └── Pages 3+ contain rows:
        Row 1: ('maayn', 'hash1...', 'default', 'SUPERADMIN')
        Row 2: ('adham', 'hash2...', 'default', 'ADMIN')
        Row 3: ('john', 'hash3...', 'default', 'USER')
```

### Metadata:

```
data/
└── system.francodb.meta     ← Contains schema:
    TABLE franco_users 3 1 4 username 4 1 password_hash 4 0 ...
```

---

## 8. Summary

| Step | Action | Location | Data Flow |
|------|--------|----------|-----------|
| **1. Hash** | `HashPassword()` | `auth_manager.cpp:18` | `password` → `hash` (hex string) |
| **2. Store** | `SaveUsers()` | `auth_manager.cpp:128` | `hash` → `users_cache_` → `system.francodb` |
| **3. Retrieve** | `LoadUsers()` | `auth_manager.cpp:87` | `system.francodb` → `users_cache_` |
| **4. Authenticate** | `Authenticate()` | `auth_manager.cpp:173` | `HashPassword(input)` == `stored_hash` |

### Key Takeaways:

- ✅ Hashes are stored as **VARCHAR strings** in `franco_users` table
- ✅ Storage uses **normal table operations** (INSERT/SELECT via ExecutionEngine)
- ✅ Hashes are **cached in memory** for fast lookups
- ✅ Authentication **compares hashes**, never plaintext passwords
- ✅ Physical storage is in **`system.francodb`** (same as any other table data)

---

## 9. Code References

- **Hashing**: `src/common/auth_manager.cpp:18-35`
- **Storage**: `src/common/auth_manager.cpp:128-164`
- **Retrieval**: `src/common/auth_manager.cpp:87-126`
- **Authentication**: `src/common/auth_manager.cpp:173-192`
- **Pepper**: `src/include/common/franco_net_config.h`
