' ==============================================================================
' FrancoDB Server Stop Script
' ==============================================================================
' This script gracefully stops the FrancoDB service on Windows.
' It handles both service and direct process stopping modes.
' ==============================================================================

Option Explicit
On Error Resume Next

Dim objShell, objWMIService, colItems
Dim strServiceName, intReturnCode, intWaitCount, intState

' Initialize
Set objShell = CreateObject("WScript.Shell")
strServiceName = "FrancoDBService"

' ==============================================================================
' HELPER: Get service state (1=Stopped, 4=Running, etc.)
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
' HELPER: Check if process is running
' ==============================================================================
Function IsProcessRunning(strProcessName)
    Dim objWMIService, colItems
    
    Set objWMIService = GetObject("winmgmts:")
    Set colItems = objWMIService.ExecQuery("Select * from Win32_Process Where Name = '" & strProcessName & "'")
    
    IsProcessRunning = (colItems.Count > 0)
End Function

' ==============================================================================
' MAIN STOP LOGIC
' ==============================================================================
Sub StopFrancoDB()
    Dim blnGracefulStop
    
    blnGracefulStop = False
    
    ' Step 1: Try to stop the service gracefully
    intReturnCode = objShell.Run("cmd /c sc stop " & strServiceName, 0, True)
    
    if intReturnCode = 0 or intReturnCode = 1062 then ' 1062 = already stopped
        ' Wait for service to stop (max 60 seconds)
        intWaitCount = 0
        Do While (intWaitCount < 60)
            WScript.Sleep 1000
            intState = GetServiceState(strServiceName)
            if intState = 1 or intState = -1 then
                blnGracefulStop = True
                Exit Do
            end if
            intWaitCount = intWaitCount + 1
            if intWaitCount Mod 10 = 0 then
                WScript.Echo "[INFO] Waiting for shutdown... (" & intWaitCount & "s)"
            end if
        Loop
    end if
    
    ' Step 2: Check if processes are still running and force kill if needed
    if IsProcessRunning("francodb_server.exe") or IsProcessRunning("francodb_service.exe") then
        ' Force kill silently
        objShell.Run "cmd /c taskkill /F /IM francodb_service.exe 2>nul", 0, True
        objShell.Run "cmd /c taskkill /F /IM francodb_server.exe 2>nul", 0, True
        WScript.Sleep 1000
    end if
    
End Sub

' ==============================================================================
' RUN THE SCRIPT
' ==============================================================================
' Silent operation - no popup windows
StopFrancoDB()


