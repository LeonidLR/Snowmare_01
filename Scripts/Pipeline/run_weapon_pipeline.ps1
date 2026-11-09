# Weapon single-material pipeline, stages 1-5 in Blender (background) + the review sheets (system Python + Pillow).
# Stage 0 (Unreal, read-only export of a Fab pack): run_unreal_export_weapon.ps1 (the pistol starts from the pack's FBX).
#   prep    blender_weapon_prep.py    high parts in weapon space (centimetres), mount transforms
#   reduce  blender_weapon_reduce.py  decimate to the budgets in weapon.json + ONE atlas uv (artist islands repacked)
#   bake    blender_weapon_bake.py    Cycles projection: BaseColor / Normal / ORM, 1K (res in weapon.json)
#   export  blender_weapon_export.py  one FBX per part, one material slot `M_<Asset>`, round-trip checked
#   review  blender_weapon_review.py  before / after renders (cells) + weapon_review_sheet.py (labelled sheets)
# Usage:
#   powershell -ExecutionPolicy Bypass -File Scripts/Pipeline/run_weapon_pipeline.ps1 -AssetDir ArtPipeline/Weapons/Pistol9mm [-Stages prep,reduce,bake,export,review] [-Only Part,Part] [-BakeArgs "--dilate-px 6"]
# Each stage writes <AssetDir>/03_Blender/<stage>.log and merges its numbers into 06_Review/summary.json.
param(
    [Parameter(Mandatory = $true)][string]$AssetDir,
    [string[]]$Stages = @("prep", "reduce", "bake", "export", "review"),
    [string]$Only = "",
    [string]$ReduceArgs = "", [string]$BakeArgs = "", [string]$ExportArgs = "", [string]$ReviewArgs = "",
    [string]$Blender = "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
)
$ErrorActionPreference = "Stop"
$Stages = @($Stages | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not [System.IO.Path]::IsPathRooted($AssetDir)) { $AssetDir = Join-Path $root $AssetDir }
$markers = @{ prep = "PREP_DONE"; reduce = "REDUCE_DONE"; bake = "BAKE_DONE"; export = "EXPORT_DONE"; review = "REVIEW_DONE" }
$extra = @{ reduce = $ReduceArgs; bake = $BakeArgs; export = $ExportArgs; review = $ReviewArgs }
foreach ($stage in $Stages) {
    $script = Join-Path $PSScriptRoot "blender_weapon_$stage.py"
    New-Item -ItemType Directory -Force (Join-Path $AssetDir "03_Blender") | Out-Null
    $log = Join-Path $AssetDir "03_Blender\$stage.log"
    $bargs = @("--background", "--factory-startup", "--python", $script, "--", "--asset-dir", $AssetDir)
    if ($Only -and $stage -in @("reduce", "bake", "review")) { $bargs += @("--only", $Only) }
    if ($extra[$stage]) { $bargs += ($extra[$stage] -split " " | Where-Object { $_ }) }
    Write-Host "[$stage] running ..."
    $p = Start-Process -FilePath $Blender -ArgumentList ($bargs | ForEach-Object { if ($_ -match " ") { "`"$_`"" } else { $_ } }) `
        -NoNewWindow -Wait -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
    $done = Select-String -Path $log -Pattern $markers[$stage] | Select-Object -First 1
    if (-not $done) { Write-Host "[$stage] FAILED (exit $($p.ExitCode)) - see $log"; exit 1 }
    Write-Host $done.Line
}
if ($Stages -contains "review") {
    $py = (Get-Command python -ErrorAction SilentlyContinue)
    if ($py) { & python (Join-Path $PSScriptRoot "weapon_review_sheet.py") $AssetDir } else { Write-Host "python / Pillow not found: sheets skipped (cells are in 06_Review/cells)" }
}
