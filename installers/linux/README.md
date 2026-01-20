# FrancoDB Linux Installer

This folder contains the Debian package (.deb) builder for FrancoDB.

## 📦 Contents

- **build_deb.sh** - Script to build .deb package
- **README.md** - This file

## 🔧 Prerequisites

### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git dpkg-dev fakeroot
```

### System Requirements
- Ubuntu 20.04+ or Debian 10+
- CMake 3.10+
- GCC 7.0+ or Clang 6.0+
- 1 GB free disk space

## 🏗️ Building the .deb Package

### Step 1: Make Script Executable

```bash
cd /path/to/FrancoDB/installers/linux
chmod +x build_deb.sh
```

### Step 2: Run Builder

```bash
./build_deb.sh
```

The script will:
1. ✅ Build FrancoDB from source
2. ✅ Create Debian package structure
3. ✅ Generate control files
4. ✅ Package into .deb
5. ✅ Create MD5 checksum

### Step 3: Find Package

Output location:
```
FrancoDB/Output/francodb_1.0.0_amd64.deb
FrancoDB/Output/francodb_1.0.0_amd64.deb.md5
```

## 📋 Package Features

✅ **System Integration**
  - Installs to `/opt/francodb/`
  - Creates `francodb` system user
  - Configures systemd service
  - Adds to PATH via symlinks

✅ **Post-Install Scripts**
  - Sets proper permissions
  - Creates default configuration
  - Enables systemd service
  - Shows next steps

✅ **Clean Uninstall**
  - Stops service gracefully
  - Optional data removal
  - Removes system user (purge)

## 🎯 Installation

### Install Package

```bash
sudo dpkg -i francodb_1.0.0_amd64.deb
```

### Fix Dependencies (if needed)

```bash
sudo apt-get install -f
```

### Start Service

```bash
sudo systemctl start francodb
sudo systemctl enable francodb  # Enable on boot
```

### Verify Installation

```bash
sudo systemctl status francodb
francodb --version
```

## 📂 Installed Files

| Item | Location |
|------|----------|
| Binaries | `/opt/francodb/bin/` |
| Configuration | `/opt/francodb/etc/francodb.conf` |
| Data | `/opt/francodb/data/` |
| Logs | `/opt/francodb/log/` |
| Systemd Service | `/etc/systemd/system/francodb.service` |
| Symlinks | `/usr/local/bin/{francodb,francodb_server}` |

## ⚙️ Customization

### Change Version

Edit `build_deb.sh`:
```bash
VERSION="1.0.0"  # Change this
```

### Modify Control File

The script auto-generates `DEBIAN/control`. To customize, edit this section in `build_deb.sh`:

```bash
cat > "$DEB_DIR/DEBIAN/control" << EOF
Package: francodb
Version: $VERSION
...
EOF
```

### Custom Install Scripts

Edit these sections in `build_deb.sh`:
- `DEBIAN/preinst` - Runs before installation
- `DEBIAN/postinst` - Runs after installation
- `DEBIAN/prerm` - Runs before removal
- `DEBIAN/postrm` - Runs after removal

## 🐛 Troubleshooting

**Issue**: Build fails with "CMake not found"
```bash
sudo apt-get install cmake
```

**Issue**: dpkg-deb command not found
```bash
sudo apt-get install dpkg-dev
```

**Issue**: Service fails to start after install
```bash
# Check logs
sudo journalctl -u francodb -n 50

# Verify config
sudo cat /opt/francodb/etc/francodb.conf

# Check permissions
ls -la /opt/francodb/
```

**Issue**: Permission denied errors
```bash
# Fix permissions
sudo chown -R francodb:francodb /opt/francodb
sudo chmod 700 /opt/francodb/{data,log}
```

## 📝 Testing

### Test Package Installation

```bash
# Install
sudo dpkg -i francodb_1.0.0_amd64.deb

# Test service
sudo systemctl start francodb
sudo systemctl status francodb

# Test CLI
francodb --version
francodb

# Uninstall (keep data)
sudo dpkg -r francodb

# Purge (remove everything including data)
sudo dpkg -P francodb
```

### Verify Package Contents

```bash
dpkg -c francodb_1.0.0_amd64.deb
```

### Check Package Info

```bash
dpkg -I francodb_1.0.0_amd64.deb
```

## 🔍 Package Quality Checks

### Lintian (Debian Package Checker)

```bash
sudo apt-get install lintian
lintian francodb_1.0.0_amd64.deb
```

### Verify Checksum

```bash
md5sum -c francodb_1.0.0_amd64.deb.md5
```

## 📚 Additional Package Formats

### Convert to RPM (for CentOS/RHEL)

```bash
sudo apt-get install alien
sudo alien --to-rpm francodb_1.0.0_amd64.deb
```

### Convert to tar.gz

```bash
sudo apt-get install alien
sudo alien --to-tgz francodb_1.0.0_amd64.deb
```

## 🚀 Distribution

### Upload to Repository

```bash
# Upload to GitHub Releases
gh release create v1.0.0 francodb_1.0.0_amd64.deb

# Upload to PPA (Personal Package Archive)
# Follow: https://help.launchpad.net/Packaging/PPA
```

### Create APT Repository

```bash
# Install tools
sudo apt-get install dpkg-dev

# Create repository structure
mkdir -p repo/pool/main
cp francodb_1.0.0_amd64.deb repo/pool/main/

# Generate Packages file
cd repo
dpkg-scanpackages pool/ /dev/null | gzip -9c > pool/Packages.gz

# Host the repository
python3 -m http.server 8000
```

## 🔐 Package Signing (Optional)

For production, sign the package:

```bash
# Generate GPG key (if needed)
gpg --gen-key

# Sign package
dpkg-sig --sign builder francodb_1.0.0_amd64.deb

# Verify signature
dpkg-sig --verify francodb_1.0.0_amd64.deb
```

## 📖 Documentation

- Build script: `build_deb.sh`
- Installation guide: `../../INSTALLATION_GUIDE.md`
- Project README: `../../README.md`

## 💡 Tips

1. **Test in clean environment**: Use Docker or VM to test installation
2. **Check dependencies**: Ensure all dependencies are listed in control file
3. **Version numbering**: Follow Debian versioning scheme (1.0.0-1)
4. **Changelog**: Update changelog for each release

## 🌍 Multi-Distribution Support

The build script supports:
- ✅ Ubuntu 20.04+
- ✅ Debian 10+
- ✅ Linux Mint 20+
- ✅ Pop!_OS 20.04+

For other distributions, convert using `alien` or create native packages.

---

**Last Updated**: January 19, 2026

