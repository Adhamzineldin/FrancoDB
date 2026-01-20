# Encryption Key Storage & Retrieval in FrancoDB

## Overview

FrancoDB uses **XOR encryption** to encrypt database files (`.francodb` and `.meta` files). The encryption key is stored in the **configuration file** (`francodb.conf`) and loaded into memory at server startup. This document explains:
1. How encryption keys are generated
2. How keys are stored in the config file
3. How keys are retrieved and used
4. How encryption is applied to database files

---

## 1. Encryption Key Generation

### Key Generation: `GenerateEncryptionKey()`

**Location:** `src/common/config_manager.cpp:136-146`

```cpp
std::string ConfigManager::GenerateEncryptionKey() {
    std::random_device rd;  // Hardware random number generator
    std::mt19937 gen(rd()); // Mersenne Twister PRNG
    std::uniform_int_distribution<> dis(0, 255); // Random bytes (0-255)
    
    std::stringstream ss;
    for (int i = 0; i < 32; ++i) {  // Generate 32 random bytes
        ss << std::hex << std::setw(2) << std::setfill('0') << dis(gen);
    }
    return ss.str();  // Returns 64-character hex string
}
```

### How It Works:

1. **Random Generation**: Uses `std::random_device` (hardware RNG if available)
2. **32 Random Bytes**: Generates 32 random bytes (0-255)
3. **Hex Encoding**: Converts each byte to 2-digit hexadecimal
4. **Result**: 64-character hex string (e.g., `"a1b2c3d4e5f6789abcdef0123456789abcdef0123456789abcdef0123456789abcd"`)

### Example Output:
```
Generated encryption key: a1b2c3d4e5f6789abcdef0123456789abcdef0123456789abcdef0123456789abcd
```

---

## 2. Storage: How Keys Are Saved

### Storage Location

**File:** `francodb.conf` (configuration file in project root)  
**Format:** Plain text INI-style file  
**Key Name:** `encryption_key`

### Storage Process: `SaveConfig()`

**Location:** `src/common/config_manager.cpp:70-90`

```cpp
bool ConfigManager::SaveConfig(const std::string& config_path) {
    std::ofstream file(config_path);
    if (!file.is_open()) {
        return false;
    }
    
    file << "# FrancoDB Configuration File\n";
    file << "# Generated automatically\n\n";
    
    file << "port = " << port_ << "\n";
    file << "root_username = \"" << root_username_ << "\"\n";
    file << "root_password = \"" << root_password_ << "\"\n";
    file << "data_directory = \"" << data_directory_ << "\"\n";
    file << "encryption_enabled = " << (encryption_enabled_ ? "true" : "false") << "\n";
    
    // Save encryption key if it exists
    if (!encryption_key_.empty()) {
        file << "encryption_key = \"" << encryption_key_ << "\"\n";
    }
    
    file << "autosave_interval = " << autosave_interval_ << "\n";
    
    return true;
}
```

### Config File Format:

```ini
# FrancoDB Configuration File
# Generated automatically

port = 2501
root_username = "maayn"
root_password = "root"
data_directory = "data"
encryption_enabled = true
encryption_key = "a1b2c3d4e5f6789abcdef0123456789abcdef0123456789abcdef0123456789abcd"
autosave_interval = 30
```

### Important Notes:

⚠️ **Security Warning**: The encryption key is stored in **plain text** in the config file!
- Anyone with access to `francodb.conf` can decrypt your databases
- In production, consider:
  - File permissions (chmod 600 on Linux)
  - Environment variables
  - Key management systems (AWS KMS, HashiCorp Vault)

---

## 3. Retrieval: How Keys Are Loaded

### Loading Process: `LoadConfig()`

**Location:** `src/common/config_manager.cpp:18-68`

```cpp
bool ConfigManager::LoadConfig(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        return false;  // Config file doesn't exist
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        // Parse key=value pairs
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;
        
        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);
        
        // Trim whitespace and remove quotes
        // ... (trimming code) ...
        
        // Set values
        if (key == "encryption_key") {
            encryption_key_ = value;  // ← Key loaded here
        } else if (key == "encryption_enabled") {
            encryption_enabled_ = (value == "true" || value == "1" || value == "yes");
        }
        // ... (other config values) ...
    }
    
    return true;
}
```

### What Happens:

1. **File Read**: Opens `francodb.conf` for reading
2. **Line Parsing**: Reads each line, skips comments
3. **Key Extraction**: Finds `encryption_key = "..."` line
4. **Value Storage**: Stores key in `ConfigManager::encryption_key_` (in-memory)
5. **Access**: Key can be retrieved via `GetEncryptionKey()`

### Memory Storage:

```cpp
class ConfigManager {
private:
    std::string encryption_key_ = "";  // ← Stored in memory
    bool encryption_enabled_ = false;
    // ...
};
```

---

## 4. Usage: How Keys Are Applied to Databases

### Key Distribution Flow:

```
francodb.conf (disk)
    ↓
ConfigManager::LoadConfig() (loads into memory)
    ↓
ConfigManager::GetEncryptionKey() (retrieves from memory)
    ↓
DiskManager::SetEncryptionKey() (sets per-database)
    ↓
Encryption::EncryptXOR() / DecryptXOR() (uses key for encryption)
```

### Setting Encryption Key on Databases

#### **1. Default Database** (main.cpp)

**Location:** `src/cmd/server/main.cpp:186-200`

```cpp
// Load configuration
auto& config = ConfigManager::GetInstance();
config.LoadConfig("francodb.conf");

// Get encryption settings
bool encryption_enabled = config.IsEncryptionEnabled();
std::string encryption_key = config.GetEncryptionKey();

// Create disk manager
std::string db_path = data_dir + "/francodb.db";
auto disk_manager = std::make_unique<DiskManager>(db_path);

// Apply encryption if enabled
if (encryption_enabled && !encryption_key.empty()) {
    disk_manager->SetEncryptionKey(encryption_key);  // ← Key set here
}
```

#### **2. System Database** (franco_server.cpp)

**Location:** `src/network/franco_server.cpp:50-56`

```cpp
// Create system database
std::string system_db_path = data_dir + "/system";
system_disk_ = std::make_unique<DiskManager>(system_db_path);

// Apply encryption to system database if enabled
if (config.IsEncryptionEnabled() && !config.GetEncryptionKey().empty()) {
    system_disk_->SetEncryptionKey(config.GetEncryptionKey());  // ← Key set here
}
```

#### **3. User-Created Databases** (database_registry.cpp)

**Location:** `src/include/network/database_registry.h:43-64`

```cpp
std::shared_ptr<DbEntry> GetOrCreate(const std::string &name, size_t pool_size = BUFFER_POOL_SIZE) {
    // ... create database ...
    
    auto entry = std::make_shared<DbEntry>();
    std::string db_path = data_dir + "/" + name;
    entry->dm = std::make_unique<DiskManager>(db_path);
    
    // Apply encryption if enabled
    auto& config = ConfigManager::GetInstance();
    if (config.IsEncryptionEnabled() && !config.GetEncryptionKey().empty()) {
        entry->dm->SetEncryptionKey(config.GetEncryptionKey());  // ← Key set here
    }
    
    // ...
}
```

### DiskManager Storage:

**Location:** `src/include/storage/disk/disk_manager.h:83-84`

```cpp
class DiskManager {
private:
    std::string encryption_key_;      // ← Key stored per-database
    bool encryption_enabled_ = false;
    
public:
    void SetEncryptionKey(const std::string& key) {
        encryption_key_ = key;
        encryption_enabled_ = !key.empty();
    }
};
```

---

## 5. Encryption Process: How Keys Are Used

### Encryption Algorithm: XOR Cipher

**Location:** `src/common/encryption.cpp:9-23`

```cpp
void Encryption::EncryptXOR(const std::string& key, char* data, size_t size) {
    if (key.empty()) return; // No encryption if key is empty
    
    // Derive 32-byte key from hex string
    auto key_bytes = DeriveKey(key, 32);
    size_t key_len = key_bytes.size();
    
    // XOR each byte with key (cycling through key)
    for (size_t i = 0; i < size; ++i) {
        data[i] ^= key_bytes[i % key_len];  // XOR operation
    }
}

void Encryption::DecryptXOR(const std::string& key, char* data, size_t size) {
    // XOR is symmetric - encryption and decryption are the same
    EncryptXOR(key, data, size);
}
```

### Key Derivation: `DeriveKey()`

**Location:** `src/common/encryption.cpp:43-59`

```cpp
std::vector<uint8_t> Encryption::DeriveKey(const std::string& key_str, size_t key_size) {
    std::vector<uint8_t> key(key_size);  // 32 bytes
    
    if (key_str.length() >= key_size) {
        // Use first 32 bytes (if key is long enough)
        std::memcpy(key.data(), key_str.data(), key_size);
    } else {
        // Repeat key to fill 32 bytes (if key is short)
        size_t pos = 0;
        for (size_t i = 0; i < key_size; ++i) {
            key[i] = static_cast<uint8_t>(key_str[pos]);
            pos = (pos + 1) % key_str.length();
        }
    }
    
    return key;
}
```

### How It Works:

1. **Hex to Bytes**: Converts 64-char hex string → 32 bytes
   ```
   "a1b2c3d4..." → [0xa1, 0xb2, 0xc3, 0xd4, ...]
   ```

2. **XOR Operation**: For each byte in data:
   ```cpp
   encrypted_byte = data_byte ^ key_byte[i % 32]
   ```

3. **Symmetric**: Encryption and decryption are the same (XOR is reversible)

### Example:

```
Original data:  "Hello World"
Key (hex):      "a1b2c3d4..."
Key (bytes):    [0xa1, 0xb2, 0xc3, ...]

Encryption:
  'H' (0x48) ^ 0xa1 = 0xe9
  'e' (0x65) ^ 0xb2 = 0xd7
  'l' (0x6c) ^ 0xc3 = 0xaf
  ...

Decryption (same operation):
  0xe9 ^ 0xa1 = 'H' (0x48)
  0xd7 ^ 0xb2 = 'e' (0x65)
  0xaf ^ 0xc3 = 'l' (0x6c)
  ...
```

---

## 6. When Encryption Is Applied

### ReadPage() - Decryption

**Location:** `src/storage/disk/disk_manager.cpp:103-131`

```cpp
void DiskManager::ReadPage(uint32_t page_id, char *page_data) {
    // Read page from disk
    // ... (OS-specific read) ...
    
    // Decrypt if encryption is enabled
    if (encryption_enabled_ && !encryption_key_.empty() && page_id > 0) {
        // Don't decrypt page 0 (magic header must remain readable)
        Encryption::DecryptXOR(encryption_key_, page_data, PAGE_SIZE);
    }
}
```

### WritePage() - Encryption

**Location:** `src/storage/disk/disk_manager.cpp:133-160`

```cpp
void DiskManager::WritePage(uint32_t page_id, const char *page_data) {
    // Encrypt if encryption is enabled (make a copy to avoid modifying original)
    char encrypted_data[PAGE_SIZE];
    const char* data_to_write = page_data;
    
    if (encryption_enabled_ && !encryption_key_.empty() && page_id > 0) {
        // Don't encrypt page 0 (magic header must remain readable)
        std::memcpy(encrypted_data, page_data, PAGE_SIZE);
        Encryption::EncryptXOR(encryption_key_, encrypted_data, PAGE_SIZE);
        data_to_write = encrypted_data;
    }
    
    // Write to disk
    // ... (OS-specific write) ...
}
```

### Metadata Encryption

**Location:** `src/storage/disk/disk_manager.cpp:183-212`

```cpp
void DiskManager::WriteMetadata(const std::string &data) {
    std::string data_to_write = data;
    
    // Encrypt metadata if encryption is enabled
    if (encryption_enabled_ && !encryption_key_.empty()) {
        std::vector<char> encrypted(data_to_write.begin(), data_to_write.end());
        Encryption::EncryptXOR(encryption_key_, encrypted.data(), encrypted.size());
        data_to_write = std::string(encrypted.data(), encrypted.size());
    }
    
    // Write to .meta file
    // ... (write with magic header) ...
}
```

### Important Notes:

- **Page 0 is NOT encrypted**: Magic header (`"FRANCODB"`) must remain readable
- **All other pages are encrypted**: Pages 1, 2, 3+ are encrypted
- **Metadata is encrypted**: `.meta` files are encrypted (except magic header)

---

## 7. Complete Example Flow

### Setup (First Time):

```cpp
// 1. Interactive configuration
ConfigManager::InteractiveConfig();
// User enables encryption
// System generates key: "a1b2c3d4e5f6789..."

// 2. Save to config file
ConfigManager::SaveConfig("francodb.conf");
// Writes: encryption_key = "a1b2c3d4e5f6789..."
```

### Server Startup:

```cpp
// 1. Load config
ConfigManager::LoadConfig("francodb.conf");
// Reads: encryption_key = "a1b2c3d4e5f6789..."
// Stores in: ConfigManager::encryption_key_

// 2. Create default database
DiskManager dm("data/francodb.db");
dm.SetEncryptionKey(config.GetEncryptionKey());
// Key stored in: DiskManager::encryption_key_

// 3. Create system database
DiskManager system_dm("data/system");
system_dm.SetEncryptionKey(config.GetEncryptionKey());
// Same key used for all databases
```

### Database Operations:

```cpp
// Write data
char page[4096] = "Hello World...";
dm.WritePage(3, page);
// → Encrypts page with key
// → Writes encrypted data to disk

// Read data
char page[4096];
dm.ReadPage(3, page);
// → Reads encrypted data from disk
// → Decrypts page with key
// → Returns: "Hello World..."
```

---

## 8. Security Considerations

### ✅ What FrancoDB Does:

1. **Key Generation**: Uses hardware RNG when available
2. **Key Length**: 32 bytes (256 bits) - strong enough for XOR
3. **Per-Database**: Each database uses the same key (from config)
4. **Memory Storage**: Key stored in memory during runtime

### ⚠️ Security Limitations:

1. **Plain Text Storage**: Key stored in plain text in `francodb.conf`
   - **Risk**: Anyone with file access can decrypt databases
   - **Mitigation**: Use file permissions (chmod 600)

2. **XOR Encryption**: Not cryptographically secure
   - **Risk**: Vulnerable to known-plaintext attacks
   - **Mitigation**: For production, use AES-256 or similar

3. **Single Key**: All databases use the same key
   - **Risk**: Compromising one database compromises all
   - **Mitigation**: Use per-database keys (future enhancement)

4. **No Key Rotation**: Key cannot be changed without re-encrypting
   - **Risk**: Compromised key = permanent vulnerability
   - **Mitigation**: Implement key rotation mechanism

### 🔒 Best Practices:

1. **File Permissions**:
   ```bash
   chmod 600 francodb.conf  # Only owner can read/write
   ```

2. **Environment Variables** (future):
   ```cpp
   encryption_key = std::getenv("FRANCODB_ENCRYPTION_KEY");
   ```

3. **Key Management Service** (future):
   - AWS KMS
   - HashiCorp Vault
   - Azure Key Vault

---

## 9. Summary

| Step | Action | Location | Data Flow |
|------|--------|----------|-----------|
| **1. Generate** | `GenerateEncryptionKey()` | `config_manager.cpp:136` | Random bytes → 64-char hex string |
| **2. Store** | `SaveConfig()` | `config_manager.cpp:70` | Memory → `francodb.conf` (plain text) |
| **3. Load** | `LoadConfig()` | `config_manager.cpp:18` | `francodb.conf` → Memory (`encryption_key_`) |
| **4. Apply** | `SetEncryptionKey()` | `disk_manager.h:76` | Config → `DiskManager::encryption_key_` |
| **5. Use** | `EncryptXOR()` / `DecryptXOR()` | `encryption.cpp:9` | Key → XOR cipher → Encrypted/Decrypted data |

### Key Takeaways:

- ✅ Encryption key is stored in **`francodb.conf`** (plain text)
- ✅ Key is loaded into **memory** at server startup
- ✅ Key is applied to **each database** via `SetEncryptionKey()`
- ✅ Encryption uses **XOR cipher** (symmetric, reversible)
- ✅ **Page 0 is NOT encrypted** (magic header must be readable)
- ⚠️ **Security warning**: Plain text storage is a security risk

---

## 10. Code References

- **Key Generation**: `src/common/config_manager.cpp:136-146`
- **Key Storage**: `src/common/config_manager.cpp:70-90`
- **Key Loading**: `src/common/config_manager.cpp:18-68`
- **Key Application**: `src/include/storage/disk/disk_manager.h:76`
- **Encryption**: `src/common/encryption.cpp:9-23`
- **Key Derivation**: `src/common/encryption.cpp:43-59`
- **Read Encryption**: `src/storage/disk/disk_manager.cpp:103-131`
- **Write Encryption**: `src/storage/disk/disk_manager.cpp:133-160`
