#include "storage/disk/disk_manager.h"
#include "common/encryption.h"
#include <stdexcept>
#include <cstring>
#include <filesystem>

namespace francodb {
    constexpr char FRAME_FILE_MAGIC[] = "FRANCO_DATABASE_MADE_BY_MAAYN";
    constexpr size_t MAGIC_LEN = sizeof(FRAME_FILE_MAGIC) - 1; 

    // New Magic Header for the Metadata File
    constexpr char META_FILE_MAGIC[] = "FRANCO_META"; 
    constexpr size_t META_MAGIC_LEN = sizeof(META_FILE_MAGIC) - 1;

    DiskManager::DiskManager(const std::string &db_file) {
        // 1. Enforce the ".francodb" extension
        std::filesystem::path path(db_file);
        if (path.extension() != ".francodb") {
            file_name_ = db_file + ".francodb";
        } else {
            file_name_ = db_file;
        }

        // 2. Set Meta File Name (e.g., "my.francodb.meta")
        meta_file_name_ = file_name_ + ".meta";

        // 3. Open the file (OS Specific)
#ifdef _WIN32
        // Windows: CreateFileA (The 'A' stands for ANSI strings)
        db_io_handle_ = CreateFileA(
            file_name_.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ, // Allow other apps to read (but not write)
            nullptr,
            OPEN_ALWAYS, // Open existing or create new
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (db_io_handle_ == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Failed to open database file: " + file_name_);
        }
#else
        // Linux: open()
        db_io_fd_ = open(file_name_.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if (db_io_fd_ == -1) {
            throw std::runtime_error("Failed to open database file: " + file_name_);
        }
#endif

        // 4. THE MAGIC CHECK (The Professor Pleaser)
        // Check if the file is empty (New Database)
        if (GetFileSize(file_name_) == 0) {
            // --- PAGE 0: BRANDING ---
            char magic_page[PAGE_SIZE];
            std::memset(magic_page, 0, PAGE_SIZE);
            std::memcpy(magic_page, FRAME_FILE_MAGIC, MAGIC_LEN);
            WritePage(0, magic_page); 

            // --- PAGE 2: FREE PAGE BITMAP ---
            char bitmap_page[PAGE_SIZE];
            std::memset(bitmap_page, 0, PAGE_SIZE);
            
            // We must mark Page 0, 1, and 2 as "USED" so they aren't recycled.
            // Binary: 00000111 = 0x07 (Page 0, 1, 2)
            bitmap_page[0] = 0x07; 
            
            WritePage(2, bitmap_page); 

            FlushLog();
            std::cout << "[INFO] Created new FrancoDB file with FreePageMap: " << file_name_ << std::endl;
        } else {
            // It's an existing file. Validate it!
            char magic_page[PAGE_SIZE];
            ReadPage(0, magic_page);

            // Check the first bytes
            if (std::memcmp(magic_page, FRAME_FILE_MAGIC, MAGIC_LEN) != 0) {
                throw std::runtime_error(
                    "CORRUPTION ERROR: File is not a valid FrancoDB format. Missing magic header.");
            }
        }
    }

    DiskManager::~DiskManager() {
        ShutDown();
    }

    void DiskManager::ShutDown() {
#ifdef _WIN32
        if (db_io_handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(db_io_handle_);
            db_io_handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (db_io_fd_ != -1) {
            close(db_io_fd_);
            db_io_fd_ = -1;
        }
#endif
    }

    void DiskManager::ReadPage(uint32_t page_id, char *page_data) {
        uint32_t offset = page_id * PAGE_SIZE;

#ifdef _WIN32
        OVERLAPPED overlapped = {};
        overlapped.Offset = offset;
        DWORD bytes_read;
        if (!ReadFile(db_io_handle_, page_data, PAGE_SIZE, &bytes_read, &overlapped)) {
            throw std::runtime_error("Disk I/O Error: Failed to read page " + std::to_string(page_id));
        }
        if (bytes_read < PAGE_SIZE) {
            std::memset(page_data + bytes_read, 0, PAGE_SIZE - bytes_read);
        }
#else
        ssize_t bytes_read = pread(db_io_fd_, page_data, PAGE_SIZE, offset);
        if (bytes_read == -1) {
            throw std::runtime_error("Disk I/O Error: Failed to read page " + std::to_string(page_id));
        }
        if (bytes_read < PAGE_SIZE) {
            std::memset(page_data + bytes_read, 0, PAGE_SIZE - bytes_read);
        }
#endif
        
        // Decrypt if encryption is enabled
        if (encryption_enabled_ && !encryption_key_.empty() && page_id > 0) {
            // Don't decrypt page 0 (magic header)
            Encryption::DecryptXOR(encryption_key_, page_data, PAGE_SIZE);
        }
    }

    void DiskManager::WritePage(uint32_t page_id, const char *page_data) {
        // SAFEGUARD: Only allow writing to page 0 if the data is the magic header
        if (page_id == 0) {
            if (std::memcmp(page_data, FRAME_FILE_MAGIC, MAGIC_LEN) != 0) {
                std::cerr << "[FATAL] Attempt to overwrite page 0 (magic header) with invalid data!" << std::endl;
                std::cerr << "[DEBUG] First 32 bytes: ";
                for (size_t i = 0; i < 32; ++i) std::cerr << std::hex << (0xFF & page_data[i]) << " ";
                std::cerr << std::dec << std::endl;
                throw std::runtime_error("Attempt to overwrite page 0 (magic header) with invalid data. Aborted.");
            }
        }
        // Encrypt if encryption is enabled (make a copy to avoid modifying original)
        char encrypted_data[PAGE_SIZE];
        const char* data_to_write = page_data;
        if (encryption_enabled_ && !encryption_key_.empty() && page_id > 0) {
            // Don't encrypt page 0 (magic header)
            std::memcpy(encrypted_data, page_data, PAGE_SIZE);
            Encryption::EncryptXOR(encryption_key_, encrypted_data, PAGE_SIZE);
            data_to_write = encrypted_data;
        }
        uint32_t offset = page_id * PAGE_SIZE;

#ifdef _WIN32
        OVERLAPPED overlapped = {};
        overlapped.Offset = offset;
        DWORD bytes_written;
        if (!WriteFile(db_io_handle_, data_to_write, PAGE_SIZE, &bytes_written, &overlapped)) {
            throw std::runtime_error("Disk I/O Error: Failed to write page " + std::to_string(page_id));
        }
#else
        ssize_t bytes_written = pwrite(db_io_fd_, data_to_write, PAGE_SIZE, offset);
        if (bytes_written == -1) {
            throw std::runtime_error("Disk I/O Error: Failed to write page " + std::to_string(page_id));
        }
#endif
        // Ensure durability for page 0 and other critical pages
        if (page_id == 0) {
            FlushLog();
        }
    }

    void DiskManager::FlushLog() {
        std::lock_guard<std::mutex> guard(io_mutex_);
#ifdef _WIN32
        FlushFileBuffers(db_io_handle_);
#else
        fsync(db_io_fd_);
#endif
    }

    int DiskManager::GetFileSize(const std::string &file_name) {
        struct stat stat_buf;
        int rc = stat(file_name.c_str(), &stat_buf);
        return rc == 0 ? static_cast<int>(stat_buf.st_size) : -1;
    }
    
    int DiskManager::GetNumPages() {
        return GetFileSize(file_name_) / PAGE_SIZE;
    }

    // --- NEW: SECURE METADATA I/O ---

    void DiskManager::WriteMetadata(const std::string &data) {
        // We use std::ofstream here because Metadata is variable length text, not fixed 4KB pages.
        // However, we wrap it with a Magic Header for security.
        
        std::string data_to_write = data;
        
        // Encrypt metadata if encryption is enabled
        if (encryption_enabled_ && !encryption_key_.empty()) {
            std::vector<char> encrypted(data_to_write.begin(), data_to_write.end());
            Encryption::EncryptXOR(encryption_key_, encrypted.data(), encrypted.size());
            data_to_write = std::string(encrypted.data(), encrypted.size());
        }
        
        std::ofstream out(meta_file_name_, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            std::cerr << "[DISK] Error opening meta file for write: " << meta_file_name_ << std::endl;
            return;
        }

        // 1. Write Magic Header (FRANCO_META)
        out.write(META_FILE_MAGIC, META_MAGIC_LEN);

        // 2. Write Size
        size_t size = data_to_write.size();
        out.write(reinterpret_cast<const char*>(&size), sizeof(size));

        // 3. Write Actual Data (encrypted if enabled)
        out.write(data_to_write.c_str(), size);
        out.close();
        // Ensure metadata is durable
        FlushLog();
    }

    bool DiskManager::ReadMetadata(std::string &data) {
        std::ifstream in(meta_file_name_, std::ios::binary);
        if (!in.is_open()) return false; 

        // 1. Verify Magic Header
        char magic[16];
        std::memset(magic, 0, 16);
        in.read(magic, META_MAGIC_LEN);
        
        if (std::memcmp(magic, META_FILE_MAGIC, META_MAGIC_LEN) != 0) {
            std::cerr << "[DISK] Invalid Meta File (Wrong Magic Header)!" << std::endl;
            return false;
        }

        // 2. Read Size
        size_t size;
        in.read(reinterpret_cast<char*>(&size), sizeof(size));

        // 3. Read Data
        std::vector<char> buffer(size);
        in.read(buffer.data(), size);
        
        // 4. Decrypt if encryption is enabled
        if (encryption_enabled_ && !encryption_key_.empty()) {
            Encryption::DecryptXOR(encryption_key_, buffer.data(), buffer.size());
        }
        
        data.assign(buffer.data(), size);
        return true;
    }
    
} // namespace francodb

