#include <windows.h>
#include <tchar.h>
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <ctime>

// 1. GLOBAL VARIABLES
static TCHAR g_ServiceName[] = TEXT("FrancoDBService");
SERVICE_STATUS g_ServiceStatus;
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;
PROCESS_INFORMATION g_ServerProcess = {0};
bool g_IsStopping = false; // Simple flag to prevent restart loops during shutdown

// 2. HELPER: GetExeDir
std::string GetExeDir() {
    TCHAR buffer[MAX_PATH];
    GetModuleFileName(NULL, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path().string();
}

// 3. LOGGER
void LogDebug(const std::string &msg) {
    std::string binStr = GetExeDir();
    std::filesystem::path binDir(binStr);
    std::filesystem::path logDir = binDir.parent_path() / "log";

    if (!std::filesystem::exists(logDir)) {
        try { std::filesystem::create_directories(logDir); } catch (...) { return; }
    }

    std::filesystem::path logFile = logDir / "francodb_service.log";
    std::ofstream log(logFile, std::ios::app);
    if (log.is_open()) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::string timeStr = std::ctime(&now);
        if (!timeStr.empty() && timeStr.back() == '\n') timeStr.pop_back();
        log << "[" << timeStr << "] " << msg << "\n";
    }
}

// 4. SERVICE HELPERS
void ReportStatus(DWORD currentState, DWORD win32ExitCode, DWORD waitHint) {
    static DWORD checkPoint = 1;
    g_ServiceStatus.dwCurrentState = currentState;
    g_ServiceStatus.dwWin32ExitCode = win32ExitCode;
    g_ServiceStatus.dwWaitHint = waitHint;

    if (currentState == SERVICE_START_PENDING)
        g_ServiceStatus.dwControlsAccepted = 0;
    else
        g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;

    if (currentState == SERVICE_RUNNING || currentState == SERVICE_STOPPED)
        g_ServiceStatus.dwCheckPoint = 0;
    else
        g_ServiceStatus.dwCheckPoint = checkPoint++;

    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

// 5. SERVER PROCESS MANAGER
bool StartServerProcess() {
    std::filesystem::path binDir = GetExeDir();
    std::filesystem::path serverExe = binDir / "francodb_server.exe";

    if (!std::filesystem::exists(serverExe)) {
        LogDebug("CRITICAL: Server EXE not found at " + serverExe.string());
        return false;
    }

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; 

    ZeroMemory(&g_ServerProcess, sizeof(g_ServerProcess));

    std::wstring cmdLine = L"\"" + serverExe.wstring() + L"\" --service";
    std::vector<wchar_t> cmdLineBuffer(cmdLine.begin(), cmdLine.end());
    cmdLineBuffer.push_back(0);

    LogDebug("Attempting to start process...");
    
    // CREATE_NEW_PROCESS_GROUP allows us to send CTRL+BREAK later
    BOOL success = CreateProcessW(NULL, cmdLineBuffer.data(), NULL, NULL, FALSE, 
        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, NULL, binDir.wstring().c_str(), &si, &g_ServerProcess);
        
    if (!success) {
        LogDebug("CreateProcess failed. Error: " + std::to_string(GetLastError()));
    }
    return success;
}

void StopServerProcess() {
    if (g_ServerProcess.hProcess == NULL) return;

    LogDebug("Requesting Graceful Shutdown...");
    
    // 1. Send CTRL+BREAK (Ask nicely)
    if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, g_ServerProcess.dwProcessId)) {
        LogDebug("Failed to send signal. Error: " + std::to_string(GetLastError()));
    }

    // 2. Wait up to 3 seconds
    if (WaitForSingleObject(g_ServerProcess.hProcess, 3000) == WAIT_TIMEOUT) {
        LogDebug("Server stuck. Performing Hard Kill.");
        TerminateProcess(g_ServerProcess.hProcess, 1);
    } else {
        LogDebug("Server shut down gracefully.");
    }

    CloseHandle(g_ServerProcess.hProcess);
    CloseHandle(g_ServerProcess.hThread);
    ZeroMemory(&g_ServerProcess, sizeof(g_ServerProcess));
}

// 6. MAIN SERVICE ENTRY POINTS
VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    switch (CtrlCode) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            g_IsStopping = true;
            ReportStatus(SERVICE_STOP_PENDING, 0, 3000);
            SetEvent(g_ServiceStopEvent); // Signal Main Loop to exit
            break;
    }
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR *argv) {
    g_StatusHandle = RegisterServiceCtrlHandler(g_ServiceName, ServiceCtrlHandler);
    if (!g_StatusHandle) return;

    // A. INITIALIZE
    ReportStatus(SERVICE_START_PENDING, 0, 3000);
    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_ServiceStopEvent) {
        ReportStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    // B. START PROCESS (CRASH GUARD LOGIC)
    LogDebug("Service Starting...");
    if (!StartServerProcess()) {
        LogDebug("Failed to launch exe. Aborting.");
        ReportStatus(SERVICE_STOPPED, 1, 0); // Error code 1
        return;
    }

    // C. WAIT 1 SECOND (CHECK FOR IMMEDIATE CRASH)
    // This prevents the "Stuck in Starting" bug. If it crashes now, we fail the start.
    if (WaitForSingleObject(g_ServerProcess.hProcess, 1000) == WAIT_OBJECT_0) {
        DWORD exitCode = 0;
        GetExitCodeProcess(g_ServerProcess.hProcess, &exitCode);
        LogDebug("Server crashed on startup (Code: " + std::to_string(exitCode) + "). Service Aborted.");
        
        CloseHandle(g_ServerProcess.hProcess);
        CloseHandle(g_ServerProcess.hThread);
        ReportStatus(SERVICE_STOPPED, exitCode, 0); 
        return;
    }

    // D. REPORT RUNNING (Only now are we safe)
    ReportStatus(SERVICE_RUNNING, 0, 0);

    // E. MONITORING LOOP (Replaces WorkerThread)
    // We wait for EITHER the Stop Event OR the Process dying
    HANDLE waitObjects[2] = { g_ServiceStopEvent, g_ServerProcess.hProcess };
    
    while (!g_IsStopping) {
        DWORD waitResult = WaitForMultipleObjects(2, waitObjects, FALSE, INFINITE);
        
        // CASE 1: Service Stop Requested
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }
        
        // CASE 2: Server Process Died
        if (waitResult == WAIT_OBJECT_0 + 1) {
             DWORD exitCode = 0;
             GetExitCodeProcess(g_ServerProcess.hProcess, &exitCode);
             LogDebug("Server crashed (Code: " + std::to_string(exitCode) + "). Restarting...");

             // Cleanup old handles
             CloseHandle(g_ServerProcess.hProcess);
             CloseHandle(g_ServerProcess.hThread);
             ZeroMemory(&g_ServerProcess, sizeof(g_ServerProcess));

             // Circuit Breaker / Restart Delay
             Sleep(2000); 

             if (g_IsStopping) break;

             // Restart
             if (!StartServerProcess()) {
                 LogDebug("Failed to restart. Service Stopping.");
                 g_IsStopping = true;
             }
             
             // Update the handle in our wait array
             waitObjects[1] = g_ServerProcess.hProcess;
        }
    }

    // F. SHUTDOWN
    LogDebug("Service Stopping...");
    StopServerProcess();
    
    ReportStatus(SERVICE_STOPPED, 0, 0);
    CloseHandle(g_ServiceStopEvent);
    LogDebug("Service Stopped.");
}

int _tmain(int argc, TCHAR *argv[]) {
    SERVICE_TABLE_ENTRY ServiceTable[] = {
        {g_ServiceName, (LPSERVICE_MAIN_FUNCTION) ServiceMain},
        {NULL, NULL}
    };
    if (!StartServiceCtrlDispatcher(ServiceTable)) return 0;
    return 0;
}