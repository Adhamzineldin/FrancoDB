# ChronosDB Windows Installation Troubleshooting Guide

## Common Issues

### Error: "start.vps is missing" on Windows startup

**Symptom:** After restarting your computer, you see an error message saying "start.vps is missing" or a similar error about a VBS file.

**Cause:** This is a leftover registry entry from an old FrancoDB installation (the previous name of ChronosDB) that's trying to auto-start a file that no longer exists.

**Solution:**

1. Open Command Prompt or PowerShell as Administrator
2. Run the following command to remove the legacy startup entry:
   ```
   reg delete "HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "FrancoDB Server" /f
   ```
3. Restart your computer - the error should no longer appear

**Note:** ChronosDB now runs as a Windows Service (ChronosDBService) instead of using VBScript startup files. The service is configured to start automatically when Windows boots.

### ChronosDB Service Management

ChronosDB is installed as a Windows Service and starts automatically on system boot. You can manage it using:

- **Start Menu Shortcuts:**
  - Start ChronosDB
  - Stop ChronosDB

- **Windows Services Manager:**
  - Press Win+R, type `services.msc`, and press Enter
  - Look for "ChronosDBService"

- **Command Line:**
  ```powershell
  # Check service status
  sc.exe query ChronosDBService
  
  # Start service
  sc.exe start ChronosDBService
  
  # Stop service
  sc.exe stop ChronosDBService
  
  # Disable auto-start (not recommended)
  sc.exe config ChronosDBService start= demand
  
  # Re-enable auto-start
  sc.exe config ChronosDBService start= auto
  ```

### Service Won't Start

1. Check the service log: `C:\Program Files\ChronosDB\log\chronosdb_service.log`
2. Verify the configuration file exists: `C:\Program Files\ChronosDB\bin\chronosdb.conf`
3. Ensure port 2501 (or your configured port) is not already in use
4. Try running the server directly: `chronosdb_server.exe` from the installation directory

### Uninstallation

If you need to completely remove ChronosDB:

1. Use the uninstaller from Start Menu → ChronosDB → Uninstall ChronosDB
2. When prompted, choose whether to delete database files
3. If manual cleanup is needed:
   ```powershell
   # Stop and remove service
   sc.exe stop ChronosDBService
   sc.exe delete ChronosDBService
   
   # Remove from PATH (optional)
   # Edit system environment variables through System Properties
   
   # Delete installation directory
   Remove-Item "C:\Program Files\ChronosDB" -Recurse -Force
   ```

## Getting Help

For more assistance:
- Check the main README.md in the project root
- Review logs in: `C:\Program Files\ChronosDB\log\`
- Open an issue on the project's GitHub repository

