#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sys/stat.h>
#include <filesystem>
#include <vector>

// OS-Specific Includes for Raw I/O
#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

#include "common/config.h"
#include "common/config_manager.h"

namespace francodb {
    uint32_t CalculateChecksum(const char* data);
    void UpdatePageChecksum(char* page_data, uint32_t page_id);

    /**
     * DiskManager takes care of the allocation and deallocation of pages within a database.
     * It performs the physical read/write operations to the disk.
     * Enforces the .francodb file extension.
     */
    class DiskManager {
    public:
        /**
         * Creates or opens a database file.
         * Automatically appends ".francodb" if missing.
         * @param db_file The base name of the database.
         */
        explicit DiskManager(const std::string &db_file);

        ~DiskManager();

        /**
         * Read a specific page from the .francodb file.
         */
        void ReadPage(uint32_t page_id, char *page_data);

        /**
         * Write a specific page to the .francodb file.
         */
        void WritePage(uint32_t page_id, const char *page_data);
        
        void FlushLog();

        /**
         * Returns the size of the file in bytes.
         */
        int GetFileSize(const std::string &file_name);
    
        /**
         * Returns the enforced file name (e.g., "users.francodb").
         */
        inline std::string GetFileName() const { return file_name_; }
        
        int GetNumPages();

        /**
         * Closes and deletes the file (useful for unit tests).
         */
        void ShutDown();

        // --- NEW: SECURE METADATA MANAGEMENT ---
        // These methods handle the .francodb.meta file with magic headers
        void WriteMetadata(const std::string &data);
        bool ReadMetadata(std::string &data);
        
        // Set encryption key (if encryption is enabled)
        void SetEncryptionKey(const std::string& key) { encryption_key_ = key; encryption_enabled_ = !key.empty(); }
        bool IsEncryptionEnabled() const { return encryption_enabled_; }

    private:
        std::string file_name_;
        std::string meta_file_name_; // <--- Stores the name of the meta file
        std::mutex io_mutex_;
        std::string encryption_key_;
        bool encryption_enabled_ = false;

        // OS-Specific File Handles
#ifdef _WIN32
        HANDLE db_io_handle_;
#else
        int db_io_fd_;
#endif
    };

} // namespace francodb