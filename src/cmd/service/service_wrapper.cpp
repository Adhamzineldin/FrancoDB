// service_wrapper.cpp - Logs to {InstallDir}/log/service_debug.txt
#include <windows.h>
#include <tchar.h>
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <thread>
#include <atomic>
#include <vector>
#include <iomanip>

static TCHAR g_ServiceName[] = TEXT("FrancoDBService");
SERVICE_STATUS g_ServiceStatus;
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;
PROCESS_INFORMATION g_ServerProcess;
std::atomic<bool> g_Running(false);
std::string g_LogPath;

// Helper: Get the directory where the executable resides
std::string GetExeDir() {
    TCHAR buffer[MAX_PATH];
    GetModuleFileName(NULL, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path().string();
}

void InitLog() {
    // Exe is in .../FrancoDB/bin/
    // We want .../FrancoDB/log/
    std::filesystem::path binDir = GetExeDir();
    std::filesystem::path rootDir = binDir.parent_path();
    std::filesystem::path logDir = rootDir / "log";
    
    // Create log dir if it doesn't exist
    if (!std::filesystem::exists(logDir)) {
        std::filesystem::create_directories(logDir);
    }
    
    g_LogPath = (logDir / "service_debug.txt").string();
}

void LogDebug(const std::string& msg) {
    if (g_LogPath.empty()) InitLog();
    
    std::ofstream logFile(g_LogPath, std::ios::app);
    if (logFile.is_open()) {
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        logFile << std::put_time(&tm, "[%Y-%m-%d %H:%M:%S] ") << msg << std::endl;
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

bool StartServerProcess() {
    LogDebug("Attempting to start server process...");

    std::filesystem::path binDir = GetExeDir();
    std::filesystem::path serverExe = binDir / "francodb_server.exe";

    LogDebug("Target Server EXE: " + serverExe.string());

    if (!std::filesystem::exists(serverExe)) {
        LogDebug("ERROR: francodb_server.exe does not exist at target path!");
        return false;
    }

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    ZeroMemory(&g_ServerProcess, sizeof(g_ServerProcess));

    std::wstring cmdLine = L"\"" + serverExe.wstring() + L"\"";
    std::vector<wchar_t> cmdLineBuffer(cmdLine.begin(), cmdLine.end());
    cmdLineBuffer.push_back(0);

    BOOL result = CreateProcessW(
        NULL,
        cmdLineBuffer.data(),
        NULL, NULL, FALSE,
        CREATE_NO_WINDOW,
        NULL,
        binDir.wstring().c_str(),
        &si,
        &g_ServerProcess
    );

    if (!result) {
        DWORD error = GetLastError();
        LogDebug("CreateProcessW failed with error code: " + std::to_string(error));
        return false;
    }

    g_Running = true;
    LogDebug("Server process started. PID: " + std::to_string(g_ServerProcess.dwProcessId));
    return true;
}

void StopServerProcess() {
    if (g_ServerProcess.hProcess == NULL) return;
    g_Running = false;
    LogDebug("Stopping server process...");

    if (AttachConsole(g_ServerProcess.dwProcessId)) {
        SetConsoleCtrlHandler(NULL, TRUE);
        GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
        FreeConsole();
        
        if (WaitForSingleObject(g_ServerProcess.hProcess, 3000) == WAIT_OBJECT_0) {
            CloseHandle(g_ServerProcess.hProcess);
            CloseHandle(g_ServerProcess.hThread);
            g_ServerProcess.hProcess = NULL;
            return;
        }
    }

    TerminateProcess(g_ServerProcess.hProcess, 1);
    CloseHandle(g_ServerProcess.hProcess);
    CloseHandle(g_ServerProcess.hThread);
    g_ServerProcess.hProcess = NULL;
}

void WorkerThread() {
    if (!StartServerProcess()) {
        SetEvent(g_ServiceStopEvent);
        return;
    }

    while (g_Running) {
        if (g_ServerProcess.hProcess != NULL) {
            DWORD waitResult = WaitForSingleObject(g_ServerProcess.hProcess, 1000);
            if (waitResult == WAIT_OBJECT_0) {
                if (g_Running) {
                    DWORD exitCode = 0;
                    GetExitCodeProcess(g_ServerProcess.hProcess, &exitCode);
                    LogDebug("Server exited unexpectedly with code: " + std::to_string(exitCode));
                    LogDebug("Code 3221225781 (0xC0000135) means DLL NOT FOUND.");
                    
                    CloseHandle(g_ServerProcess.hProcess);
                    CloseHandle(g_ServerProcess.hThread);
                    ZeroMemory(&g_ServerProcess, sizeof(g_ServerProcess));
                    
                    Sleep(2000);
                    if (!StartServerProcess()) {
                         g_Running = false;
                         SetEvent(g_ServiceStopEvent);
                    }
                }
            }
        }
    }
}

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    if (CtrlCode == SERVICE_CONTROL_STOP || CtrlCode == SERVICE_CONTROL_SHUTDOWN) {
        ReportStatus(SERVICE_STOP_PENDING, 0, 3000);
        g_Running = false;
        SetEvent(g_ServiceStopEvent);
    }
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv) {
    InitLog();
    LogDebug("ServiceMain starting...");
    
    g_StatusHandle = RegisterServiceCtrlHandler(g_ServiceName, ServiceCtrlHandler);
    if (!g_StatusHandle) return;

    ReportStatus(SERVICE_START_PENDING, 0, 3000);

    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    
    std::thread worker(WorkerThread);
    worker.detach();

    ReportStatus(SERVICE_RUNNING, 0, 0);
    LogDebug("Reporting SERVICE_RUNNING.");

    WaitForSingleObject(g_ServiceStopEvent, INFINITE);

    ReportStatus(SERVICE_STOP_PENDING, 0, 0);
    StopServerProcess();
    CloseHandle(g_ServiceStopEvent);
    ReportStatus(SERVICE_STOPPED, 0, 0);
    LogDebug("Service stopped.");
}

int _tmain(int argc, TCHAR* argv[]) {
    // For console debugging
    std::cout << "[DEBUG] FrancoDB Service Wrapper" << std::endl;
    InitLog();
    
    SERVICE_TABLE_ENTRY ServiceTable[] = {
        {g_ServiceName, (LPSERVICE_MAIN_FUNCTION)ServiceMain},
        {NULL, NULL}
    };

    if (!StartServiceCtrlDispatcher(ServiceTable)) {
        std::cout << "[DEBUG] Console mode (Service Dispatcher failed)." << std::endl;
        return 0;
    }
    return 0;
}