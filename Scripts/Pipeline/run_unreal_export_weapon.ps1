# Weapon pipeline, stage 0: headless, read-only Unreal inventory + FBX / texture export of the meshes in a content folder
# (used for the Shotgun D-1 Fab import: the pack ships no FBX).
# Usage:
#   powershell -ExecutionPolicy Bypass -File Scripts/Pipeline/run_unreal_export_weapon.ps1 `
#       -Folder /Game/Fab/_Model_D-1__Shotgun -Out ArtPipeline/Weapons/ShotgunD1 [-Project <path to a .uproject>]
# Nothing is saved or modified. -Project may point at a content-only scratch project whose Content holds a COPY of the pack
# (the 2026-11-08 shotgun export ran like that: a worktree without Binaries cannot start the commandlet for this project).
# Note: the project path must not contain 8.3 short names (C:\Users\NAME~1): the engine cannot map them to a package path.
param(
    [Parameter(Mandatory = $true)][string]$Folder,
    [Parameter(Mandatory = $true)][string]$Out,
    [string]$Meshes = "",
    [string]$Project = "",
    [string]$Engine = "C:\Program Files\Epic Games\UE_5.8"
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Project) { $Project = Join-Path $root "CodexTactics.uproject" }
if (-not [System.IO.Path]::IsPathRooted($Out)) { $Out = Join-Path $root $Out }
New-Item -ItemType Directory -Force (Join-Path $Out "01_Source") | Out-Null
$script = Join-Path $PSScriptRoot "unreal_export_weapon.py"
$pyArgs = @("--folder", $Folder, "--out", $Out)
if ($Meshes) { $pyArgs += @("--meshes", $Meshes) }
$env:CODEX_PIPELINE_ARGS = ConvertTo-Json -Compress $pyArgs
$log = Join-Path $Out "01_Source\unreal_export.log"
$cmd = Join-Path $Engine "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$argLine = "`"$Project`" -run=pythonscript -script=`"$script`" -unattended -AllowCommandletRendering -RenderOffscreen -nosplash -nop4 -stdout -FullStdOutLogOutput"
$p = Start-Process -FilePath $cmd -ArgumentList $argLine -NoNewWindow -Wait -PassThru -RedirectStandardOutput $log
$done = Select-String -Path $log -Pattern "PIPELINE_INVENTORY_DONE" | Select-Object -First 1
if ($done) { Write-Host $done.Line } else { Write-Host "Export did not finish (exit $($p.ExitCode)) - see $log"; exit 1 }
