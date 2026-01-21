# ==============================================================================
# FrancoDB Server Management Scripts
# ==============================================================================
# PowerShell functions to manage FrancoDB service

# Requires Admin privileges
#Requires -RunAsAdministrator

# ==============================================================================
# START FUNCTION
# ==============================================================================
function Start-FrancoDBServer {
    <#
    .SYNOPSIS
    Starts the FrancoDB service.
    
    .DESCRIPTION
    Attempts to start the FrancoDB Windows service. If the service is not found,
    it tries to start the executable directly.
    
    .EXAMPLE
    Start-FrancoDBServer
    #>
    
    Write-Host "FrancoDB Service Startup" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    
    $serviceName = "FrancoDBService"
    $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    
    if ($service) {
        Write-Host "[INFO] Service found: $serviceName" -ForegroundColor Yellow
        
        if ($service.Status -eq 'Running') {
            Write-Host "[OK] FrancoDB is already running!" -ForegroundColor Green
            return $true
        }
        
        Write-Host "[INFO] Starting service..." -ForegroundColor Yellow
        Start-Service -Name $serviceName -ErrorAction SilentlyContinue
        
        # Wait for service to start (max 30 seconds)
        $waited = 0
        while ($waited -lt 30) {
            Start-Sleep -Seconds 1
            $service = Get-Service -Name $serviceName
            if ($service.Status -eq 'Running') {
                Write-Host "[OK] FrancoDB started successfully!" -ForegroundColor Green
                return $true
            }
            $waited++
        }
        
        Write-Host "[WARN] Service took too long to start. Check francodb.conf" -ForegroundColor Yellow
        return $false
    }
    else {
        Write-Host "[WARN] Service not found. Attempting direct startup..." -ForegroundColor Yellow
        
        $serverExe = Join-Path -Path $PSScriptRoot -ChildPath "francodb_server.exe"
        
        if (Test-Path $serverExe) {
            Write-Host "[INFO] Starting: $serverExe" -ForegroundColor Yellow
            & $serverExe
            Write-Host "[OK] FrancoDB started" -ForegroundColor Green
            return $true
        }
        else {
            Write-Host "[ERROR] Server executable not found: $serverExe" -ForegroundColor Red
            return $false
        }
    }
}

# ==============================================================================
# STOP FUNCTION
# ==============================================================================
function Stop-FrancoDBServer {
    <#
    .SYNOPSIS
    Stops the FrancoDB service.
    
    .DESCRIPTION
    Gracefully stops the FrancoDB service. If it doesn't stop within 60 seconds,
    the process is forcefully terminated.
    
    .PARAMETER Force
    Force terminate the process without waiting for graceful shutdown
    
    .EXAMPLE
    Stop-FrancoDBServer
    Stop-FrancoDBServer -Force
    #>
    
    param(
        [switch]$Force
    )
    
    Write-Host "FrancoDB Service Stop" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    
    $serviceName = "FrancoDBService"
    $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    
    if ($service) {
        Write-Host "[INFO] Service found. Stopping..." -ForegroundColor Yellow
        Stop-Service -Name $serviceName -Force:$Force -ErrorAction SilentlyContinue
        
        if (-not $Force) {
            # Wait for graceful shutdown
            $waited = 0
            while ($waited -lt 60) {
                Start-Sleep -Seconds 1
                $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
                if ($service.Status -eq 'Stopped') {
                    Write-Host "[OK] Service stopped" -ForegroundColor Green
                    return $true
                }
                if ($waited % 10 -eq 0) {
                    Write-Host "[INFO] Waiting for shutdown... ($waited`s)" -ForegroundColor Yellow
                }
                $waited++
            }
            Write-Host "[WARN] Graceful stop timeout. Force terminating..." -ForegroundColor Yellow
        }
    }
    
    # Kill any remaining processes
    $procs = Get-Process -Name "francodb_server", "francodb_service" -ErrorAction SilentlyContinue
    if ($procs) {
        Write-Host "[INFO] Terminating processes..." -ForegroundColor Yellow
        $procs | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 1
    }
    
    # Verify
    $remaining = Get-Process -Name "francodb_server", "francodb_service" -ErrorAction SilentlyContinue
    if ($remaining) {
        Write-Host "[ERROR] Could not terminate all processes" -ForegroundColor Red
        return $false
    }
    else {
        Write-Host "[OK] All FrancoDB processes stopped" -ForegroundColor Green
        return $true
    }
}

# ==============================================================================
# STATUS FUNCTION
# ==============================================================================
function Get-FrancoDBStatus {
    <#
    .SYNOPSIS
    Gets the status of FrancoDB service.
    
    .EXAMPLE
    Get-FrancoDBStatus
    #>
    
    $serviceName = "FrancoDBService"
    $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    
    Write-Host "FrancoDB Service Status" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    
    if ($service) {
        $statusColor = if ($service.Status -eq 'Running') { 'Green' } else { 'Red' }
        Write-Host "Service Status: $($service.Status)" -ForegroundColor $statusColor
        Write-Host "Service Name: $($service.Name)" -ForegroundColor White
        Write-Host "Display Name: $($service.DisplayName)" -ForegroundColor White
        Write-Host "Start Type: $($service.StartType)" -ForegroundColor White
    }
    else {
        Write-Host "Service Status: NOT FOUND" -ForegroundColor Red
    }
    
    # Check processes
    Write-Host ""
    $procs = Get-Process -Name "francodb_server", "francodb_service" -ErrorAction SilentlyContinue
    if ($procs) {
        Write-Host "Running Processes:" -ForegroundColor Yellow
        $procs | Select-Object -Property Name, Id, Handles, WorkingSet | Format-Table
    }
    else {
        Write-Host "No FrancoDB processes running" -ForegroundColor Yellow
    }
}

# ==============================================================================
# RESTART FUNCTION
# ==============================================================================
function Restart-FrancoDBServer {
    <#
    .SYNOPSIS
    Restarts the FrancoDB service.
    
    .EXAMPLE
    Restart-FrancoDBServer
    #>
    
    Write-Host "FrancoDB Service Restart" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    
    Write-Host "[INFO] Stopping FrancoDB..." -ForegroundColor Yellow
    Stop-FrancoDBServer
    
    Write-Host ""
    Start-Sleep -Seconds 2
    
    Write-Host "[INFO] Starting FrancoDB..." -ForegroundColor Yellow
    Start-FrancoDBServer
}

# ==============================================================================
# EXPORT FUNCTIONS
# ==============================================================================
Export-ModuleMember -Function @(
    'Start-FrancoDBServer',
    'Stop-FrancoDBServer',
    'Get-FrancoDBStatus',
    'Restart-FrancoDBServer'
)

# ==============================================================================
# QUICK REFERENCE
# ==============================================================================
# Usage in PowerShell:
#
#   . .\FrancoDBUtils.ps1              # Load the script
#   Start-FrancoDBServer                # Start service
#   Stop-FrancoDBServer                 # Stop service
#   Restart-FrancoDBServer              # Restart service
#   Get-FrancoDBStatus                  # Check status
#
# ==============================================================================

