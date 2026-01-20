# FrancoDB Installation Guide

## 📦 Installation Methods

FrancoDB provides multiple installation options for different platforms.

> **Installer Files**: All installer configurations are located in the **`installers/`** folder:
> - **Windows**: `installers/windows/` - Inno Setup (.exe)
> - **Linux**: `installers/linux/` - Debian package (.deb)  
> - **Docker**: `installers/docker/` - Dockerfile and compose
>
> See [installers/README.md](installers/README.md) for build instructions.

---

## 🪟 Windows Installation

### Requirements
- Windows 7 or later (x64)
- Administrator privileges
- 500 MB free disk space
- CMake 3.10+ (for building from source)

### Using Pre-built Installer (.exe)

> **Build Instructions**: See [installers/windows/README.md](installers/windows/README.md)

1. **Download** `FrancoDB_Setup_1.0.0.exe` from `Output/` folder
2. **Run** the installer with administrator privileges
3. **Follow** the wizard steps:
   - Select installation directory
   - Configure server port (default: 2501)
   - Set root credentials
   - Choose encryption options
4. **Complete** the installation
5. **Start** the service:
   ```powershell
   net start FrancoDBService
   ```

### Post-Installation

- **Configuration file**: `C:\Program Files\FrancoDB\bin\francodb.conf`
- **Data directory**: `C:\Program Files\FrancoDB\data`
- **Logs**: `C:\Program Files\FrancoDB\log`
- **Connect**: Open Command Prompt and type `francodb`

### Troubleshooting

**Issue**: Service won't start
- Solution: Check `francodb.conf` for valid configuration
- Solution: Ensure port is not in use: `netstat -ano | findstr :2501`

**Issue**: Cannot connect after installation
- Solution: Check Windows Firewall allows port 2501
- Solution: Verify service is running: `net start FrancoDBService`

---

## 🐧 Linux Installation

### Requirements
- Ubuntu 20.04+ / Debian 10+ / CentOS 8+ / Fedora 33+
- 500 MB free disk space
- CMake 3.10+
- GCC 7.0+ or Clang 6.0+

### Using .deb Package (Ubuntu/Debian)

> **Build Instructions**: See [installers/linux/README.md](installers/linux/README.md)

#### 1. Build the .deb package

```bash
cd /path/to/FrancoDB/installers/linux
chmod +x build_deb.sh
./build_deb.sh
```

This creates: `Output/francodb_1.0.0_amd64.deb`

#### 2. Install the package

```bash
sudo dpkg -i francodb_1.0.0_amd64.deb
```

#### 3. Start the service

```bash
sudo systemctl start francodb
sudo systemctl enable francodb  # Enable on boot
```

#### 4. Verify installation

```bash
sudo systemctl status francodb
```

### Installation Details

| Item | Location |
|------|----------|
| Binaries | `/opt/francodb/bin/` |
| Configuration | `/opt/francodb/etc/francodb.conf` |
| Data | `/opt/francodb/data/` |
| Logs | `/opt/francodb/log/` |
| Systemd Service | `/etc/systemd/system/francodb.service` |

### Quick Commands

```bash
# Start/Stop service
sudo systemctl start francodb
sudo systemctl stop francodb

# Check status
sudo systemctl status francodb
journalctl -u francodb -f

# Connect with shell
francodb

# Edit configuration
sudo nano /opt/francodb/etc/francodb.conf

# View logs
sudo tail -f /opt/francodb/log/*.log
```

### Troubleshooting

**Issue**: `apt-get install` fails with dependencies
- Solution: Run `sudo apt-get update` first
- Solution: Install build-essential: `sudo apt-get install build-essential cmake`

**Issue**: Service fails to start
- Solution: Check logs: `sudo journalctl -u francodb -n 50`
- Solution: Verify config file: `sudo cat /opt/francodb/etc/francodb.conf`

**Issue**: Permission denied when accessing data
- Solution: Restart service: `sudo systemctl restart francodb`

---

## 🐳 Docker Installation

> **Deployment Guide**: See [installers/docker/README.md](installers/docker/README.md)

### Requirements
- Docker 20.10+
- Docker Compose 1.29+
- 2 GB RAM
- 1 GB disk space

### Quick Start

```bash
cd /path/to/FrancoDB/installers/docker

# Start container
docker-compose up -d

# Check status
docker-compose ps

# View logs
docker-compose logs -f francodb

# Connect with shell
docker-compose exec francodb francodb

# Stop container
docker-compose down
```

### Docker Features

- **Multi-stage build**: Minimal image size (~200MB)
- **Health checks**: Automatic health monitoring
- **Volume persistence**: Data survives container restart
- **Environment variables**: Customizable configuration
- **Security**: Runs as non-root user

### Configuration

Edit `docker-compose.yml` to customize:

```yaml
environment:
  FRANCODB_PORT: "2501"
  FRANCODB_LOG_LEVEL: "INFO"
  FRANCODB_BUFFER_POOL_SIZE: "1024"
```

### Persistence

Data is stored in named volumes:
- `francodb_data`: Database files
- `francodb_logs`: Log files

To backup:
```bash
docker run --rm -v francodb_data:/data -v $(pwd):/backup \
  alpine tar czf /backup/francodb_backup.tar.gz -C /data .
```

---

## 🔧 Manual Installation from Source

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y build-essential cmake git libssl-dev

# CentOS/RHEL
sudo yum install -y gcc g++ cmake git openssl-devel

# macOS
brew install cmake openssl
```

### Build Steps

```bash
# Clone repository
git clone https://github.com/yourusername/FrancoDB.git
cd FrancoDB

# Create build directory
mkdir -p build
cd build

# Configure and build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)

# Install (optional)
sudo cmake --install . --prefix /opt/francodb
```

### Run Server

```bash
# From build directory
./francodb_server

# Or after installation
/opt/francodb/bin/francodb_server
```

---

## 📋 Configuration

### Default Configuration File

```conf
# FrancoDB Configuration

# Network
port = 2501
bind_address = "0.0.0.0"

# Data Storage
data_directory = "/opt/francodb/data"
log_directory = "/opt/francodb/log"

# Performance
buffer_pool_size = 1024
autosave_interval = 30

# Logging
log_level = "INFO"
log_format = "json"

# Encryption
encryption_enabled = false

# Root User
root_username = "maayn"
root_password = "change_me"
```

### Important Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `port` | 2501 | Server port |
| `bind_address` | 0.0.0.0 | Listen address |
| `buffer_pool_size` | 1024 | MB of buffer memory |
| `autosave_interval` | 30 | Seconds between saves |

---

## 🔒 Security

### Initial Setup Recommendations

1. **Change root password** immediately after installation
2. **Enable encryption** for sensitive data
3. **Configure firewall** to restrict access
4. **Regular backups** of data directory
5. **Keep software updated**

### Default Credentials

| Item | Default |
|------|---------|
| Root User | `maayn` |
| Root Password | Set during installation |
| Port | `2501` |

---

## 📊 System Requirements

### Minimum
- CPU: 1 core
- RAM: 512 MB
- Disk: 500 MB
- Network: 100 Mbps

### Recommended
- CPU: 2+ cores
- RAM: 2+ GB
- Disk: 10+ GB (for data)
- Network: 1 Gbps

### Enterprise
- CPU: 4+ cores
- RAM: 8+ GB
- Disk: 100+ GB (for data)
- SSD storage
- Redundant network

---

## ✅ Verification

### Test Installation

```bash
# Check version
francodb --version

# Test connection
francodb
> SELECT 1;

# View server status
francodb_server --status
```

### Health Check

```bash
# Linux/macOS
curl http://localhost:2501/health

# Docker
docker-compose exec francodb curl http://localhost:2501/health
```

---

## 📞 Support

### Documentation
- README.md: Project overview
- S_PLUS_ENHANCEMENTS.md: Feature details
- QUICK_START_S_PLUS.md: Usage guide

### Issues
- GitHub Issues: https://github.com/yourusername/FrancoDB/issues
- Email: dev@francodb.io

### Logs
- Windows: `C:\Program Files\FrancoDB\log`
- Linux: `/opt/francodb/log`
- Docker: `docker-compose logs francodb`

---

## 🚀 Next Steps

1. **Read** S_PLUS_ENHANCEMENTS.md for SQL features
2. **Try** QUICK_START_S_PLUS.md examples
3. **Join** community for support
4. **Report** issues on GitHub

---

**Happy Database Building!** 🎉

