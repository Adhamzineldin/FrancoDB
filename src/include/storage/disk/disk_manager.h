#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sys/stat.h>
#include <filesystem>

// OS-Specific Includes for Raw I/O
#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

#include "common/config.h"

namespace francodb {

    /**
     * DiskManager takes care of the allocation and deallocation of pages within a database.
     * It performs the physical read/write operations to the disk.
     * * "Exclusive" Feature: Enforces the .franco file extension.
     */
    class DiskManager {
    public:
        /**
         * Creates or opens a database file.
         * Automatically appends ".franco" if missing.
         * @param db_file The base name of the database.
         */
        explicit DiskManager(const std::string &db_file);

        ~DiskManager();

        /**
         * Read a specific page from the .franco file.
         */
        void ReadPage(uint32_t page_id, char *page_data);

        /**
         * Write a specific page to the .franco file.
         */
        void WritePage(uint32_t page_id, const char *page_data);
        
        void FlushLog();

        /**
         * Returns the size of the file in bytes.
         */
        int GetFileSize(const std::string &file_name);
    
        /**
         * Returns the enforced file name (e.g., "users.franco").
         */
        inline std::string GetFileName() const { return file_name_; }

        /**
         * Closes and deletes the file (useful for unit tests).
         */
        void ShutDown();

    private:
        std::string file_name_;
        std::mutex io_mutex_;

        // OS-Specific File Handles
#ifdef _WIN32
        HANDLE db_io_handle_;
#else
        int db_io_fd_;
#endif
    };

} // namespace francodb