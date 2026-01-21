' ==============================================================================
' FrancoDB Server Startup Script
' ==============================================================================
' This script starts the FrancoDB service on Windows.
' It handles both service and direct executable startup modes.
' ==============================================================================

Option Explicit
On Error Resume Next

Dim objShell, objWMIService, colItems, objItem
Dim strServiceName, strInstallPath, strServerExe
Dim intReturnCode, blnServiceExists, blnRunning

' Initialize
Set objShell = CreateObject("WScript.Shell")
strServiceName = "FrancoDBService"
blnServiceExists = False
blnRunning = False

' Get the install path (script is in {app}\bin)
strInstallPath = objShell.CurrentDirectory
If Right(strInstallPath, 1) <> "\" Then
    strInstallPath = strInstallPath & "\"
End If
strServerExe = strInstallPath & "francodb_server.exe"

' ==============================================================================
' HELPER: Check if service exists
' ==============================================================================
Function ServiceExists(sName)
    Dim objShell, objWMIService, colServices, objService
    Dim strQuery, bFound
    
    Set objShell = CreateObject("WScript.Shell")
    Set objWMIService = GetObject("winmgmts:")
    strQuery = "Select * from Win32_Service Where Name = '" & sName & "'"
    Set colServices = objWMIService.ExecQuery(strQuery)
    
    bFound = (colServices.Count > 0)
    ServiceExists = bFound
End Function

' ==============================================================================
' HELPER: Get service state (1=Stopped, 2=Start Pending, 4=Running, 3=Stop Pending)
' ==============================================================================
Function GetServiceState(sName)
    Dim objWMIService, colItems, objItem
    
    Set objWMIService = GetObject("winmgmts:")
    Set colItems = objWMIService.ExecQuery("Select State From Win32_Service Where Name = '" & sName & "'")
    
    If colItems.Count > 0 Then
        Select Case colItems(0).State
            Case "Stopped"
                GetServiceState = 1
            Case "Start Pending"
                GetServiceState = 2
            Case "Running"
                GetServiceState = 4
            Case "Stop Pending"
                GetServiceState = 3
            Case Else
                GetServiceState = -1
        End Select
    Else
        GetServiceState = -1
    End If
End Function

' ==============================================================================
' MAIN STARTUP LOGIC
' ==============================================================================
Sub StartFrancoDB()
    Dim intState, intWaitCount, blnSuccess
    
    WScript.Echo "FrancoDB Startup Script"
    WScript.Echo "========================================"
    WScript.Echo ""
    
    ' Check if service exists
    if ServiceExists(strServiceName) then
        WScript.Echo "[INFO] Service found: " & strServiceName
        
        ' Check current state
        intState = GetServiceState(strServiceName)
        
        Select Case intState
            Case 4 ' Already running
                WScript.Echo "[OK] FrancoDB is already running!"
                Exit Sub
            Case 2 ' Start pending
                WScript.Echo "[INFO] Service is starting..."
                intWaitCount = 0
                Do While (intWaitCount < 30)
                    WScript.Sleep 1000
                    intState = GetServiceState(strServiceName)
                    if intState = 4 then
                        WScript.Echo "[OK] FrancoDB started successfully!"
                        Exit Sub
                    end if
                    intWaitCount = intWaitCount + 1
                Loop
                WScript.Echo "[WARN] Service took too long to start. Check manually."
                Exit Sub
            Case 1, -1 ' Stopped or unknown
                WScript.Echo "[INFO] Starting service..."
                intReturnCode = objShell.Run("cmd /c sc start " & strServiceName, 0, True)
                
                if intReturnCode = 0 then
                    ' Wait for service to actually start
                    intWaitCount = 0
                    Do While (intWaitCount < 30)
                        WScript.Sleep 1000
                        intState = GetServiceState(strServiceName)
                        if intState = 4 then
                            WScript.Echo "[OK] FrancoDB started successfully!"
                            Exit Sub
                        end if
                        intWaitCount = intWaitCount + 1
                    Loop
                    WScript.Echo "[WARN] Service may not have started. Check francodb.conf"
                else
                    WScript.Echo "[ERROR] Failed to start service (Code: " & intReturnCode & ")"
                end if
        End Select
    else
        WScript.Echo "[WARN] Service not found. Attempting direct startup..."
        
        ' Fallback: Try to run the executable directly
        if FileExists(strServerExe) then
            WScript.Echo "[INFO] Starting executable: " & strServerExe
            intReturnCode = objShell.Run("""" & strServerExe & """", 1, False)
            if intReturnCode = 0 then
                WScript.Echo "[OK] FrancoDB started!"
            else
                WScript.Echo "[ERROR] Failed to start executable"
            end if
        else
            WScript.Echo "[ERROR] Server executable not found at: " & strServerExe
        end if
    end if
End Sub

' ==============================================================================
' HELPER: Check if file exists
' ==============================================================================
Function FileExists(strPath)
    Dim objFSO
    Set objFSO = CreateObject("Scripting.FileSystemObject")
    FileExists = objFSO.FileExists(strPath)
End Function

' ==============================================================================
' RUN THE SCRIPT
' ==============================================================================
StartFrancoDB()

WScript.Echo ""
WScript.Echo "========================================"
WScript.Echo "Script completed."

