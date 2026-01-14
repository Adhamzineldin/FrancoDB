// main_server.cpp
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <atomic>
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

using namespace francodb;

// Global server pointer for signal handlers
static FrancoServer* g_server = nullptr;
static BufferPoolManager* g_bpm = nullptr;
static Catalog* g_catalog = nullptr;
static BufferPoolManager* g_system_bpm = nullptr;
static Catalog* g_system_catalog = nullptr;
static AuthManager* g_auth_manager = nullptr;
static std::atomic<bool> g_shutdown_requested(false);

// Save all data function (called by both signal handler and shutdown)
void SaveAllData() {
    // Use both cerr and cout, and flush immediately
    std::cerr << "\n[SHUTDOWN] Saving all data to disk..." << std::endl;
    std::cout << "\n[SHUTDOWN] Saving all data to disk..." << std::endl;
    std::cerr.flush();
    std::cout.flush();
    
    try {
        // Save all users
        if (g_auth_manager) {
            g_auth_manager->SaveUsers();
        }
        
        // Save system catalog
        if (g_system_catalog) {
            g_system_catalog->SaveCatalog();
        }
        
        // Flush system buffer pool
        if (g_system_bpm) {
            g_system_bpm->FlushAllPages();
        }
        
        // Save default catalog
        if (g_catalog) {
            g_catalog->SaveCatalog();
        }
        
        // Flush default buffer pool
        if (g_bpm) {
            g_bpm->FlushAllPages();
        }
        
        std::cerr << "[SHUTDOWN] All data saved successfully." << std::endl;
        std::cout << "[SHUTDOWN] All data saved successfully." << std::endl;
        std::cerr.flush();
        std::cout.flush();
    } catch (const std::exception& e) {
        std::cerr << "[SHUTDOWN] Error during save: " << e.what() << std::endl;
        std::cout << "[SHUTDOWN] Error during save: " << e.what() << std::endl;
        std::cerr.flush();
        std::cout.flush();
    }
}

// Crash handler - saves everything before exit
void CrashHandler(int signal) {
    std::cerr << "\n[CRASH HANDLER] Signal " << signal << " received. Saving all data..." << std::endl;
    std::cout << "\n[CRASH HANDLER] Signal " << signal << " received. Saving all data..." << std::endl;
    std::cerr.flush();
    std::cout.flush();
    
    g_shutdown_requested = true;
    SaveAllData();
    
    // Shutdown server gracefully
    if (g_server) {
        g_server->RequestShutdown();
        // Give a moment for cleanup
#ifdef _WIN32
        Sleep(1000);  // Increased to 1 second
#else
        usleep(1000000);  // Increased to 1 second
#endif
    }
    
    std::exit(1);
}

#ifdef _WIN32
// Windows-specific Ctrl+C handler
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
    // Flush output immediately
    std::cerr << "\n[WINDOWS HANDLER] Event " << dwCtrlType << " received (Ctrl+C/Close). Saving all data..." << std::endl;
    std::cout << "\n[WINDOWS HANDLER] Event " << dwCtrlType << " received (Ctrl+C/Close). Saving all data..." << std::endl;
    std::cerr.flush();
    std::cout.flush();
    
    switch (dwCtrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            g_shutdown_requested = true;
            SaveAllData();
            if (g_server) {
                g_server->RequestShutdown();
                // Give more time for cleanup
                Sleep(2000);  // Increased to 2 seconds
            }
            // Final flush before exit
            std::cerr.flush();
            std::cout.flush();
            return TRUE;
        default:
            return FALSE;
    }
}
#endif

// Exception handler for uncaught exceptions
void TerminateHandler() {
    std::cerr << "\n[TERMINATE HANDLER] Uncaught exception. Saving all data..." << std::endl;
    SaveAllData();
    if (g_server) {
        g_server->Shutdown();
    }
    std::abort();
}

int main() {
    std::cout << "==========================================" << std::endl;
    std::cout << "     FRANCO DB SERVER v2.0 (Multi-Protocol)" << std::endl;
    std::cout << "==========================================" << std::endl;

    // Set up crash handlers
    std::set_terminate(TerminateHandler);
    
#ifdef _WIN32
    // Windows-specific: Use SetConsoleCtrlHandler for better Ctrl+C handling
    if (SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
        std::cout << "[INFO] Windows Ctrl+C handler registered successfully." << std::endl;
    } else {
        std::cerr << "[WARNING] Failed to register Windows Ctrl+C handler!" << std::endl;
    }
    signal(SIGINT, CrashHandler);   // Also set signal handler as backup
    signal(SIGTERM, CrashHandler);  // Termination request
    signal(SIGABRT, CrashHandler);  // Abort signal
    std::cout << "[INFO] Signal handlers registered (SIGINT, SIGTERM, SIGABRT)." << std::endl;
#else
    // Unix/Linux: Use sigaction for more reliable signal handling
    struct sigaction sa;
    sa.sa_handler = CrashHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    std::cout << "[INFO] Signal handlers registered (SIGINT, SIGTERM, SIGSEGV, SIGABRT)." << std::endl;
#endif

    try {
        // Core setup - use absolute or fixed paths for persistence
        std::filesystem::create_directories("data");
        auto disk_manager = std::make_unique<DiskManager>("data/francodb.db");
        auto bpm = std::make_unique<BufferPoolManager>(
            BUFFER_POOL_SIZE, disk_manager.get());
        auto catalog = std::make_unique<Catalog>(bpm.get());
        // Load existing catalog if it exists
        catalog->LoadCatalog();
        
        // Store global pointers for crash handler
        g_bpm = bpm.get();
        g_catalog = catalog.get();
        
        // Server with multiple protocol support
        FrancoServer server(bpm.get(), catalog.get());
        g_server = &server;
        
        // Get system components for crash handler
        g_system_bpm = server.GetSystemBpm();
        g_system_catalog = server.GetSystemCatalog();
        g_auth_manager = server.GetAuthManager();
        
        // Configuration from file/env
        int port = net::DEFAULT_PORT;
        // Could read from config: config::GetServerPort()
        
        server.Start(port);
        
        // Server.Start() blocks, so shutdown will be handled by signal handlers
        // The server loop will exit when running_ is set to false
        
        // If shutdown was requested, save data before exit
        if (g_shutdown_requested) {
            SaveAllData();
        }
        
        // Server runs until shutdown
        // Cleanup happens automatically with smart pointers
        g_server = nullptr;
        
    } catch (const std::exception &e) {
        std::cerr << "[CRASH] Server failed: " << e.what() << std::endl;
        CrashHandler(0);
        return 1;
    } catch (...) {
        std::cerr << "[CRASH] Unknown exception occurred." << std::endl;
        CrashHandler(0);
        return 1;
    }

    return 0;
}