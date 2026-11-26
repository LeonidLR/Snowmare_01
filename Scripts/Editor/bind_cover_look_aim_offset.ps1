# Binds ABP_Operative aim-offset pins (G025). The editor of this checkout must be closed.
# Result: Saved\Logs\bind_cover_look_aim_offset.txt
$ErrorActionPreference = "Stop"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$ProjectDir = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"
$Script = Join-Path $PSScriptRoot "bind_cover_look_aim_offset.py"
$Log = Join-Path $ProjectDir "Saved\Logs\bind_cover_look_aim_offset_cmd.log"
$Report = Join-Path $ProjectDir "Saved\Logs\bind_cover_look_aim_offset.txt"
. (Join-Path (Split-Path $PSScriptRoot -Parent) "hidden_process.ps1")
. (Join-Path (Split-Path $PSScriptRoot -Parent) "agent_lock.ps1")
$Owned = Enter-AgentLock "bind cover look aim offset"
try {
    $Proc = Start-HiddenProcess "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "`"$Project`" -run=pythonscript `"-script=$Script`" -unattended -nullrhi -nosplash -nosound -NoTelemetry `"-abslog=$Log`""
    $null = $Proc.Handle
    if (-not $Proc.WaitForExit(600000)) {
        Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue
        Write-Host "TIMEOUT after 600 s"
        exit 3
    }
    Write-Host "Exit code $($Proc.ExitCode). Log: $Log"
} finally {
    if ($Owned) { Exit-AgentLock }
}
if (Test-Path $Report) {
    Get-Content $Report | ForEach-Object { Write-Host $_ }
}
exit 0
