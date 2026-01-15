#include <iostream>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <memory>

// FIX: Include winsock2.h BEFORE any other windows headers
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib") 
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
#include "common/auth_manager.h"

using namespace francodb;
namespace fs = std::filesystem;

// GLOBAL SERVER POINTER
std::unique_ptr<FrancoServer> g_Server;
std::atomic<bool> g_ShutdownInProgress(false);

std::string GetExecutableDir() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return fs::path(buffer).parent_path().string();
#else
    return fs::current_path().string();
#endif
}

void SetupServiceLogging(const std::string& exe_dir) {
    fs::path bin_path = exe_dir;
    fs::path log_dir = bin_path.parent_path() / "log";
    if (!fs::exists(log_dir)) {
        try { fs::create_directories(log_dir); } catch(...) {}
    }
    fs::path log_path = log_dir / "francodb_server.log";
    for (int i = 0; i < 5; i++) {
        FILE* fp = freopen(log_path.string().c_str(), "a", stdout);
        if (fp) {
            freopen(log_path.string().c_str(), "a", stderr);
            std::cout.setf(std::ios::unitbuf);
            std::cerr.setf(std::ios::unitbuf);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

#ifdef _WIN32
BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || 
        signal == CTRL_CLOSE_EVENT || signal == CTRL_SHUTDOWN_EVENT) {
        
        if (g_ShutdownInProgress.exchange(true)) return TRUE;

        std::cerr << "\n[SYSTEM] Graceful shutdown initiated..." << std::endl;
        
        if (g_Server) {
            g_Server->Shutdown(); 
        }
        return TRUE;
    }
    return FALSE;
}
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[CRASH] WSAStartup failed!" << std::endl;
        return 1;
    }
#endif

    bool is_service = false;
    if (argc > 1 && std::string(argv[1]) == "--service") {
        is_service = true;
    }

    std::string exe_dir = GetExecutableDir();
    
    if (is_service) {
        SetupServiceLogging(exe_dir);
    } 
    
    std::cout << "==========================================" << std::endl;
    std::cout << "     FRANCO DB SERVER v2.0 (Active)" << std::endl;
    std::cout << "==========================================" << std::endl;

#ifdef _WIN32
    if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
        std::cerr << "[WARN] Could not register console handler." << std::endl;
    }
#endif

    try {
        fs::path config_path = fs::path(exe_dir) / "francodb.conf";
        auto& config = ConfigManager::GetInstance();
        
        if (fs::exists(config_path)) {
            std::cout << "[INFO] Loading config from: " << config_path << std::endl;
            config.LoadConfig(config_path.string());
        } else {
            std::cout << "[WARN] Config not found, using defaults." << std::endl;
            config.SaveConfig(config_path.string());
        }
        
        int port = config.GetPort();
        fs::path data_dir_path = fs::absolute(fs::path(config.GetDataDirectory()));
        if (fs::path(config.GetDataDirectory()).is_relative()) {
            data_dir_path = fs::absolute(fs::path(exe_dir) / config.GetDataDirectory());
        }
        std::cout << "[INFO] Data Directory: " << data_dir_path << std::endl;
        fs::create_directories(data_dir_path);

        fs::path db_path = data_dir_path / "francodb.db";
        
        auto disk_manager = std::make_unique<DiskManager>(db_path.string());
        if (config.IsEncryptionEnabled()) disk_manager->SetEncryptionKey(config.GetEncryptionKey());
        
        auto bpm = std::make_unique<BufferPoolManager>(BUFFER_POOL_SIZE, disk_manager.get());
        auto catalog = std::make_unique<Catalog>(bpm.get());
        
        try { 
            if (catalog->GetAllTableNames().empty()) catalog->LoadCatalog(); 
        } catch (...) {}
        
        // Initialize Auth Manager (Global)
        g_auth_manager = new AuthManager(bpm.get(), catalog.get());

        std::cout << "[INFO] Initializing Server Logic..." << std::endl;
        g_Server = std::make_unique<FrancoServer>(bpm.get(), catalog.get());
        
        std::cout << "[INFO] Starting Network Listener on port " << port << "..." << std::endl;
        g_Server->Start(port);
        
        // CLEAN SHUTDOWN
        delete g_auth_manager;
        g_Server.reset(); 

    } catch (const std::exception &e) {
        std::cerr << "[CRASH] Critical Failure: " << e.what() << std::endl;
        if (g_Server) g_Server.reset();
        #ifdef _WIN32
        WSACleanup();
        #endif
        return 1;
    }

    #ifdef _WIN32
    WSACleanup();
    #endif

    return 0;
}