#define NOMINMAX  // Prevent Windows.h min/max macros from conflicting with std::min/max
#include <windows.h>
#include <tchar.h>
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <algorithm>

static TCHAR g_ServiceName[] = TEXT("FrancoDBService");
SERVICE_STATUS g_ServiceStatus;
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;
HANDLE g_ShutdownEvent = INVALID_HANDLE_VALUE;
PROCESS_INFORMATION g_ServerProcess = {};
bool g_IsStopping = false;

int g_RestartCount = 0;
const int MAX_RESTARTS = 5;
auto g_LastRestartTime = std::chrono::steady_clock::now();

const DWORD GRACEFUL_SHUTDOWN_TIMEOUT_MS = 90000;

std::string GetExeDir() {
    wchar_t buffer[32767];
    GetModuleFileNameW(NULL, buffer, 32767);
    return std::filesystem::path(buffer).parent_path().string();
}

void LogDebug(const std::string &msg) {
    try {
        std::filesystem::path logDir = std::filesystem::path(GetExeDir()).parent_path() / "log";
        if (!std::filesystem::exists(logDir)) std::filesystem::create_directories(logDir);
        
        std::ofstream log(logDir / "francodb_service.log", std::ios::app);
        if (log.is_open()) {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            char timeBuf[26];
            ctime_s(timeBuf, sizeof(timeBuf), &now);
            std::string t(timeBuf);
            if (!t.empty()) t.pop_back();
            log << "[" << t << "] " << msg << std::endl;
        }
    } catch (...) {}
}

void ReportStatus(DWORD currentState, DWORD win32ExitCode, DWORD waitHint) {
    static DWORD checkPoint = 1;
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = currentState;
    g_ServiceStatus.dwWin32ExitCode = win32ExitCode;
    g_ServiceStatus.dwWaitHint = waitHint;
    g_ServiceStatus.dwControlsAccepted = (currentState == SERVICE_START_PENDING) ? 0 : SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_ServiceStatus.dwCheckPoint = (currentState == SERVICE_RUNNING || currentState == SERVICE_STOPPED) ? 0 : checkPoint++;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

bool StartServerProcess() {
    std::filesystem::path binDir = GetExeDir();
    std::filesystem::path serverExe = binDir / "francodb_server.exe";

    if (!std::filesystem::exists(serverExe)) {
        LogDebug("ERROR: Server executable not found at " + serverExe.string());
        return false;
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; 

    std::wstring cmdLine = L"\"" + serverExe.wstring() + L"\" --service";
    std::vector<wchar_t> cmdLineBuffer(cmdLine.begin(), cmdLine.end());
    cmdLineBuffer.push_back(0);

    bool success = CreateProcessW(NULL, cmdLineBuffer.data(), NULL, NULL, FALSE, 
        CREATE_NO_WINDOW, NULL, binDir.wstring().c_str(), &si, &g_ServerProcess);
    
    if (success) {
        LogDebug("Server process started successfully (PID: " + std::to_string(g_ServerProcess.dwProcessId) + ")");
    } else {
        LogDebug("ERROR: Failed to start server process. Error code: " + std::to_string(GetLastError()));
    }
    
    return success;
}

void StopServerProcess() {
    if (g_ServerProcess.hProcess == NULL) {
        LogDebug("No server process to stop");
        return;
    }

    LogDebug("Initiating graceful shutdown (timeout: " + std::to_string(GRACEFUL_SHUTDOWN_TIMEOUT_MS / 1000) + "s)");
    
    // ✅ SIGNAL THE SHUTDOWN EVENT (most reliable method)
    if (g_ShutdownEvent != INVALID_HANDLE_VALUE) {
        LogDebug("Signaling shutdown event to server...");
        SetEvent(g_ShutdownEvent);
    } else {
        LogDebug("WARNING: Shutdown event not available!");
    }

    // Wait with progress updates
    auto startTime = std::chrono::steady_clock::now();
    DWORD elapsed = 0;
    DWORD waitInterval = 10000;
    
    while (elapsed < GRACEFUL_SHUTDOWN_TIMEOUT_MS) {
        DWORD remainingTime = GRACEFUL_SHUTDOWN_TIMEOUT_MS - elapsed;
        DWORD waitTime = std::min(waitInterval, remainingTime);
        
        DWORD result = WaitForSingleObject(g_ServerProcess.hProcess, waitTime);
        
        if (result == WAIT_OBJECT_0) {
            DWORD exitCode;
            GetExitCodeProcess(g_ServerProcess.hProcess, &exitCode);
            LogDebug("✅ Server shutdown completed successfully after " + std::to_string(elapsed / 1000) + "s (exit code: " + std::to_string(exitCode) + ")");
            CloseHandle(g_ServerProcess.hProcess);
            CloseHandle(g_ServerProcess.hThread);
            g_ServerProcess.hProcess = NULL;
            return;
        }
        
        elapsed = static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime
        ).count());
        
        if (elapsed % 10000 < waitTime) {
            LogDebug("Still waiting for graceful shutdown... (" + std::to_string(elapsed / 1000) + "s / " + std::to_string(GRACEFUL_SHUTDOWN_TIMEOUT_MS / 1000) + "s)");
            ReportStatus(SERVICE_STOP_PENDING, 0, GRACEFUL_SHUTDOWN_TIMEOUT_MS - elapsed + 5000);
        }
    }

    // ⚠️ TIMEOUT REACHED
    LogDebug("❌ CRITICAL: Graceful shutdown timeout after 90 seconds");
    LogDebug("❌ Force terminating - DATABASE MAY BE CORRUPTED");
    
    TerminateProcess(g_ServerProcess.hProcess, 1);
    WaitForSingleObject(g_ServerProcess.hProcess, 5000);
    
    CloseHandle(g_ServerProcess.hProcess);
    CloseHandle(g_ServerProcess.hThread);
    g_ServerProcess.hProcess = NULL;
}

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    if (CtrlCode == SERVICE_CONTROL_STOP || CtrlCode == SERVICE_CONTROL_SHUTDOWN) {
        LogDebug("Received stop/shutdown signal from Windows");
        g_IsStopping = true;
        ReportStatus(SERVICE_STOP_PENDING, 0, GRACEFUL_SHUTDOWN_TIMEOUT_MS + 10000);
        SetEvent(g_ServiceStopEvent);
    }
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR *argv) {
    LogDebug("=== FrancoDB Service Starting ===");
    
    g_StatusHandle = RegisterServiceCtrlHandler(g_ServiceName, ServiceCtrlHandler);
    if (!g_StatusHandle) {
        LogDebug("ERROR: Failed to register service control handler");
        return;
    }
    
    ReportStatus(SERVICE_START_PENDING, 0, 3000);
    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    
    // ✅ Create shutdown event BEFORE starting server
    g_ShutdownEvent = CreateEventW(NULL, TRUE, FALSE, L"Global\\FrancoDBShutdownEvent");
    if (g_ShutdownEvent == NULL) {
        LogDebug("ERROR: Could not create shutdown event. Error: " + std::to_string(GetLastError()));
    } else {
        LogDebug("Created shutdown event successfully");
    }

    if (!StartServerProcess()) {
        LogDebug("ERROR: Failed to start server process");
        if (g_ShutdownEvent) CloseHandle(g_ShutdownEvent);
        ReportStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    // Give server time to initialize
    Sleep(2000);

    if (WaitForSingleObject(g_ServerProcess.hProcess, 100) == WAIT_OBJECT_0) {
        DWORD exitCode;
        GetExitCodeProcess(g_ServerProcess.hProcess, &exitCode);
        LogDebug("ERROR: Server process terminated immediately (exit code: " + std::to_string(exitCode) + ")");
        if (g_ShutdownEvent) CloseHandle(g_ShutdownEvent);
        ReportStatus(SERVICE_STOPPED, 1067, 0);
        return;
    }

    LogDebug("Server running normally");
    ReportStatus(SERVICE_RUNNING, 0, 0);

    while (!g_IsStopping) {
        HANDLE waitObjects[2] = { g_ServiceStopEvent, g_ServerProcess.hProcess };
        DWORD result = WaitForMultipleObjects(2, waitObjects, FALSE, INFINITE);
        
        if (result == WAIT_OBJECT_0 || g_IsStopping) {
            LogDebug("Stop event received");
            break;
        }
        
        if (result == WAIT_OBJECT_0 + 1) { 
            DWORD exitCode;
            GetExitCodeProcess(g_ServerProcess.hProcess, &exitCode);
            LogDebug("WARNING: Server process died unexpectedly (exit code: " + std::to_string(exitCode) + ")");
            
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::minutes>(now - g_LastRestartTime).count() > 10) {
                g_RestartCount = 0;
            }
            
            g_LastRestartTime = now;
            g_RestartCount++;

            if (g_RestartCount > MAX_RESTARTS) {
                LogDebug("CRITICAL: Circuit breaker triggered (" + std::to_string(g_RestartCount) + " crashes in 10 minutes)");
                break;
            }

            LogDebug("Attempting restart (" + std::to_string(g_RestartCount) + "/" + std::to_string(MAX_RESTARTS) + ")");
            CloseHandle(g_ServerProcess.hProcess);
            CloseHandle(g_ServerProcess.hThread);
            
            ResetEvent(g_ShutdownEvent);
            Sleep(3000);
            
            if (!StartServerProcess()) {
                LogDebug("ERROR: Failed to restart server");
                break;
            }
        }
    }

    LogDebug("Service stopping - initiating server shutdown");
    StopServerProcess();
    
    if (g_ShutdownEvent) CloseHandle(g_ShutdownEvent);
    
    LogDebug("=== FrancoDB Service Stopped ===");
    ReportStatus(SERVICE_STOPPED, 0, 0);
}

int _tmain(int argc, TCHAR *argv[]) {
    SERVICE_TABLE_ENTRY ServiceTable[] = { 
        {g_ServiceName, (LPSERVICE_MAIN_FUNCTION) ServiceMain}, 
        {NULL, NULL} 
    };
    return StartServiceCtrlDispatcher(ServiceTable);
}