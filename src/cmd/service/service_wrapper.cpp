#include <windows.h>
#include <tchar.h>
#include <iostream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>

static TCHAR g_ServiceName[] = TEXT("FrancoDBService");
SERVICE_STATUS g_ServiceStatus;
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;
PROCESS_INFORMATION g_ServerProcess = {0};
std::atomic<bool> g_Running(false);
std::thread g_WorkerThread;

void LogDebug(const std::string& msg) {
    std::ofstream log("C:\\francodb_service_debug.txt", std::ios::app);
    if (log.is_open()) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        log << std::ctime(&now) << " - " << msg << "\n";
    }
}

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

std::string GetExeDir() {
    TCHAR buffer[MAX_PATH];
    GetModuleFileName(NULL, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path().string();
}

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

    // FIX: Pass --service flag
    std::wstring cmdLine = L"\"" + serverExe.wstring() + L"\" --service";
    std::vector<wchar_t> cmdLineBuffer(cmdLine.begin(), cmdLine.end());
    cmdLineBuffer.push_back(0);

    LogDebug("Attempting to start process...");
    
    BOOL success = CreateProcessW(NULL, cmdLineBuffer.data(), NULL, NULL, FALSE, 
        CREATE_NO_WINDOW, NULL, binDir.wstring().c_str(), &si, &g_ServerProcess);
        
    if (!success) {
        LogDebug("CreateProcess failed. Error: " + std::to_string(GetLastError()));
    }
    return success;
}

void StopServerProcess() {
    if (g_ServerProcess.hProcess == NULL) return;
    TerminateProcess(g_ServerProcess.hProcess, 1);
    CloseHandle(g_ServerProcess.hProcess);
    CloseHandle(g_ServerProcess.hThread);
    g_ServerProcess.hProcess = NULL;
}

void WorkerThread() {
    int crash_count = 0;
    auto last_crash_time = std::chrono::steady_clock::now();

    if (!StartServerProcess()) {
        g_Running = false;
        SetEvent(g_ServiceStopEvent);
        return;
    }

    while (g_Running) {
        DWORD waitResult = WaitForSingleObject(g_ServerProcess.hProcess, 500);
        
        if (waitResult == WAIT_OBJECT_0) {
            DWORD exitCode = 0;
            GetExitCodeProcess(g_ServerProcess.hProcess, &exitCode);
            
            LogDebug("Server exited with code: " + std::to_string(exitCode));

            if (exitCode == 3221225781) { 
                LogDebug("FATAL: Missing DLLs. Stopping Service.");
                g_Running = false;
                SetEvent(g_ServiceStopEvent);
                return;
            }

            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_crash_time).count();
            
            if (duration < 10) crash_count++; else crash_count = 1;
            last_crash_time = now;

            if (crash_count > 3) {
                LogDebug("CIRCUIT BREAKER TRIPPED. Giving up.");
                g_Running = false;
                SetEvent(g_ServiceStopEvent);
                return;
            }

            CloseHandle(g_ServerProcess.hProcess);
            CloseHandle(g_ServerProcess.hThread);
            ZeroMemory(&g_ServerProcess, sizeof(g_ServerProcess));
            Sleep(2000); 
            
            if (g_Running) {
                LogDebug("Restarting server...");
                if (!StartServerProcess()) {
                    g_Running = false;
                    SetEvent(g_ServiceStopEvent);
                }
            }
        }
    }
    StopServerProcess();
}

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    switch (CtrlCode) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            ReportStatus(SERVICE_STOP_PENDING, 0, 3000);
            g_Running = false;
            SetEvent(g_ServiceStopEvent);
            break;
    }
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv) {
    g_StatusHandle = RegisterServiceCtrlHandler(g_ServiceName, ServiceCtrlHandler);
    if (!g_StatusHandle) return;

    ReportStatus(SERVICE_START_PENDING, 0, 3000);
    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_ServiceStopEvent) {
        ReportStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    g_Running = true;
    LogDebug("Service Starting...");
    g_WorkerThread = std::thread(WorkerThread);

    ReportStatus(SERVICE_RUNNING, 0, 0);
    WaitForSingleObject(g_ServiceStopEvent, INFINITE);

    ReportStatus(SERVICE_STOP_PENDING, 0, 5000);
    if (g_WorkerThread.joinable()) g_WorkerThread.join();

    CloseHandle(g_ServiceStopEvent);
    ReportStatus(SERVICE_STOPPED, 0, 0);
    LogDebug("Service Stopped.");
}

int _tmain(int argc, TCHAR* argv[]) {
    SERVICE_TABLE_ENTRY ServiceTable[] = {
        {g_ServiceName, (LPSERVICE_MAIN_FUNCTION)ServiceMain},
        {NULL, NULL}
    };
    if (!StartServiceCtrlDispatcher(ServiceTable)) return 0;
    return 0;
}