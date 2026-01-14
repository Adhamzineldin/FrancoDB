// main_server.cpp
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <atomic>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif
#include "network/franco_server.h"
#include "storage/disk/disk_manager.h"
#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "common/config.h"
#include "common/franco_net_config.h"
#include "common/config_manager.h"

using namespace francodb;
namespace fs = std::filesystem;

// Global server pointer for signal handlers
static FrancoServer* g_server = nullptr;
static BufferPoolManager* g_bpm = nullptr;
static Catalog* g_catalog = nullptr;
static BufferPoolManager* g_system_bpm = nullptr;
static Catalog* g_system_catalog = nullptr;
static AuthManager* g_auth_manager = nullptr;
static std::atomic<bool> g_shutdown_requested(false);

// Helper: Get the directory where the executable resides
std::string GetExecutableDir() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return fs::path(buffer).parent_path().string();
#else
    return fs::current_path().string();
#endif
}

// Helper: Ensure database file exists before DiskManager tries to open it
void CreateFileIfMissing(const std::string& path) {
    if (!fs::exists(path)) {
        std::cout << "[INFO] Database file missing. Creating: " << path << std::endl;
        std::ofstream file(path, std::ios::binary | std::ios::out);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to create database file. Check permissions for: " + path);
        }
        file.close();
    }
}

// Save all data function (called by both signal handler and shutdown)
void SaveAllData() {
    std::cerr << "\n[SHUTDOWN] Saving all data to disk..." << std::endl;
    std::cout << "\n[SHUTDOWN] Saving all data to disk..." << std::endl;
    
    try {
        if (g_auth_manager) g_auth_manager->SaveUsers();
        if (g_system_catalog) g_system_catalog->SaveCatalog();
        if (g_system_bpm) g_system_bpm->FlushAllPages();
        if (g_catalog) g_catalog->SaveCatalog();
        if (g_bpm) g_bpm->FlushAllPages();
        
        std::cout << "[SHUTDOWN] All data saved successfully." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[SHUTDOWN] Error during save: " << e.what() << std::endl;
    }
}

// Crash handler - saves everything before exit
void CrashHandler(int signal) {
    std::cout << "\n[CRASH HANDLER] Signal " << signal << " received." << std::endl;
    g_shutdown_requested = true;
    SaveAllData();
    
    if (g_server) {
        g_server->RequestShutdown();
#ifdef _WIN32
        Sleep(1000);
#else
        usleep(1000000);
#endif
    }
    std::exit(1);
}

#ifdef _WIN32
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
    g_shutdown_requested = true;
    SaveAllData();
    if (g_server) {
        g_server->RequestShutdown();
        Sleep(2000);
    }
    return TRUE;
}
#endif

void TerminateHandler() {
    std::cerr << "\n[TERMINATE HANDLER] Uncaught exception." << std::endl;
    SaveAllData();
    std::abort();
}

int main() {
    std::cout << "==========================================" << std::endl;
    std::cout << "     FRANCO DB SERVER v2.0" << std::endl;
    std::cout << "==========================================" << std::endl;

    std::set_terminate(TerminateHandler);
    
#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    signal(SIGINT, CrashHandler);
    signal(SIGTERM, CrashHandler);
    signal(SIGABRT, CrashHandler);
#else
    struct sigaction sa;
    sa.sa_handler = CrashHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
#endif

    try {
        // 1. Locate Config relative to Executable
        // This fixes the "Service can't find config" error
        std::string exe_dir = GetExecutableDir();
        fs::path config_path = fs::path(exe_dir) / "francodb.conf";
        
        auto& config = ConfigManager::GetInstance();
        
        if (fs::exists(config_path)) {
            std::cout << "[INFO] Loading config from: " << config_path << std::endl;
            config.LoadConfig(config_path.string());
        } else {
            std::cout << "[WARN] Config not found at " << config_path << ". Using defaults/interactive." << std::endl;
            // Only run interactive if we are in a console, otherwise use defaults
            if (_isatty(_fileno(stdin))) {
                config.InteractiveConfig();
                config.SaveConfig(config_path.string());
            }
        }
        
        // 2. Resolve Data Directory
        int port = config.GetPort();
        std::string raw_data_dir = config.GetDataDirectory();
        
        // If data dir is relative (starts with .. or .), resolve it relative to exe_dir
        fs::path data_dir_path;
        if (fs::path(raw_data_dir).is_relative()) {
             data_dir_path = fs::absolute(fs::path(exe_dir) / raw_data_dir);
        } else {
             data_dir_path = raw_data_dir;
        }

        std::cout << "[INFO] Data Directory: " << data_dir_path << std::endl;
        
        bool encryption_enabled = config.IsEncryptionEnabled();
        std::string encryption_key = config.GetEncryptionKey();
        
        // 3. Create Directories & Files
        try {
            fs::create_directories(data_dir_path);
        } catch (const fs::filesystem_error& e) {
            throw std::runtime_error("Cannot create data directory. Permission denied? " + std::string(e.what()));
        }
        
        // Ensure the main DB file exists
        fs::path db_path = data_dir_path / "francodb.db";
        CreateFileIfMissing(db_path.string());
        
        // 4. Initialize Components
        auto disk_manager = std::make_unique<DiskManager>(db_path.string());
        if (encryption_enabled && !encryption_key.empty()) {
            disk_manager->SetEncryptionKey(encryption_key);
        }
        
        auto bpm = std::make_unique<BufferPoolManager>(BUFFER_POOL_SIZE, disk_manager.get());
        auto catalog = std::make_unique<Catalog>(bpm.get());
        
        try {
            catalog->LoadCatalog();
        } catch (...) {
            std::cout << "[INFO] No valid catalog found. Initializing new one." << std::endl;
        }
        
        // Save immediately to ensure file structure integrity
        catalog->SaveCatalog();
        bpm->FlushAllPages();
        
        g_bpm = bpm.get();
        g_catalog = catalog.get();
        
        FrancoServer server(bpm.get(), catalog.get());
        g_server = &server;
        
        g_system_bpm = server.GetSystemBpm();
        g_system_catalog = server.GetSystemCatalog();
        g_auth_manager = server.GetAuthManager();
        
        std::cout << "[INFO] Server starting on port " << port << "..." << std::endl;
        server.Start(port);
        
        if (g_shutdown_requested) {
            SaveAllData();
        }
        
        g_server = nullptr;
        
    } catch (const std::exception &e) {
        std::cerr << "[CRASH] Server failed: " << e.what() << std::endl;
        // Check if it's a permission error
        std::string msg = e.what();
        if (msg.find("Permission denied") != std::string::npos || msg.find("Access is denied") != std::string::npos) {
            std::cerr << "[HINT] Try running as Administrator." << std::endl;
        }
        CrashHandler(0);
        return 1;
    } catch (...) {
        std::cerr << "[CRASH] Unknown exception occurred." << std::endl;
        CrashHandler(0);
        return 1;
    }

    return 0;
}