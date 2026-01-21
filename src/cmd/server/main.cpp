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

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
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

using namespace francodb;
namespace fs = std::filesystem;

// Global pointers for resource management
std::unique_ptr<FrancoServer> g_Server;
std::atomic<bool> g_ShutdownRequested(false);

std::string GetExecutableDir() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return fs::path(buffer).parent_path().string();
#else
    return fs::current_path().string();
#endif
}

void SetupServiceLogging(const std::string &exe_dir) {
    fs::path bin_path = exe_dir;
    fs::path log_dir = bin_path.parent_path() / "log";
    if (!fs::exists(log_dir)) {
        try { fs::create_directories(log_dir); } catch (...) {
        }
    }
    fs::path log_path = log_dir / "francodb_server.log";

    // Force open log file (Retry logic)
    for (int i = 0; i < 3; i++) {
        FILE *fp = freopen(log_path.string().c_str(), "a", stdout);
        if (fp) {
            freopen(log_path.string().c_str(), "a", stderr);
            std::cout.setf(std::ios::unitbuf);
            std::cerr.setf(std::ios::unitbuf);
            std::cout << "\n=== NEW SERVER SESSION ===" << std::endl;
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// Triggers the shutdown sequence safely
// This is the ONLY thing the background thread calls.
void TriggerShutdown() {
    if (g_ShutdownRequested.exchange(true)) return; // Already triggered

    std::cout << "[SHUTDOWN] Signal received. Interrupting network listener..." << std::endl;

    // CRITICAL: We do NOT flush here. We just break the network loop.
    // The Main Thread is stuck in 'accept()'. This forces it to error out and continue.
    if (g_Server) {
        g_Server->Stop();
    }
}

#ifdef _WIN32
BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT ||
        signal == CTRL_CLOSE_EVENT || signal == CTRL_SHUTDOWN_EVENT) {
        TriggerShutdown();
        return TRUE;
    }
    return FALSE;
}

void ShutdownEventMonitor() {
    HANDLE hEvent = OpenEventW(SYNCHRONIZE, FALSE, L"Global\\FrancoDBShutdownEvent");
    if (hEvent) {
        std::cout << "[INFO] Monitoring shutdown event..." << std::endl;
        WaitForSingleObject(hEvent, INFINITE);
        std::cout << "[SYSTEM] ✅ Shutdown event received from service!" << std::endl;
        TriggerShutdown();
        CloseHandle(hEvent);
    }
}
#endif

int main(int argc, char *argv[]) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    bool is_service = (argc > 1 && std::string(argv[1]) == "--service");
    std::string exe_dir = GetExecutableDir();
    if (is_service) SetupServiceLogging(exe_dir);

    std::cout << "==========================================" << std::endl;
    std::cout << "     FRANCO DB SERVER v2.0 (Active)" << std::endl;
    std::cout << "==========================================" << std::endl;

#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    std::thread monitorThread;
    if (is_service) {
        monitorThread = std::thread(ShutdownEventMonitor);
        monitorThread.detach(); // It's fine to detach, it just signals g_Server
    }
#endif

    try {
        // 1. CONFIG
        auto &config = ConfigManager::GetInstance();
        fs::path config_path = fs::path(exe_dir) / "francodb.conf";

        if (fs::exists(config_path)) config.LoadConfig(config_path.string());
        else config.SaveConfig(config_path.string());

        fs::path data_dir = fs::absolute(fs::path(exe_dir) / config.GetDataDirectory());
        fs::create_directories(data_dir);

        // 2. INITIALIZE DB COMPONENTS
        std::cout << "[INFO] Initializing DB components..." << std::endl;
        std::filesystem::path system_dir = std::filesystem::path(data_dir) / "system";


        if (!std::filesystem::exists(system_dir)) {
            std::filesystem::create_directories(system_dir);
        }


        auto disk_manager = std::make_unique<DiskManager>((system_dir / "disk_manager.francodb").string());
        if (config.IsEncryptionEnabled()) disk_manager->SetEncryptionKey(config.GetEncryptionKey());

        auto bpm = std::make_unique<BufferPoolManager>(BUFFER_POOL_SIZE, disk_manager.get());
        auto catalog = std::make_unique<Catalog>(bpm.get());
        
        auto log_manager = std::make_unique<LogManager>((system_dir / "franco.log").string());
        std::cout << "[INFO] Log Manager initialized at: " << (system_dir / "franco.log").string() << std::endl;

        if (catalog->GetAllTableNames().empty()) {
            try { catalog->LoadCatalog(); } catch (...) {
            }
        }

        // 3. START SERVER
        g_Server = std::make_unique<FrancoServer>(bpm.get(), catalog.get(), log_manager.get());
        std::cout << "[READY] FrancoDB Server listening on port " << config.GetPort() << "..." << std::endl;

        // === BLOCKING CALL === 
        // This holds the Main Thread until TriggerShutdown() calls g_Server->Stop()
        try {
            g_Server->Start(config.GetPort());
        } catch (...) {
            // Start() will throw or return when Stop() is called. This is expected.
        }

        // === SHUTDOWN SEQUENCE (MAIN THREAD ONLY) ===
        // We are guaranteed to be on the Main Thread here.
        // No other thread is touching the DB.

        std::cout << "[SHUTDOWN] Network stopped. Beginning synchronous flush..." << std::endl;

        // 4. FLUSH BUFFERS
        if (bpm) {
            std::cout << "[SHUTDOWN] Flushing Buffer Pool..." << std::endl;
            bpm->FlushAllPages();
            std::cout << "[SHUTDOWN] Buffer Pool Flushed." << std::endl;
        }

        if (catalog) {
            std::cout << "[SHUTDOWN] Saving Catalog..." << std::endl;
            catalog->SaveCatalog();
        }

        
        
        
        log_manager->StopFlushThread();
        // 5. DESTROY SERVER LOGIC
        // Destroy server first so no new requests try to use the DB
        g_Server.reset();

        // 6. DESTROY DB COMPONENTS
        // Catalog first, then BPM, then DiskManager (Reverse order of creation)
        catalog.reset();
        bpm.reset();
        disk_manager.reset();

        std::cout << "[SHUTDOWN] All resources destroyed safely." << std::endl;

        if (g_auth_manager) {
            delete g_auth_manager;
            g_auth_manager = nullptr;
        }
    } catch (const std::exception &e) {
        std::cerr << "[CRASH] Critical Failure: " << e.what() << std::endl;
        return 1;
    }

#ifdef _WIN32
    WSACleanup();
#endif
    std::cout << "[SHUTDOWN] Server exited cleanly" << std::endl;
    return 0;
}
