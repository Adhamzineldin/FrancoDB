#include "storage/disk/disk_manager.h"
#include <stdexcept>
#include <cstring>
#include <filesystem>

namespace francodb {
    constexpr char FRAME_FILE_MAGIC[] = "FRANCODB";
    constexpr size_t MAGIC_LEN = sizeof(FRAME_FILE_MAGIC) - 1; 

    DiskManager::DiskManager(const std::string &db_file) {
        // 1. Enforce the ".franco" extension
        std::filesystem::path path(db_file);
        if (path.extension() != ".fdb") {
            file_name_ = db_file + ".fdb";
        } else {
            file_name_ = db_file;
        }

        // 2. Open the file (OS Specific)
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


        // 3. THE MAGIC CHECK (The Professor Pleaser)
        // Check if the file is empty (New Database)
        if (GetFileSize(file_name_) == 0) {
            // It's a new file. Branding it!
            // We write the Magic Bytes "FRAN" into the very first page.
            char magic_page[PAGE_SIZE];
            std::memset(magic_page, 0, PAGE_SIZE);
            std::memcpy(magic_page, FRAME_FILE_MAGIC, MAGIC_LEN);

            WritePage(0, magic_page); // Page 0 is now reserved for Metadata

            // Ensure it hits the disk immediately
            FlushLog();
            std::cout << "[INFO] Created new FrancoDB file: " << file_name_ << std::endl;
        } else {
            // It's an existing file. Validate it!
            char magic_page[PAGE_SIZE];
            ReadPage(0, magic_page);

            // Check the first 4 bytes
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
        // Windows does not have atomic seek+read (pread). 
        // We use OVERLAPPED to specify the offset explicitly.
        OVERLAPPED overlapped = {};
        overlapped.Offset = offset;

        DWORD bytes_read;
        if (!ReadFile(db_io_handle_, page_data, PAGE_SIZE, &bytes_read, &overlapped)) {
            throw std::runtime_error("Disk I/O Error: Failed to read page " + std::to_string(page_id));
        }

        // If we read less than PAGE_SIZE (e.g., new file), fill the rest with zeros
        if (bytes_read < PAGE_SIZE) {
            std::memset(page_data + bytes_read, 0, PAGE_SIZE - bytes_read);
        }
#else
        // Linux pread is atomic and thread-safe
        ssize_t bytes_read = pread(db_io_fd_, page_data, PAGE_SIZE, offset);
        if (bytes_read == -1) {
            throw std::runtime_error("Disk I/O Error: Failed to read page " + std::to_string(page_id));
        }
        if (bytes_read < PAGE_SIZE) {
            std::memset(page_data + bytes_read, 0, PAGE_SIZE - bytes_read);
        }
#endif
    }

    void DiskManager::WritePage(uint32_t page_id, const char *page_data) {
        uint32_t offset = page_id * PAGE_SIZE;

#ifdef _WIN32
        OVERLAPPED overlapped = {};
        overlapped.Offset = offset;

        DWORD bytes_written;
        if (!WriteFile(db_io_handle_, page_data, PAGE_SIZE, &bytes_written, &overlapped)) {
            throw std::runtime_error("Disk I/O Error: Failed to write page " + std::to_string(page_id));
        }
#else
        ssize_t bytes_written = pwrite(db_io_fd_, page_data, PAGE_SIZE, offset);
        if (bytes_written == -1) {
            throw std::runtime_error("Disk I/O Error: Failed to write page " + std::to_string(page_id));
        }
#endif
    }

    // --- THE MISSING IMPLEMENTATION ---
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
} // namespace francodb
