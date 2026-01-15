#include <iostream>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
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

static FrancoServer* g_server = nullptr;
static BufferPoolManager* g_bpm = nullptr;
static Catalog* g_catalog = nullptr;
static BufferPoolManager* g_system_bpm = nullptr;
static Catalog* g_system_catalog = nullptr;
static AuthManager* g_auth_manager = nullptr;
static std::atomic<bool> g_shutdown_requested(false);

std::string GetExecutableDir() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return fs::path(buffer).parent_path().string();
#else
    return fs::current_path().string();
#endif
}

// FIX: Robust Logging Setup with Retry Logic
void SetupServiceLogging(const std::string& exe_dir) {
    fs::path log_path = fs::path(exe_dir) / "francodb_server.log";
    
    // Try up to 5 times to grab the file lock (fixes Fast Restart crashes)
    for (int i = 0; i < 5; i++) {
        FILE* fp = freopen(log_path.string().c_str(), "a", stdout);
        if (fp) {
            freopen(log_path.string().c_str(), "a", stderr);
            // Force "Unit Buffering" (Flush after every insertion)
            std::cout.setf(std::ios::unitbuf);
            std::cerr.setf(std::ios::unitbuf);
            return;
        }
        // Wait 100ms before retrying
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool IsFileCorrupt(const std::string& path) {
    if (!fs::exists(path)) return false; 
    return fs::file_size(path) == 0;     
}

void SaveAllData() {
    std::cerr << "\n[SHUTDOWN] Saving all data to disk..." << std::endl;
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

void CrashHandler(int signal) {
    std::cout << "\n[CRASH HANDLER] Signal " << signal << " received." << std::endl;
    g_shutdown_requested = true;
    SaveAllData();
    if (g_server) g_server->RequestShutdown();
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

int main(int argc, char* argv[]) {
    // 1. CHECK ARGUMENTS (New logic for CLion support)
    bool is_service = false;
    if (argc > 1 && std::string(argv[1]) == "--service") {
        is_service = true;
    }

    std::string exe_dir = GetExecutableDir();
    
    // 2. SETUP LOGGING (Only redirect if running as Service)
    if (is_service) {
        SetupServiceLogging(exe_dir);
        std::cout << "==========================================" << std::endl;
        std::cout << "     FRANCO DB SERVER v2.0 (Service Mode)" << std::endl;
        std::cout << "==========================================" << std::endl;
    } else {
        std::cout << "==========================================" << std::endl;
        std::cout << "     FRANCO DB SERVER v2.0 (Console Mode)" << std::endl;
        std::cout << "==========================================" << std::endl;
    }

    std::set_terminate(TerminateHandler);
#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    signal(SIGINT, CrashHandler);
    signal(SIGTERM, CrashHandler);
#endif

    try {
        // 3. Load Config
        fs::path config_path = fs::path(exe_dir) / "francodb.conf";
        auto& config = ConfigManager::GetInstance();
        
        if (fs::exists(config_path)) {
            std::cout << "[INFO] Loading config from: " << config_path << std::endl;
            config.LoadConfig(config_path.string());
        } else {
            std::cout << "[WARN] Config not found, using defaults." << std::endl;
            config.SaveConfig(config_path.string());
        }
        
        // 4. Resolve Data Directory
        int port = config.GetPort();
        fs::path data_dir_path = fs::absolute(fs::path(config.GetDataDirectory()));
        // If config path was relative, anchor it to exe dir
        if (fs::path(config.GetDataDirectory()).is_relative()) {
            data_dir_path = fs::absolute(fs::path(exe_dir) / config.GetDataDirectory());
        }

        std::cout << "[INFO] Data Directory: " << data_dir_path << std::endl;
        
        // 5. Create Directory & Verify Files
        fs::create_directories(data_dir_path);
        fs::path db_path = data_dir_path / "francodb.db";

        if (IsFileCorrupt(db_path.string())) {
            std::cout << "[WARN] Corrupt DB file detected. Resetting..." << std::endl;
            fs::remove(db_path);
        }

        // 6. Initialize Engine
        std::cout << "[INFO] Initializing Engine..." << std::endl;
        auto disk_manager = std::make_unique<DiskManager>(db_path.string());
        if (config.IsEncryptionEnabled()) {
            disk_manager->SetEncryptionKey(config.GetEncryptionKey());
        }
        
        auto bpm = std::make_unique<BufferPoolManager>(BUFFER_POOL_SIZE, disk_manager.get());
        auto catalog = std::make_unique<Catalog>(bpm.get());
        
        try { catalog->LoadCatalog(); } 
        catch (...) { std::cout << "[INFO] New Catalog Initialized." << std::endl; }
        
        catalog->SaveCatalog();
        bpm->FlushAllPages();
        
        g_bpm = bpm.get();
        g_catalog = catalog.get();
        
        // 7. Initialize Server
        std::cout << "[INFO] Initializing Server Logic..." << std::endl;
        FrancoServer server(bpm.get(), catalog.get());
        g_server = &server;
        
        g_system_bpm = server.GetSystemBpm();
        g_system_catalog = server.GetSystemCatalog();
        g_auth_manager = server.GetAuthManager();
        
        // 8. START LISTENING
        std::cout << "[INFO] Starting Network Listener on port " << port << "..." << std::endl;
        
        try {
            server.Start(port);
        } catch (const std::exception& e) {
            std::cerr << "[FATAL] Network Error: " << e.what() << std::endl;
            throw; 
        }
        
        if (g_shutdown_requested) {
            SaveAllData();
        }
        g_server = nullptr;

    } catch (const std::exception &e) {
        std::cerr << "[CRASH] Critical Failure: " << e.what() << std::endl;
        CrashHandler(0);
        return 1;
    } catch (...) {
        std::cerr << "[CRASH] Unknown Critical Failure." << std::endl;
        CrashHandler(0);
        return 1;
    }

    return 0;
}