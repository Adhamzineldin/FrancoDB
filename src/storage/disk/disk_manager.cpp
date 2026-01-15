#include "storage/disk/disk_manager.h"
#include "common/encryption.h"
#include <stdexcept>
#include <cstring>
#include <filesystem>
#include <cstdio>

namespace francodb {
    constexpr char FRAME_FILE_MAGIC[] = "FRANCO_DATABASE_MADE_BY_MAAYN";
    constexpr size_t MAGIC_LEN = sizeof(FRAME_FILE_MAGIC) - 1; 

    // New Magic Header for the Metadata File
    constexpr char META_FILE_MAGIC[] = "FRANCO_META"; 
    constexpr size_t META_MAGIC_LEN = sizeof(META_FILE_MAGIC) - 1;

    
    uint32_t CalculateChecksum(const char* data) {
        uint32_t sum = 0;
        // We skip the first 4 bytes where the checksum itself is stored
        for (size_t i = 4; i < PAGE_SIZE; i++) {
            sum += static_cast<uint8_t>(data[i]);
        }
        return sum;
    }
    
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
        OVERLAPPED overlapped = {0};
        overlapped.Offset = offset;
        DWORD bytes_read;
        ReadFile(db_io_handle_, page_data, PAGE_SIZE, &bytes_read, &overlapped);
#endif

        if (bytes_read < PAGE_SIZE) return;

        // 1. Decrypt first
        if (encryption_enabled_ && !encryption_key_.empty() && page_id > 0) {
            Encryption::DecryptXOR(encryption_key_, page_data, PAGE_SIZE);
        }

        // 2. Verify Checksum
        // Only check checksum for data pages (not bitmap or meta pages)
        if (page_id > 2) {
            uint32_t stored_checksum;
            std::memcpy(&stored_checksum, page_data, sizeof(uint32_t));
            uint32_t actual_checksum = CalculateChecksum(page_data);
            // std::fprintf(stderr, "[DEBUG] ReadPage: page_id=%u stored_checksum=%08x actual_checksum=%08x first_bytes=%02x %02x %02x %02x\n", page_id, stored_checksum, actual_checksum, (unsigned char)page_data[0], (unsigned char)page_data[1], (unsigned char)page_data[2], (unsigned char)page_data[3]);
            if (stored_checksum != actual_checksum) {
                std::cerr << "[CRITICAL] Checksum Mismatch on Page " << page_id << "!" << std::endl;
                throw std::runtime_error("Data Corruption Detected (Checksum Error)");
            }
        }
    }

    void DiskManager::WritePage(uint32_t page_id, const char *page_data) {
        char processed_data[PAGE_SIZE];
        std::memcpy(processed_data, page_data, PAGE_SIZE);

        // 1. Calculate and store Checksum in the first 4 bytes
        // (Excluding Page 0, 1, 2 which are special: Magic, Reserved, Bitmap)
        if (page_id > 2) {
            uint32_t checksum = CalculateChecksum(processed_data);
            std::memcpy(processed_data, &checksum, sizeof(uint32_t));
        }

        // 2. Encrypt if enabled
        if (encryption_enabled_ && !encryption_key_.empty() && page_id > 0) {
            Encryption::EncryptXOR(encryption_key_, processed_data, PAGE_SIZE);
        }

        // 3. Physical Write
        uint32_t offset = page_id * PAGE_SIZE;
#ifdef _WIN32
        OVERLAPPED overlapped = {0};
        overlapped.Offset = offset;
        DWORD bytes_written;
        if (!WriteFile(db_io_handle_, processed_data, PAGE_SIZE, &bytes_written, &overlapped)) {
            throw std::runtime_error("Disk Write Failure on Page " + std::to_string(page_id));
        }
#endif
        // std::fprintf(stderr, "[DEBUG] WritePage: page_id=%u checksum=%08x first_bytes=%02x %02x %02x %02x\n", page_id, *(const uint32_t*)page_data, (unsigned char)page_data[0], (unsigned char)page_data[1], (unsigned char)page_data[2], (unsigned char)page_data[3]);
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


    
    void DiskManager::WriteMetadata(const std::string &data) {
        std::string temp_meta = meta_file_name_ + ".tmp";
        std::string data_to_write = data;

        if (encryption_enabled_ && !encryption_key_.empty()) {
            std::vector<char> encrypted(data_to_write.begin(), data_to_write.end());
            Encryption::EncryptXOR(encryption_key_, encrypted.data(), encrypted.size());
            data_to_write.assign(encrypted.data(), encrypted.size());
        }

        std::ofstream out(temp_meta, std::ios::binary | std::ios::trunc);
        out.write(META_FILE_MAGIC, META_MAGIC_LEN);
        size_t size = data_to_write.size();
        out.write(reinterpret_cast<const char*>(&size), sizeof(size));
        out.write(data_to_write.c_str(), size);
        out.close();

        // Force bits to physical platter
        HANDLE hFile = CreateFileA(temp_meta.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(hFile);
            CloseHandle(hFile);
        }

        // Atomic Swap
        MoveFileExA(temp_meta.c_str(), meta_file_name_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }

    bool DiskManager::ReadMetadata(std::string &data) {
        // Check file size first
        if (GetFileSize(meta_file_name_) <= (int)META_MAGIC_LEN) {
            return false; 
        }

        std::ifstream in(meta_file_name_, std::ios::binary);
        if (!in.is_open()) return false; 

        char magic[16] = {0};
        in.read(magic, META_MAGIC_LEN);
    
        if (std::memcmp(magic, META_FILE_MAGIC, META_MAGIC_LEN) != 0) {
            std::cerr << "[DISK] Invalid Meta File (Wrong Magic Header)!" << std::endl;
            return false;
        }

        size_t size = 0;
        in.read(reinterpret_cast<char*>(&size), sizeof(size));
    
        // Safety check against corrupted size values
        if (size == 0 || size > 100 * 1024 * 1024) return false; 

        std::vector<char> buffer(size);
        in.read(buffer.data(), size);
    
        if (encryption_enabled_ && !encryption_key_.empty()) {
            Encryption::DecryptXOR(encryption_key_, buffer.data(), buffer.size());
        }
    
        data.assign(buffer.data(), size);
        return true;
    }
    
    // Helper to update checksum in a page buffer (excluding page 0, 1, 2)
    void UpdatePageChecksum(char* page_data, uint32_t page_id) {
        if (page_id <= 2) return;
        uint32_t checksum = CalculateChecksum(page_data);
        std::memcpy(page_data, &checksum, sizeof(uint32_t));
    }
}
