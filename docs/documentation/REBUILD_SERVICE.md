# Rebuild Service to Fix Error 1053

## Problem
Service shows as running in installer but fails with Error 1053 when started manually. This means the service executable needs to be rebuilt with the latest fixes.

## Solution

### Step 1: Rebuild the Service Executable
```powershell
cd G:\University\Graduation\FrancoDB
cmake --build cmake-build-debug --target francodb_service --config Debug
```

### Step 2: Stop and Remove Old Service
```cmd
sc stop FrancoDBService
sc delete FrancoDBService
```

### Step 3: Copy New Service Executable
```cmd
copy cmake-build-debug\francodb_service.exe "C:\Program Files\FrancoDB\bin\"
```

### Step 4: Recreate and Start Service
```cmd
sc create FrancoDBService binPath= "C:\Program Files\FrancoDB\bin\francodb_service.exe" start= auto
sc start FrancoDBService
```

### Step 5: Verify
```cmd
sc query FrancoDBService
```

Should show: `STATE: 4 RUNNING`

## What Was Fixed

1. **Process Verification**: Service now verifies the server process actually started before reporting RUNNING
2. **Quick Crash Detection**: Detects if server process exits immediately (within 100ms)
3. **Better Error Handling**: Reports specific error codes if process fails to start
4. **Proper Status Reporting**: Reports RUNNING only after confirming process is alive

## Alternative: Reinstall Using Installer

1. Uninstall FrancoDB from Control Panel
2. Rebuild service: `cmake --build cmake-build-debug --target francodb_service`
3. Recompile installer
4. Run installer again

## Check Event Viewer

If service still fails, check Event Viewer:
```powershell
Get-EventLog -LogName Application -Source "FrancoDBService" -Newest 10
```

Look for error messages about:
- Server process not found
- Process exited immediately
- Invalid process handle
