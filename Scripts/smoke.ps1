# Runs a headless in-game check (dev console command) on a map and prints its log lines.
# Usage: powershell -ExecutionPolicy Bypass -File Scripts\smoke.ps1 [-Command CodexTactics.MovementSmoke] [-Map /Game/Maps/L_MovementTest] [-Extra "-ForceMainMenu"] [-Log Smoke.log]
# -Log names the log file in Saved\Logs (verify_all.ps1 gives every check its own so they can run side by side).
# -TimeoutSeconds kills a game that never exits (e.g. the command is missing because the binaries are stale).
param(
    [string]$Command = "CodexTactics.MovementSmoke",
    [string]$Map = "/Game/Maps/L_MovementTest",
    [string]$Extra = "",
    [string]$Log = "Smoke.log",
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = "Stop"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$ProjectDir = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"
$LogFile = Join-Path $ProjectDir (Join-Path "Saved\Logs" $Log)

. (Join-Path $PSScriptRoot "agent_lock.ps1")
$Owned = Enter-AgentLock "smoke $Command"
try {
    # Start-Process takes one argument string: quote the paths (the project path has a space).
    $GameArgs = "`"$Project`" $Map -game -nullrhi -nosplash -nosound -unattended -windowed -FORCELOGFLUSH -NoTelemetry `"-ExecCmds=$Command`" `"-abslog=$LogFile`" $Extra"
    $Game = Start-Process "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -ArgumentList $GameArgs -WindowStyle Hidden -PassThru
    if (-not $Game.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $Game.Id -Force -ErrorAction SilentlyContinue
        Write-Host "Smoke TIMEOUT after $TimeoutSeconds s ($Command never finished: missing command / stale binaries?)"
    }
} finally {
    if ($Owned) { Exit-AgentLock }
}

$lines = Select-String -Path $LogFile -Pattern "Smoke|LogCodexTactics: (Error|Warning)|LogNavigation: (Error|Warning)" |
    ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
$lines | ForEach-Object { Write-Host $_ }
if ($lines -match "RESULT: PASS") { exit 0 }
exit 1
