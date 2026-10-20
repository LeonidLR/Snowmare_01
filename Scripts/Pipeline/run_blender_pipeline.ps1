# Character single-slot pipeline, stages 2-6 in Blender (background). Stage 1 (Unreal export): run_unreal_export.ps1.
# Usage:
#   powershell -ExecutionPolicy Bypass -File Scripts/Pipeline/run_blender_pipeline.ps1 `
#       -AssetDir ArtPipeline/Characters/Operatives/Post_Apo_Survivor_a [-Stages clean,atlas,bake,export,review] `
#       [-Res 4096] [-ExtraArgs @{atlas="--search-time 120"}]
# Each stage writes <AssetDir>/03_Blender/<stage>.log and merges its numbers into 06_Review/summary.json.
param(
    [Parameter(Mandatory = $true)][string]$AssetDir,
    [string[]]$Stages = @("clean", "atlas", "bake", "export", "review"),
    [int]$Res = 4096,
    [hashtable]$ExtraArgs = @{},
    [string]$Blender = "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
)
$ErrorActionPreference = "Stop"
$Stages = @($Stages | ForEach-Object { $_ -split "," } | Where-Object { $_ })  # -File passes "a,b" as one string
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not [System.IO.Path]::IsPathRooted($AssetDir)) { $AssetDir = Join-Path $root $AssetDir }
$markers = @{ clean = "CLEAN_DONE"; atlas = "ATLAS_DONE"; bake = "BAKE_DONE"; export = "EXPORT_DONE"; review = "REVIEW_DONE" }
foreach ($stage in $Stages) {
    $script = Join-Path $PSScriptRoot "blender_$stage.py"
    $log = Join-Path $AssetDir "03_Blender\$stage.log"
    $bargs = @("--background", "--factory-startup", "--python", $script, "--", "--asset-dir", $AssetDir)
    if ($stage -in @("atlas", "bake")) { $bargs += @("--res", "$Res") }
    if ($ExtraArgs.ContainsKey($stage)) { $bargs += ($ExtraArgs[$stage] -split " ") }
    Write-Host "[$stage] running ..."
    $p = Start-Process -FilePath $Blender -ArgumentList ($bargs | ForEach-Object { if ($_ -match " ") { "`"$_`"" } else { $_ } }) `
        -NoNewWindow -Wait -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
    $done = Select-String -Path $log -Pattern $markers[$stage] | Select-Object -First 1
    if (-not $done) { Write-Host "[$stage] FAILED (exit $($p.ExitCode)) - see $log"; exit 1 }
    Write-Host $done.Line
}
