# Copies a run's champion weights over the game file - the ONLY way trainer output reaches Content (the trainer never
# writes there). The current file is backed up first and the copy is hash-verified.
#   powershell -ExecutionPolicy Bypass -File Scripts\Trainer\apply_champion.ps1 -RunId pmc-main [-Target Content\Data\AI\pmc_commander.json] [-WhatIf]
# Backup: Saved\Trainer\<RunId>\backups\<name>.<timestamp>.json. Undo: copy that backup back. Commit the new weights yourself.
param(
    [Parameter(Mandatory = $true)][string]$RunId,
    [string]$Target = "Content\Data\AI\pmc_commander.json",
    [switch]$WhatIf
)
$ErrorActionPreference = "Stop"
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$RunDir = Join-Path $Repo "Saved\Trainer\$RunId"
$Champion = Join-Path $RunDir "champion_weights.json"
if (-not (Test-Path $Champion)) { Write-Host "No champion in $RunDir." -ForegroundColor Red; exit 1 }
$TargetPath = if ([System.IO.Path]::IsPathRooted($Target)) { $Target } else { Join-Path $Repo $Target }
$Meta = Get-Content (Join-Path $RunDir "champion_meta.json") -Raw | ConvertFrom-Json
Write-Host "Champion $($Meta.id) (promoted in generation $($Meta.promoted_generation)) -> $TargetPath"
if ($Meta.id -eq "baseline") { Write-Host "The champion is still the baseline: nothing to apply."; exit 0 }
if ($WhatIf) { Write-Host "WhatIf: nothing copied."; exit 0 }
if (Test-Path $TargetPath) {
    $Backups = Join-Path $RunDir "backups"
    New-Item -ItemType Directory -Force $Backups | Out-Null
    $Backup = Join-Path $Backups ("{0}.{1}.json" -f [System.IO.Path]::GetFileNameWithoutExtension($TargetPath), (Get-Date -Format "yyyyMMdd-HHmmss"))
    Copy-Item $TargetPath $Backup
    if ((Get-FileHash $Backup).Hash -ne (Get-FileHash $TargetPath).Hash) { Write-Host "Backup verify failed - aborted." -ForegroundColor Red; exit 2 }
    Write-Host "Backup: $Backup"
} else {
    Write-Host "Target did not exist yet (no backup)." -ForegroundColor Yellow
    New-Item -ItemType Directory -Force (Split-Path $TargetPath) | Out-Null
}
Copy-Item $Champion $TargetPath -Force
if ((Get-FileHash $Champion).Hash -ne (Get-FileHash $TargetPath).Hash) { Write-Host "Copy verify failed." -ForegroundColor Red; exit 2 }
Write-Host "Applied. Review the diff (git diff $Target) and commit it yourself."
