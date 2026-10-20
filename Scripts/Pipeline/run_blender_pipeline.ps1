# Character single-slot pipeline, stages 2-6 in Blender (background). Stage 1 (Unreal export): run_unreal_export.ps1.
# Stages: clean -> reduce (decimate, non-destructive: *_reduce.blend) -> atlas -> bake (high -> low) -> export -> review
# Usage:
#   powershell -ExecutionPolicy Bypass -File Scripts/Pipeline/run_blender_pipeline.ps1 `
#       -AssetDir ArtPipeline/Characters/Operatives/Post_Apo_Survivor_a [-Stages clean,atlas,bake,export,review] `
#       [-Res 4096] [-ReduceArgs "--target-tris 20000"] [-AtlasArgs "--margin-px 8"] [-BakeArgs "--margin-px 8"]
#   (enemies: -Res 2048 -ReduceArgs "--target-tris 20000" -AtlasArgs "--margin-px 8 --border-px 2" -BakeArgs "--margin-px 8")
# Each stage writes <AssetDir>/03_Blender/<stage>.log and merges its numbers into 06_Review/summary.json.
param(
    [Parameter(Mandatory = $true)][string]$AssetDir,
    [string[]]$Stages = @("clean", "reduce", "atlas", "bake", "export", "review"),
    [int]$Res = 4096,
    [hashtable]$ExtraArgs = @{},
    [string]$CleanArgs = "", [string]$ReduceArgs = "", [string]$AtlasArgs = "", [string]$BakeArgs = "",
    [string]$ExportArgs = "", [string]$ReviewArgs = "",
    [string]$Blender = "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
)
$ErrorActionPreference = "Stop"
$Stages = @($Stages | ForEach-Object { $_ -split "," } | Where-Object { $_ })  # -File passes "a,b" as one string
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not [System.IO.Path]::IsPathRooted($AssetDir)) { $AssetDir = Join-Path $root $AssetDir }
$markers = @{ clean = "CLEAN_DONE"; reduce = "REDUCE_DONE"; atlas = "ATLAS_DONE"; bake = "BAKE_DONE"; export = "EXPORT_DONE"; review = "REVIEW_DONE" }
foreach ($stage in $Stages) {
    $script = Join-Path $PSScriptRoot "blender_$stage.py"
    New-Item -ItemType Directory -Force (Join-Path $AssetDir "03_Blender") | Out-Null
    $log = Join-Path $AssetDir "03_Blender\$stage.log"
    $bargs = @("--background", "--factory-startup", "--python", $script, "--", "--asset-dir", $AssetDir)
    if ($stage -in @("atlas", "bake")) { $bargs += @("--res", "$Res") }
    if ($ExtraArgs.ContainsKey($stage)) { $bargs += ($ExtraArgs[$stage] -split " ") }
    $stageArgs = @{ clean = $CleanArgs; reduce = $ReduceArgs; atlas = $AtlasArgs; bake = $BakeArgs; export = $ExportArgs; review = $ReviewArgs }[$stage]
    if ($stageArgs) { $bargs += ($stageArgs -split " " | Where-Object { $_ }) }
    Write-Host "[$stage] running ..."
    $p = Start-Process -FilePath $Blender -ArgumentList ($bargs | ForEach-Object { if ($_ -match " ") { "`"$_`"" } else { $_ } }) `
        -NoNewWindow -Wait -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
    $done = Select-String -Path $log -Pattern $markers[$stage] | Select-Object -First 1
    if (-not $done) { Write-Host "[$stage] FAILED (exit $($p.ExitCode)) - see $log"; exit 1 }
    Write-Host $done.Line
}
