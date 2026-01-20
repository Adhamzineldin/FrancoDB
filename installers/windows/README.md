# FrancoDB Windows Installer

This folder contains the Windows installer configuration for FrancoDB.

## 📦 Contents

- **installer.iss** - Inno Setup script for creating Windows installer
- **README.md** - This file

## 🔧 Prerequisites

1. **Inno Setup 6.x** - Download from https://jrsoftware.org/isdl.php
2. **Built binaries** - Must compile FrancoDB first in `cmake-build-release/`

## 🏗️ Building the Installer

### Step 1: Build FrancoDB

```powershell
# From project root
mkdir cmake-build-release
cd cmake-build-release
cmake -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
cd ..
```

### Step 2: Verify Binaries

Make sure these files exist in `cmake-build-release/`:
- `francodb_server.exe`
- `francodb_shell.exe`
- `francodb_service.exe`
- Any required `.dll` files

### Step 3: Compile Installer

**Option A: Using Inno Setup GUI**
1. Navigate to `installers/windows/` folder
2. Open `installer.iss` in Inno Setup Compiler
3. Click **Build** > **Compile**
4. Installer will be created in `../../Output/` folder

**Option B: Using Command Line**
```powershell
cd installers\windows
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer.iss
```

Output: `../../Output/FrancoDB_Setup_1.0.0.exe`

## 📋 Installer Features

✅ **Service Installation** - Installs as Windows service
✅ **Configuration Wizard** - Interactive setup for:
  - Server port
  - Root credentials
  - Encryption options
✅ **PATH Integration** - Adds to system PATH
✅ **Protocol Handler** - Registers `maayn://` protocol
✅ **Start Menu Shortcuts** - Desktop and menu items
✅ **Upgrade Support** - Preserves config on upgrade
✅ **Clean Uninstall** - Optional data deletion

## 🎯 Output

After compilation, find the installer at:
```
FrancoDB/Output/FrancoDB_Setup_1.0.0.exe
```

## ⚙️ Customization

Edit `installer.iss` to customize:

```ini
[Setup]
AppVersion=1.0.0              ; Change version
DefaultDirName={autopf}\FrancoDB  ; Change install path
OutputBaseFilename=FrancoDB_Setup_{#MyAppVersion}  ; Output name
```

## 🐛 Troubleshooting

**Issue**: Build fails with "Source file not found"
- Solution: Ensure binaries are compiled in `cmake-build-release/`
- Check: File paths in `[Files]` section match your build output

**Issue**: Service won't start after install
- Solution: Check `francodb.conf` is generated correctly
- Verify: Port 2501 is not in use

**Issue**: Missing DLL errors
- Solution: Copy all DLLs from build directory to installer
- Check: MinGW DLLs if using MinGW compiler

## 📝 Testing

Test the installer before distribution:

1. **Install test**
   ```powershell
   FrancoDB_Setup_1.0.0.exe /SILENT /LOG="install.log"
   ```

2. **Verify installation**
   ```powershell
   net start FrancoDBService
   francodb --version
   ```

3. **Uninstall test**
   ```powershell
   "C:\Program Files\FrancoDB\unins000.exe" /SILENT
   ```

## 🔐 Code Signing (Optional)

For production, sign the installer:

```powershell
signtool sign /f certificate.pfx /p password /t http://timestamp.digicert.com FrancoDB_Setup_1.0.0.exe
```

## 📚 Documentation

- Main installer config: `installer.iss`
- Installation guide: `../../INSTALLATION_GUIDE.md`
- Project README: `../../README.md`

## 🚀 Distribution

Upload the installer to:
- GitHub Releases
- Official website
- Package managers (Chocolatey, WinGet)

---

**Last Updated**: January 19, 2026

