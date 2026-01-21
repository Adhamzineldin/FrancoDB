[Setup]
AppName=FrancoDB
AppVersion=1.0
AppPublisher=FrancoDB Team
AppPublisherURL=https://github.com/yourusername/FrancoDB
AppId={{8C5E4A9B-3F2D-4B1C-9A7E-5D8F2C1B4A3E}
DefaultDirName={autopf}\FrancoDB
DefaultGroupName=FrancoDB
OutputBaseFilename=FrancoDB_Setup
Compression=lzma
SolidCompression=yes
ChangesEnvironment=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64
WizardStyle=modern
SetupIconFile=..\..\resources\francodb.ico
UninstallDisplayIcon={app}\bin\francodb_server.exe
AppMutex=FrancoDBInstaller
UsePreviousAppDir=yes
DirExistsWarning=auto

[Dirs]
Name: "{app}\data"; Permissions: users-modify
Name: "{app}\log"; Permissions: users-modify

[Files]
; ==============================================================================
; 1. AUTOMATIC DLLs (FROM RELEASE FOLDER ONLY)
; ==============================================================================
; The "skipifsourcedoesntexist" flag prevents the error if you haven't built yet,
; but you SHOULD see these files in cmake-build-release if CMake worked.
Source: "..\..\cmake-build-release\*.dll"; DestDir: "{app}\bin"; Flags: ignoreversion skipifsourcedoesntexist

; ==============================================================================
; 2. EXECUTABLES (RELEASE ONLY)
; ==============================================================================
; Server
Source: "..\..\cmake-build-release\francodb_server.exe"; DestDir: "{app}\bin"; Flags: ignoreversion

; Shell (Installed as 'francodb_shell.exe' and alias 'francodb.exe')
Source: "..\..\cmake-build-release\francodb_shell.exe"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\..\cmake-build-release\francodb_shell.exe"; DestDir: "{app}\bin"; DestName: "francodb.exe"; Flags: ignoreversion

; Service
Source: "..\..\cmake-build-release\francodb_service.exe"; DestDir: "{app}\bin"; Flags: ignoreversion

; ==============================================================================
; 3. MANAGEMENT SCRIPTS
; ==============================================================================
; Batch scripts for easy service control
Source: "start_server.bat"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "stop_server.bat"; DestDir: "{app}\bin"; Flags: ignoreversion

; VBScript versions (for advanced users)
Source: "start_server.vbs"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "stop_server.vbs"; DestDir: "{app}\bin"; Flags: ignoreversion

; PowerShell utility module
Source: "FrancoDBUtils.ps1"; DestDir: "{app}\bin"; Flags: ignoreversion


[UninstallDelete]
; Force delete the generated config file
Type: files; Name: "{app}\bin\francodb.conf"

; Force delete the entire data and log directories (and everything inside them)
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{app}\log"

; Optional: Try to delete the root folder itself if it's empty now
Type: dirifempty; Name: "{app}"



[Icons]
Name: "{group}\FrancoDB Shell"; Filename: "{app}\bin\francodb.exe"; WorkingDir: "{app}\bin"
Name: "{group}\FrancoDB Configuration"; Filename: "notepad.exe"; Parameters: """{app}\bin\francodb.conf"""
Name: "{group}\Start FrancoDB"; Filename: "{app}\bin\start_server.bat"; WorkingDir: "{app}\bin"; IconFilename: "{app}\bin\francodb_server.exe"
Name: "{group}\Stop FrancoDB"; Filename: "{app}\bin\stop_server.bat"; WorkingDir: "{app}\bin"; IconFilename: "{app}\bin\francodb_server.exe"
Name: "{group}\Uninstall FrancoDB"; Filename: "{uninstallexe}"

[Registry]
; 1. Existing System Path Update (Keep this!)
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; \
    ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}\bin"; \
    Check: NeedsAddPath(ExpandConstant('{app}\bin'))

; ==============================================================================
; 2. URL PROTOCOL HANDLER (maayn://)
; ==============================================================================
; This registers the protocol so Windows knows "maayn://" opens FrancoDB Shell
Root: HKCR; Subkey: "maayn"; ValueType: string; ValueName: ""; ValueData: "URL:FrancoDB Protocol"; Flags: uninsdeletekey
Root: HKCR; Subkey: "maayn"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""; Flags: uninsdeletekey

; Set the Icon for the protocol (uses the shell exe icon)
Root: HKCR; Subkey: "maayn\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\bin\francodb_shell.exe,0"; Flags: uninsdeletekey

; Define the command to run when a link is clicked
; We wrap %1 in quotes to handle spaces in URLs
Root: HKCR; Subkey: "maayn\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\francodb_shell.exe"" ""%1"""; Flags: uninsdeletekey

[UninstallRun]
Filename: "sc.exe"; Parameters: "stop FrancoDBService"; Flags: runhidden; RunOnceId: "StopService"
Filename: "sc.exe"; Parameters: "delete FrancoDBService"; Flags: runhidden; RunOnceId: "DeleteService"



[Code]

var
  PortPage: TInputQueryWizardPage;
  CredentialsPage: TInputQueryWizardPage;
  EncryptionPage: TInputOptionWizardPage;
  EncryptionKeyPage: TInputQueryWizardPage;
  SummaryPage: TOutputMsgMemoWizardPage;
  
  GeneratedEncryptionKey: String;
  IsUpgrade: Boolean;
  ServiceInstalled: Boolean;
  ServiceStarted: Boolean;
  ServiceRebootRequired: Boolean;
  ServiceStartResult: Integer;

// ==============================================================================
// 1. HELPER FUNCTIONS
// ==============================================================================

// Checks if a specific process name is currently running using WMI
function IsProcessRunning(const FileName: String): Boolean;
var
  FSWbemLocator: Variant;
  FWbemObjectSet: Variant;
  Query: String;
begin
  Result := False;
  try
    FSWbemLocator := CreateOleObject('WbemScripting.SWbemLocator');
    FWbemObjectSet := FSWbemLocator.ConnectServer('', 'root\CIMV2', '', '');
    Query := 'SELECT * FROM Win32_Process WHERE Name="' + FileName + '"';
    FWbemObjectSet := FWbemObjectSet.ExecQuery(Query);
    Result := (FWbemObjectSet.Count > 0);
  except
    Result := False;
  end;
end;

function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE,
    'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
    'Path', OrigPath)
  then begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Param + ';', ';' + OrigPath + ';') = 0;
end;

function GenerateEncryptionKey(): String;
var
  I: Integer;
  Bytes: Array[0..31] of Byte;
begin
  Result := '';
  for I := 0 to 31 do
  begin
    Bytes[I] := Random(256);
    Result := Result + Format('%.2x', [Bytes[I]]);
  end;
end;

function InitializeSetup(): Boolean;
var
  PrevPath: String;
begin
  Result := True;
  IsUpgrade := False;
  if RegQueryStringValue(HKEY_LOCAL_MACHINE, 
     'Software\Microsoft\Windows\CurrentVersion\Uninstall\{8C5E4A9B-3F2D-4B1C-9A7E-5D8F2C1B4A3E}_is1',
     'InstallLocation', PrevPath) then
  begin
    IsUpgrade := True;
    if MsgBox('A previous installation was detected.' + #13#10 + #13#10 +
              'Upgrade now? (Config will be preserved)', mbConfirmation, MB_YESNO) = IDNO then
       IsUpgrade := False;
  end;
end;

function BoolToStr(Value: Boolean): String;
begin
  if Value then Result := 'true' else Result := 'false';
end;

function NeedRestart(): Boolean;
begin
  Result := ServiceRebootRequired;
end;

// ==============================================================================
// 2. WIZARD PAGES
// ==============================================================================

procedure InitializeWizard;
begin
  PortPage := CreateInputQueryPage(wpWelcome, 'Server Config', 'Port Settings', 'Enter server port (Default: 2501)');
  PortPage.Add('Port:', False);
  PortPage.Values[0] := '2501';

  CredentialsPage := CreateInputQueryPage(PortPage.ID, 'Server Config', 'Root Credentials', 'Enter root credentials.');
  CredentialsPage.Add('Username:', False);
  CredentialsPage.Add('Password:', True);
  CredentialsPage.Add('Confirm:', True);
  CredentialsPage.Values[0] := 'maayn';

  EncryptionPage := CreateInputOptionPage(CredentialsPage.ID, 'Encryption', 'Security', 'Choose preference:', True, False);
  EncryptionPage.Add('No encryption');
  EncryptionPage.Add('Auto-generated key');
  EncryptionPage.Add('Custom key');
  EncryptionPage.SelectedValueIndex := 1;

  EncryptionKeyPage := CreateInputQueryPage(EncryptionPage.ID, 'Encryption Key', 'Custom Key', 'Enter 64-char hex key.');
  EncryptionKeyPage.Add('Key:', False);

  SummaryPage := CreateOutputMsgMemoPage(wpInstalling, 'Finished', 'Status Report', 'Review the results below.', '');
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  if IsUpgrade and ((PageID = PortPage.ID) or (PageID = CredentialsPage.ID) or (PageID = EncryptionPage.ID) or (PageID = EncryptionKeyPage.ID)) then
  begin
    Result := True;
    Exit;
  end;
  if PageID = EncryptionKeyPage.ID then Result := (EncryptionPage.SelectedValueIndex <> 2);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = PortPage.ID) and (StrToIntDef(PortPage.Values[0], 0) = 0) then Result := False;
  if (CurPageID = CredentialsPage.ID) and (CredentialsPage.Values[1] <> CredentialsPage.Values[2]) then
  begin
    MsgBox('Passwords do not match.', mbError, MB_OK);
    Result := False;
  end;
  if (CurPageID = EncryptionPage.ID) and (EncryptionPage.SelectedValueIndex = 1) then
     GeneratedEncryptionKey := GenerateEncryptionKey();
end;

// ============================================================================== 
// 3. ROBUST SERVICE STOPPING
// ==============================================================================

function GetServiceState(const ServiceName: String): Integer;
var
  ResultCode: Integer;
  TempFile: String;
  Lines: TArrayOfString;
  I: Integer;
begin
  Result := -1; 
  TempFile := ExpandConstant('{tmp}\service_state.txt');
  
  if Exec('cmd.exe', '/C sc query "' + ServiceName + '" > "' + TempFile + '" 2>&1', 
          '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    if LoadStringsFromFile(TempFile, Lines) then
    begin
      for I := 0 to GetArrayLength(Lines) - 1 do
      begin
        if Pos('STATE', Lines[I]) > 0 then
        begin
          if Pos('STOPPED', Lines[I]) > 0 then Result := 1
          else if Pos('STOP_PENDING', Lines[I]) > 0 then Result := 2
          else if Pos('RUNNING', Lines[I]) > 0 then Result := 4
          else if Pos('START_PENDING', Lines[I]) > 0 then Result := 3;
          Break;
        end;
      end;
    end;
    DeleteFile(TempFile);
  end;
end;

function WaitForServiceStop(const ServiceName: String; MaxWaitSec: Integer): Boolean;
var
  I, State: Integer;
  ProcessRunning: Boolean;
begin
  Result := False;
  
  for I := 1 to MaxWaitSec do
  begin
    State := GetServiceState(ServiceName);
    
    // If Service Manager says it's stopped (1) or gone (-1)
    if (State = 1) or (State = -1) then
    begin
      // DOUBLE CHECK: Is the actual .exe still in memory?
      ProcessRunning := IsProcessRunning('francodb_server.exe') or IsProcessRunning('francodb_service.exe');
      
      if not ProcessRunning then
      begin
        Result := True;
        Break;
      end;
      
      WizardForm.StatusLabel.Caption := 'Service stopped, waiting for process cleanup...';
    end
    else
    begin
      if State = 2 then
        WizardForm.StatusLabel.Caption := 'Saving data to disk... (' + IntToStr(I) + 's)'
      else
        WizardForm.StatusLabel.Caption := 'Stopping service... (' + IntToStr(I) + 's)';
    end;
    
    Sleep(1000);
  end;
end;

// ==============================================================================
// 4. MAIN INSTALLATION LOGIC
// ==============================================================================

procedure CurStepChanged(CurStep: TSetupStep);
var
  Port, User, Pass, Key: String;
  ConfigContent, SummaryText: String;
  EncEnabled: Boolean;
  ResultCode, I: Integer;
  ServicePath, DataDir, LogDir: String;
  ServiceStopped: Boolean;
begin
  // --- STOPPING SERVICE ---
  if CurStep = ssInstall then
  begin
    WizardForm.StatusLabel.Caption := 'Stopping FrancoDB...';
    Exec('sc.exe', 'stop FrancoDBService', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    
    // Wait up to 120 seconds
    ServiceStopped := WaitForServiceStop('FrancoDBService', 120);
    
    if not ServiceStopped then
    begin
       if MsgBox('The database service is stuck.' + #13#10 + 
                 'Force close it? (Unsaved data might be lost)', mbError, MB_YESNO) = IDYES then
       begin
          Exec('taskkill', '/F /IM francodb_service.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
          Exec('taskkill', '/F /IM francodb_server.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
       end
       else
       begin
          Abort;
       end;
    end;
    
    Exec('sc.exe', 'delete FrancoDBService', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Sleep(2000);
    
    // Clean old logs
    DelTree(ExpandConstant('{app}\log'), True, True, True);
  end;

  // --- CONFIGURING & STARTING ---
  if CurStep = ssPostInstall then
  begin
    DataDir := ExpandConstant('{app}\data');
    LogDir  := ExpandConstant('{app}\log');

    // Permissions
    Exec('icacls', '"' + DataDir + '" /grant Users:(OI)(CI)M /T /C /Q', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Exec('icacls', '"' + LogDir + '" /grant Users:(OI)(CI)M /T /C /Q', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);

    // Write Config
    if not IsUpgrade then
    begin
       Port := PortPage.Values[0];
       User := CredentialsPage.Values[0];
       Pass := CredentialsPage.Values[1];
       EncEnabled := (EncryptionPage.SelectedValueIndex > 0);
       
       if EncryptionPage.SelectedValueIndex = 2 then Key := EncryptionKeyPage.Values[0] else Key := GeneratedEncryptionKey;

       ConfigContent := '# FrancoDB Config' + #13#10 + 'port = ' + Port + #13#10 + 'root_username = "' + User + '"' + #13#10 +
                        'root_password = "' + Pass + '"' + #13#10 + 'data_directory = "' + DataDir + '"' + #13#10 +
                        'encryption_enabled = ' + BoolToStr(EncEnabled) + #13#10 + 'autosave_interval = 30';
       if EncEnabled and (Key <> '') then ConfigContent := ConfigContent + #13#10 + 'encryption_key = "' + Key + '"';
       SaveStringToFile(ExpandConstant('{app}\bin\francodb.conf'), ConfigContent, False);
    end;

    // Install Service
    ServicePath := ExpandConstant('{app}\bin\francodb_service.exe');
    ServiceInstalled := False;
    ServiceRebootRequired := False;
    
    if FileExists(ServicePath) then
    begin
       for I := 1 to 3 do
       begin
          Exec('sc.exe', 'create FrancoDBService binPath= "' + ServicePath + '" start= auto', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
          if ResultCode = 0 then
          begin
             ServiceInstalled := True;
             Break;
          end
          else if (ResultCode = 1073) then
          begin
             Exec('sc.exe', 'delete FrancoDBService', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
             Sleep(2000);
          end
          else if (ResultCode = 1072) then
          begin
             Sleep(2000);
          end;
       end;

       if (not ServiceInstalled) and (ResultCode = 1072) then ServiceRebootRequired := True;

       if ServiceInstalled then
       begin
          Exec('sc.exe', 'failure FrancoDBService reset= 86400 actions= restart/5000', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
          Sleep(500);
          Exec('sc.exe', 'start FrancoDBService', '', SW_HIDE, ewWaitUntilTerminated, ServiceStartResult);
          ServiceStarted := (ServiceStartResult = 0) or (ServiceStartResult = 1056);
       end
       else
       begin
          ServiceStarted := False;
          ServiceStartResult := ResultCode; 
       end;
    end
    else
    begin
       ServiceInstalled := False;
       ServiceStarted := False;
       ServiceStartResult := -1; 
    end;

    // --- DETAILED SUMMARY REPORT ---
    SummaryText := 'STATUS REPORT' + #13#10 + '------------------------------------------------------------' + #13#10;
    
    // Service Status
    if ServiceRebootRequired then
    begin
      SummaryText := SummaryText + '[WARN] Service : REBOOT REQUIRED (Code 1072)' + #13#10;
      SummaryText := SummaryText + '       The previous service is stuck. Restart Windows' + #13#10;
      SummaryText := SummaryText + '       to finish the update.' + #13#10;
    end
    else if ServiceStarted then
      SummaryText := SummaryText + '[ OK ] Service : FrancoDB is running' + #13#10
    else if not ServiceInstalled then
      SummaryText := SummaryText + '[FAIL] Service : Failed to create (Code ' + IntToStr(ServiceStartResult) + ')' + #13#10
    else if ServiceStartResult = -1 then
      SummaryText := SummaryText + '[FAIL] Service : Executable missing from {app}\bin' + #13#10
    else
      SummaryText := SummaryText + '[FAIL] Service : Failed to start (Code ' + IntToStr(ServiceStartResult) + ')' + #13#10;

    // Environment Variables Status
    if NeedsAddPath(ExpandConstant('{app}\bin')) then
      SummaryText := SummaryText + '[INFO] Env Var : Updated (Restart Terminal)' + #13#10
    else
      SummaryText := SummaryText + '[ OK ] Env Var : Already configured' + #13#10;

    // Configuration Status
    if IsUpgrade then
      SummaryText := SummaryText + '[INFO] Config  : Preserved previous settings' + #13#10
    else
      SummaryText := SummaryText + '[ OK ] Config  : Generated successfully' + #13#10;

    SummaryText := SummaryText + #13#10;

    // Security Alert
    if (not IsUpgrade) and (EncryptionPage.SelectedValueIndex = 1) then
    begin
      SummaryText := SummaryText + 'SECURITY ALERT' + #13#10;
      SummaryText := SummaryText + '------------------------------------------------------------' + #13#10;
      SummaryText := SummaryText + 'Below is your Master Encryption Key. You MUST save this.' + #13#10;
      SummaryText := SummaryText + 'If you lose this key, your database is gone forever.' + #13#10 + #13#10;
      SummaryText := SummaryText + GeneratedEncryptionKey + #13#10;
      SummaryText := SummaryText + '------------------------------------------------------------' + #13#10;
    end;

    SummaryPage.RichEditViewer.Lines.Text := SummaryText;
    SummaryPage.RichEditViewer.Font.Name := 'Consolas';
    SummaryPage.RichEditViewer.Font.Size := 9;
  end;
end;

// ==============================================================================
// 5. UNINSTALLER LOGIC
// ==============================================================================

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode, I: Integer;
  ProcessRunning: Boolean;
begin
  if CurUninstallStep = usUninstall then
  begin
    Exec('sc.exe', 'stop FrancoDBService', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    
    // Wait for shutdown (Max 60s)
    for I := 1 to 60 do
    begin
      ProcessRunning := IsProcessRunning('francodb_server.exe');
      if not ProcessRunning then Break;
      Sleep(1000);
    end;
    
    // If still running after 60s, KILL IT. Uninstaller must succeed.
    if IsProcessRunning('francodb_server.exe') then
    begin
        Exec('taskkill', '/F /IM francodb_server.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    end;

    Exec('sc.exe', 'delete FrancoDBService', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Sleep(1000);
  end;

  if CurUninstallStep = usPostUninstall then
  begin
    if DirExists(ExpandConstant('{app}\data')) then
    begin
      if MsgBox('Delete all database files and logs?', mbConfirmation, MB_YESNO) = IDYES then
      begin
        DelTree(ExpandConstant('{app}\data'), True, True, True);
        DelTree(ExpandConstant('{app}\log'), True, True, True);
        DeleteFile(ExpandConstant('{app}\bin\francodb.conf'));
        RemoveDir(ExpandConstant('{app}\bin'));
        RemoveDir(ExpandConstant('{app}'));
      end;
    end;
  end;
end;