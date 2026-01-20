# FrancoDB Installers

Professional installation packages for all platforms.

## 📦 Available Installers

### 🪟 Windows
**Location**: `windows/`
- **Format**: `.exe` (Inno Setup)
- **Features**: Service installation, GUI wizard, auto-configuration
- **Output**: `FrancoDB_Setup_1.0.0.exe`
- **Docs**: [windows/README.md](windows/README.md)

**Build**:
```powershell
# Requires: Inno Setup 6.x
cd windows
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer.iss
```

---

### 🐧 Linux
**Location**: `linux/`
- **Format**: `.deb` (Debian Package)
- **Features**: Systemd service, automatic user creation, APT integration
- **Output**: `francodb_1.0.0_amd64.deb`
- **Docs**: [linux/README.md](linux/README.md)

**Build**:
```bash
cd linux
chmod +x build_deb.sh
./build_deb.sh
```

---

### 🐳 Docker
**Location**: `docker/`
- **Format**: Docker Image + Compose
- **Features**: Multi-stage build, health checks, production-ready
- **Image**: `francodb:latest`
- **Docs**: [docker/README.md](docker/README.md)

**Deploy**:
```bash
cd docker
docker-compose up -d
```

---

## 🚀 Quick Start

### Choose Your Platform

| Platform | Installer | Installation Time | Difficulty |
|----------|-----------|------------------|------------|
| Windows 10/11 | `.exe` installer | 2 minutes | ⭐ Easy |
| Ubuntu/Debian | `.deb` package | 3 minutes | ⭐⭐ Moderate |
| Docker | Docker Compose | 5 minutes | ⭐⭐ Moderate |

### Installation Guides

Detailed installation instructions: **[../INSTALLATION_GUIDE.md](../INSTALLATION_GUIDE.md)**

---

## 📋 Platform Requirements

### Windows
- Windows 7 SP1+ (x64)
- 500 MB disk space
- Administrator privileges
- .NET Framework 4.8+ (usually pre-installed)

### Linux
- Ubuntu 20.04+ / Debian 10+
- 500 MB disk space
- CMake 3.10+, GCC 7.0+
- Root access (sudo)

### Docker
- Docker 20.10+
- Docker Compose 1.29+
- 2 GB RAM
- 1 GB disk space

---

## 🏗️ Building All Installers

### Prerequisites Check

```bash
# Check CMake
cmake --version

# Check compiler
g++ --version  # Linux
cl.exe         # Windows (Visual Studio)

# Check Docker
docker --version
docker-compose --version
```

### Build All Platforms

**Windows** (PowerShell):
```powershell
# 1. Build binaries
mkdir cmake-build-release
cd cmake-build-release
cmake -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
cd ..

# 2. Build installer
cd installers\windows
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer.iss
```

**Linux** (Bash):
```bash
# Build .deb package
cd installers/linux
chmod +x build_deb.sh
./build_deb.sh
```

**Docker** (Any platform):
```bash
# Build Docker image
cd installers/docker
docker build -t francodb:latest .
```

---

## 📂 Directory Structure

```
installers/
├── README.md                    ← You are here
│
├── windows/
│   ├── installer.iss           ← Inno Setup script
│   └── README.md               ← Windows build guide
│
├── linux/
│   ├── build_deb.sh            ← .deb builder script
│   └── README.md               ← Linux build guide
│
└── docker/
    ├── Dockerfile              ← Multi-stage build
    ├── docker-compose.yml      ← Orchestration config
    └── README.md               ← Docker deployment guide
```

---

## 🎯 Output Locations

All built installers are placed in:
```
FrancoDB/Output/
├── FrancoDB_Setup_1.0.0.exe         (Windows)
├── francodb_1.0.0_amd64.deb         (Linux)
└── francodb_1.0.0_amd64.deb.md5     (Checksum)
```

Docker image: `francodb:latest` (local registry)

---

## 🔧 Troubleshooting

### Build Fails

**Windows**:
- Ensure Visual Studio or MinGW is installed
- Check CMake version (3.10+)
- Verify Inno Setup path

**Linux**:
- Install build tools: `sudo apt-get install build-essential cmake`
- Make script executable: `chmod +x build_deb.sh`
- Check GCC version: `gcc --version`

**Docker**:
- Ensure Docker daemon is running
- Check disk space: `docker system df`
- Update Docker: `docker version`

### Installation Fails

See platform-specific troubleshooting:
- [Windows Troubleshooting](windows/README.md#troubleshooting)
- [Linux Troubleshooting](linux/README.md#troubleshooting)
- [Docker Troubleshooting](docker/README.md#troubleshooting)

---

## 📝 Version Management

Update version across all installers:

**Windows** (`installer.iss`):
```ini
#define MyAppVersion "1.0.0"
```

**Linux** (`build_deb.sh`):
```bash
VERSION="1.0.0"
```

**Docker** (tag):
```bash
docker build -t francodb:1.0.0 .
```

---

## 🔐 Security

### Code Signing

**Windows**:
```powershell
signtool sign /f cert.pfx /p password FrancoDB_Setup.exe
```

**Linux**:
```bash
dpkg-sig --sign builder francodb_1.0.0_amd64.deb
```

**Docker**:
```bash
docker trust sign francodb:1.0.0
```

---

## 📊 Installer Features Comparison

| Feature | Windows | Linux | Docker |
|---------|---------|-------|--------|
| GUI Wizard | ✅ | ❌ | ❌ |
| Service Install | ✅ | ✅ | ✅ |
| Auto-Start | ✅ | ✅ | ✅ |
| Uninstaller | ✅ | ✅ | ✅ |
| Config Wizard | ✅ | ❌ | ❌ |
| PATH Integration | ✅ | ✅ | N/A |
| Upgrade Support | ✅ | ✅ | ✅ |
| Size | ~50 MB | ~30 MB | ~200 MB |

---

## 🚢 Distribution

### GitHub Releases

```bash
gh release create v1.0.0 \
  Output/FrancoDB_Setup_1.0.0.exe \
  Output/francodb_1.0.0_amd64.deb
```

### Docker Hub

```bash
docker tag francodb:latest username/francodb:1.0.0
docker push username/francodb:1.0.0
docker push username/francodb:latest
```

### Package Managers

- **Windows**: Chocolatey, WinGet
- **Linux**: APT repository, PPA
- **Docker**: Docker Hub, GitHub Container Registry

---

## 📖 Documentation

- [Installation Guide](../INSTALLATION_GUIDE.md) - Complete installation instructions
- [README](../README.md) - Project overview
- [S+ Enhancements](../S_PLUS_ENHANCEMENTS.md) - Feature documentation

---

## 💡 Tips

1. **Test installers** in clean VMs before distribution
2. **Version consistently** across all platforms
3. **Sign packages** for production distribution
4. **Provide checksums** (MD5, SHA256) for verification
5. **Document changes** in release notes

---

## 🆘 Support

- **Issues**: [GitHub Issues](https://github.com/yourusername/FrancoDB/issues)
- **Email**: dev@francodb.io
- **Docs**: [Installation Guide](../INSTALLATION_GUIDE.md)

---

**Last Updated**: January 19, 2026

**Status**: ✅ Production Ready

**Platforms**: Windows • Linux • Docker

