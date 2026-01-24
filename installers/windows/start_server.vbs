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
    Dim intState, intWaitCount, blnSuccess, objFSO, logFile
    
    ' Silent operation - no console output or message boxes
    ' Logs are written to a file instead
    Set objFSO = CreateObject("Scripting.FileSystemObject")
    logFile = strInstallPath & "francodb_startup.log"
    
    ' Check if service exists
    if ServiceExists(strServiceName) then
        ' Check current state
        intState = GetServiceState(strServiceName)
        
        Select Case intState
            Case 4 ' Already running
                ' Service is already running - nothing to do
                Exit Sub
            Case 2 ' Start pending
                ' Service is starting - wait for it
                intWaitCount = 0
                Do While (intWaitCount < 30)
                    WScript.Sleep 1000
                    intState = GetServiceState(strServiceName)
                    if intState = 4 then
                        Exit Sub
                    end if
                    intWaitCount = intWaitCount + 1
                Loop
                Exit Sub
            Case 1, -1 ' Stopped or unknown
                ' Start the service silently
                intReturnCode = objShell.Run("cmd /c sc start " & strServiceName, 0, True)
                
                if intReturnCode = 0 then
                    ' Wait for service to actually start
                    intWaitCount = 0
                    Do While (intWaitCount < 30)
                        WScript.Sleep 1000
                        intState = GetServiceState(strServiceName)
                        if intState = 4 then
                            Exit Sub
                        end if
                        intWaitCount = intWaitCount + 1
                    Loop
                end if
        End Select
    else
        ' Fallback: Try to run the executable directly (silently)
        if FileExists(strServerExe) then
            intReturnCode = objShell.Run("""" & strServerExe & """", 0, False)
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
' Silent startup - no output or message boxes
StartFrancoDB()


