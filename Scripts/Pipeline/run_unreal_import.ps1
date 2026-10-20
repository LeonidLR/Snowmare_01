# Character single-slot pipeline, stage 7: headless import of the optimized character (saves NEW assets only).
# Usage:
#   powershell -ExecutionPolicy Bypass -File Scripts/Pipeline/run_unreal_import.ps1 `
#       -AssetDir ArtPipeline/Characters/Operatives/Post_Apo_Survivor_a -Dest /Game/Characters/Operatives/Optimized/Post_Apo_Survivor_a
# Refuses to run while any UnrealEditor process is up (the user's editor would hold / later overwrite the packages).
# Takes a Content snapshot before and after (Scripts/Pipeline/content_snapshot.py) and fails if anything outside -Dest and
# the master material folder changed.
param(
    [Parameter(Mandatory = $true)][string]$AssetDir,
    [Parameter(Mandatory = $true)][string]$Dest,
    [string]$MasterDir = "/Game/Characters/Materials",
    [switch]$AllowExisting,
    [string]$VariantsOnly = "",
    [string]$Engine = "C:\Program Files\Epic Games\UE_5.8"
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not [System.IO.Path]::IsPathRooted($AssetDir)) { $AssetDir = Join-Path $root $AssetDir }
if (Get-Process -Name "UnrealEditor*" -ErrorAction SilentlyContinue) { Write-Host "An UnrealEditor process is running - close it first."; exit 1 }
$review = Join-Path $AssetDir "06_Review"
$before = Join-Path $review "content_before_import.json"
$after = Join-Path $review "content_after_import.json"
python (Join-Path $PSScriptRoot "content_snapshot.py") snap $before --exclude $Dest --exclude $MasterDir
$pyArgs = @("--asset-dir", $AssetDir, "--dest", $Dest, "--master-dir", $MasterDir)
if ($AllowExisting) { $pyArgs += "--allow-existing" }
if ($VariantsOnly) { $pyArgs += @("--variants-only", $VariantsOnly) }
$env:CODEX_PIPELINE_ARGS = ConvertTo-Json -Compress $pyArgs
$log = Join-Path $AssetDir "05_Export\unreal_import.log"
$script = Join-Path $PSScriptRoot "unreal_import_optimized.py"
$project = Join-Path $root "CodexTactics.uproject"
$argLine = "`"$project`" -run=pythonscript -script=`"$script`" -unattended -AllowCommandletRendering -RenderOffscreen -nosplash -nop4 -stdout -FullStdOutLogOutput"
$p = Start-Process -FilePath (Join-Path $Engine "Engine\Binaries\Win64\UnrealEditor-Cmd.exe") -ArgumentList $argLine -NoNewWindow -Wait -PassThru -RedirectStandardOutput $log
python (Join-Path $PSScriptRoot "content_snapshot.py") snap $after --exclude $Dest --exclude $MasterDir
python (Join-Path $PSScriptRoot "content_snapshot.py") diff $before $after
$diffCode = $LASTEXITCODE
$done = Select-String -Path $log -Pattern "PIPELINE_IMPORT_DONE" | Select-Object -First 1
if (-not $done) { Write-Host "Import did not finish (exit $($p.ExitCode)) - see $log"; exit 1 }
Write-Host $done.Line
if ($diffCode -ne 0) { Write-Host "Content outside the new folders changed - see the diff above"; exit 1 }
