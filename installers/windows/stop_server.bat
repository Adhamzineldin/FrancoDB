@echo off
REM ==============================================================================
REM FrancoDB Server Stop Script (Batch Version)
REM ==============================================================================
REM This batch script gracefully stops the FrancoDB service on Windows.
REM ==============================================================================

setlocal enabledelayedexpansion

echo FrancoDB Service Stop
echo ======================================
echo.

REM Verify we have admin privileges
net session >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] This script requires administrator privileges.
    echo Please run as Administrator.
    pause
    exit /b 1
)

REM Try to stop the service gracefully
echo [INFO] Stopping FrancoDB service...
sc stop FrancoDBService >nul 2>&1
set STOP_RESULT=%ERRORLEVEL%

if %STOP_RESULT% EQU 0 (
    echo [INFO] Stop command sent. Waiting for shutdown...
    timeout /t 10 /nobreak
) else if %STOP_RESULT% EQU 1062 (
    echo [OK] Service was already stopped
    goto :cleanup
) else (
    echo [WARN] Service stop returned code: %STOP_RESULT%
)

REM Check if processes are still running
tasklist /FI "IMAGENAME eq francodb_server.exe" 2>nul | find /I /N "francodb_server.exe" >nul
if %ERRORLEVEL% EQU 0 (
    echo [WARN] Process still running. Force terminating...
    taskkill /F /IM francodb_server.exe 2>nul
    taskkill /F /IM francodb_service.exe 2>nul
    timeout /t 2 /nobreak
)

:cleanup
echo [OK] FrancoDB stopped
echo.
echo ======================================
echo Script completed. Press any key to close.
pause >nul

