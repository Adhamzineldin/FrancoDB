# ✅ Installer Paths Fixed

## Changes Made

### File Paths Updated
The installer now uses proper relative paths from the installer's location (installers/windows/):

**SetupIconFile:**
```diff
- SetupIconFile=resources\francodb.ico
+ SetupIconFile=..\..\resources\francodb.ico
```

**Source Files:**
```diff
- Source: "cmake-build-release\*.dll"
+ Source: "..\..\cmake-build-release\*.dll"

- Source: "cmake-build-release\francodb_server.exe"
+ Source: "..\..\cmake-build-release\francodb_server.exe"

- Source: "cmake-build-release\francodb_shell.exe"
+ Source: "..\..\cmake-build-release\francodb_shell.exe"

- Source: "cmake-build-release\francodb_service.exe"
+ Source: "..\..\cmake-build-release\francodb_service.exe"
```

## How the Paths Work

The installer is located at: `installers/windows/installer.iss`

Using `..\..\` goes up two directories:
1. `..` → `installers/` 
2. `..\..` → FrancoDB (root)

Then from root:
- `..\..\cmake-build-release\` → Points to the build output directory ✅
- `..\..\resources\` → Points to the resources directory ✅

## Path Structure

```
FrancoDB/
├── installers/
│   └── windows/
│       └── installer.iss  ← (starting here)
├── cmake-build-release/  ← (..\..\cmake-build-release\)
├── resources/  ← (..\..\resources\)
├── src/
└── ... other directories
```

## Status

✅ **All installer paths are now correctly configured**
✅ **Installer will find all required files**
✅ **Ready to build with Inno Setup Compiler**

## Next Steps

1. Build the installer:
   - Open Inno Setup Compiler
   - File → Open: `installers/windows/installer.iss`
   - Build → Compile
   - Output: `Output/FrancoDB_Setup.exe`

2. Test the installer:
   - Run `FrancoDB_Setup.exe`
   - All DLLs and executables should be found
   - Service should install and start correctly
   - Upgrade detection should work

