# Starts a console program with NO window (Training Ground "Build in Unreal" button, bug 2026-11-16 evening: windows popped up while the page
# built a map). Start-Process -WindowStyle Hidden still gives the program a NEW console (shown when the default terminal - e.g. Windows
# Terminal - ignores the hidden flag); System.Diagnostics.Process with CreateNoWindow starts it with CREATE_NO_WINDOW: no console window
# at all, whoever the default terminal is. The program inherits this process's environment and working directory; its own output
# goes nowhere (the Unreal commandlets write -abslog files).
#   . (Join-Path $PSScriptRoot "hidden_process.ps1")
#   $Proc = Start-HiddenProcess "C:\...\UnrealEditor-Cmd.exe" $ArgumentString     # -> [System.Diagnostics.Process]
function Start-HiddenProcess([string]$FilePath, [string]$Arguments) {
    $Psi = New-Object System.Diagnostics.ProcessStartInfo
    $Psi.FileName = $FilePath
    $Psi.Arguments = $Arguments
    $Psi.UseShellExecute = $false
    $Psi.CreateNoWindow = $true
    $Psi.WorkingDirectory = (Get-Location).ProviderPath
    return [System.Diagnostics.Process]::Start($Psi)
}
