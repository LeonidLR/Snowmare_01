# Runs a headless in-game check (dev console command) on a map and prints its log lines.
# Usage: powershell -ExecutionPolicy Bypass -File Scripts\smoke.ps1 [-Command CodexTactics.MovementSmoke] [-Map /Game/Maps/L_MovementTest] [-Extra "-ForceMainMenu"]
param(
    [string]$Command = "CodexTactics.MovementSmoke",
    [string]$Map = "/Game/Maps/L_MovementTest",
    [string]$Extra = ""
)

$ErrorActionPreference = "Stop"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$ProjectDir = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"
$LogFile = Join-Path $ProjectDir "Saved\Logs\Smoke.log"

& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $Project $Map -game -nullrhi -nosplash -nosound -unattended -windowed -FORCELOGFLUSH `
    "-ExecCmds=$Command" "-abslog=$LogFile" $Extra | Out-Null

$lines = Select-String -Path $LogFile -Pattern "Smoke|LogCodexTactics: (Error|Warning)|LogNavigation: (Error|Warning)" |
    ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
$lines | ForEach-Object { Write-Host $_ }
if ($lines -match "RESULT: PASS") { exit 0 }
exit 1
