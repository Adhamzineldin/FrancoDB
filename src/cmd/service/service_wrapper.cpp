// service_wrapper.cpp - Fixes Windows GUI Hangs on Stop/Restart
#include <windows.h>
#include <tchar.h>
#include <iostream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <string>
#include <vector>

static TCHAR g_ServiceName[] = TEXT("FrancoDBService");
SERVICE_STATUS g_ServiceStatus;
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;
PROCESS_INFORMATION g_ServerProcess = {0};
std::atomic<bool> g_Running(false);
std::thread g_WorkerThread;

// Update Windows on our status so the GUI doesn't hang
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

// Get the directory where this executable lives
std::string GetExeDir() {
    TCHAR buffer[MAX_PATH];
    GetModuleFileName(NULL, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path().string();
}

bool StartServerProcess() {
    std::filesystem::path binDir = GetExeDir();
    std::filesystem::path serverExe = binDir / "francodb_server.exe";

    if (!std::filesystem::exists(serverExe)) return false;

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // Run hidden

    ZeroMemory(&g_ServerProcess, sizeof(g_ServerProcess));

    std::wstring cmdLine = L"\"" + serverExe.wstring() + L"\"";
    std::vector<wchar_t> cmdLineBuffer(cmdLine.begin(), cmdLine.end());
    cmdLineBuffer.push_back(0);

    return CreateProcessW(NULL, cmdLineBuffer.data(), NULL, NULL, FALSE, 
        CREATE_NO_WINDOW, NULL, binDir.wstring().c_str(), &si, &g_ServerProcess);
}

void StopServerProcess() {
    if (g_ServerProcess.hProcess == NULL) return;

    // 1. Try Graceful Shutdown (Ctrl+C)
    if (AttachConsole(g_ServerProcess.dwProcessId)) {
        SetConsoleCtrlHandler(NULL, TRUE);
        GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
        FreeConsole();
        
        // Wait 3 seconds for it to save data and close
        if (WaitForSingleObject(g_ServerProcess.hProcess, 3000) == WAIT_OBJECT_0) {
            CloseHandle(g_ServerProcess.hProcess);
            CloseHandle(g_ServerProcess.hThread);
            g_ServerProcess.hProcess = NULL;
            return;
        }
    }

    // 2. Force Kill if it's stuck (Prevents Service Hang)
    TerminateProcess(g_ServerProcess.hProcess, 1);
    CloseHandle(g_ServerProcess.hProcess);
    CloseHandle(g_ServerProcess.hThread);
    g_ServerProcess.hProcess = NULL;
}

// The loop that monitors the server
void WorkerThread() {
    if (!StartServerProcess()) {
        g_Running = false;
        SetEvent(g_ServiceStopEvent);
        return;
    }

    while (g_Running) {
        // Check if server process is still alive every 500ms
        DWORD waitResult = WaitForSingleObject(g_ServerProcess.hProcess, 500);
        
        if (waitResult == WAIT_OBJECT_0) {
            // Server crashed or exited
            if (g_Running) {
                // Restart logic
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
    
    // Cleanup when loop exits
    StopServerProcess();
}

// Handles "Stop" and "Restart" clicks from Windows
VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    switch (CtrlCode) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            ReportStatus(SERVICE_STOP_PENDING, 0, 3000);
            g_Running = false;
            // Signal the main thread to wake up
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
    g_WorkerThread = std::thread(WorkerThread);

    // Report RUNNING immediately so Windows knows we started OK
    ReportStatus(SERVICE_RUNNING, 0, 0);

    // Wait until we are told to stop
    WaitForSingleObject(g_ServiceStopEvent, INFINITE);

    // STOPPING SEQUENCE
    ReportStatus(SERVICE_STOP_PENDING, 0, 5000);
    
    if (g_WorkerThread.joinable()) {
        g_WorkerThread.join();
    }

    CloseHandle(g_ServiceStopEvent);
    ReportStatus(SERVICE_STOPPED, 0, 0);
}

int _tmain(int argc, TCHAR* argv[]) {
    SERVICE_TABLE_ENTRY ServiceTable[] = {
        {g_ServiceName, (LPSERVICE_MAIN_FUNCTION)ServiceMain},
        {NULL, NULL}
    };

    if (!StartServiceCtrlDispatcher(ServiceTable)) {
        return 0; // Console mode check
    }
    return 0;
}