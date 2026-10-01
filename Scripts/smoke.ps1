# Runs a headless in-game check (dev console command) on a map and prints its log lines.
# Usage: powershell -ExecutionPolicy Bypass -File Scripts\smoke.ps1 [-Command CodexTactics.MovementSmoke] [-Map /Game/Maps/L_MovementTest] [-Extra "-ForceMainMenu"] [-Log Smoke.log]
# -Log names the log file in Saved\Logs (verify_all.ps1 gives every check its own so they can run side by side).
param(
    [string]$Command = "CodexTactics.MovementSmoke",
    [string]$Map = "/Game/Maps/L_MovementTest",
    [string]$Extra = "",
    [string]$Log = "Smoke.log"
)

$ErrorActionPreference = "Stop"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$ProjectDir = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"
$LogFile = Join-Path $ProjectDir (Join-Path "Saved\Logs" $Log)

. (Join-Path $PSScriptRoot "agent_lock.ps1")
$Owned = Enter-AgentLock "smoke $Command"
try {
    & "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $Project $Map -game -nullrhi -nosplash -nosound -unattended -windowed -FORCELOGFLUSH `
        "-ExecCmds=$Command" "-abslog=$LogFile" $Extra | Out-Null
} finally {
    if ($Owned) { Exit-AgentLock }
}

$lines = Select-String -Path $LogFile -Pattern "Smoke|LogCodexTactics: (Error|Warning)|LogNavigation: (Error|Warning)" |
    ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
$lines | ForEach-Object { Write-Host $_ }
if ($lines -match "RESULT: PASS") { exit 0 }
exit 1
