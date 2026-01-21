@echo off
REM ==============================================================================
REM FrancoDB Server Start Script (Batch Version)
REM ==============================================================================
REM This batch script starts the FrancoDB service on Windows.
REM ==============================================================================

setlocal enabledelayedexpansion

echo FrancoDB Service Startup
echo ======================================
echo.

REM Get the script directory
set SCRIPT_DIR=%~dp0

REM Verify we have admin privileges
net session >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] This script requires administrator privileges.
    echo Please run as Administrator.
    pause
    exit /b 1
)

REM Try to start the service
echo [INFO] Checking service status...
sc query FrancoDBService >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [INFO] Service found. Starting FrancoDB...
    sc start FrancoDBService >nul 2>&1
    if %ERRORLEVEL% EQU 0 (
        echo [OK] Service started successfully!
        goto :end
    ) else if %ERRORLEVEL% EQU 1056 (
        echo [OK] Service is already running
        goto :end
    ) else (
        echo [ERROR] Failed to start service (Error Code: %ERRORLEVEL%)
        goto :end
    )
) else (
    echo [WARN] Service not found. Trying to start executable directly...
    if exist "%SCRIPT_DIR%francodb_server.exe" (
        echo [INFO] Starting FrancoDB executable...
        start "" "%SCRIPT_DIR%francodb_server.exe"
        echo [OK] FrancoDB started
    ) else (
        echo [ERROR] FrancoDB executable not found
        echo Expected location: %SCRIPT_DIR%francodb_server.exe
    )
)

:end
echo.
echo ======================================
echo Script completed. Press any key to close.
pause >nul

