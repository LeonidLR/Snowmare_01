# Character single-slot pipeline: delete the intermediates of an asset once it is imported into Unreal
# (user request 2026-10-20: "every temporary Blender file, every texture preview, every file that is not
# brought into Unreal - delete it, it takes a lot of space").
#
# Kept: 04_Textures_Baked final maps (*_BaseColor/_Normal/_ORM/_Sheen/_Emissive/_Subsurface.png) and 05_Export/*.fbx (what Unreal
# imported), every *.md / *.json metadata file (except the import-guard content snapshots) and anything git tracks.
# Deleted: 01_Source FBX / anims / logs, 02_Textures_Source, 03_Blender (.blend + logs), texture previews,
# 06_Review renders and in-game shots, 05_Export logs, content snapshots.
# Safety: refuses unless the optimized asset exists in Content (Characters/*/Optimized/<Asset>/SK_*.uasset);
# only touches paths inside ArtPipeline/<Category>/<Group>/<Asset>/. -DryRun lists without deleting.
#
#   powershell -ExecutionPolicy Bypass -File Scripts/Pipeline/cleanup_intermediates.ps1 -Asset ArtPipeline/Characters/Operatives/Post_Apo_Survivor_a [-DryRun]
param(
    [Parameter(Mandatory = $true)][string]$Asset,
    [switch]$DryRun
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$assetDir = (Resolve-Path (Join-Path $repo $Asset)).Path
$pipelineRoot = Join-Path $repo "ArtPipeline"
if (-not $assetDir.StartsWith($pipelineRoot + "\")) { throw "Not inside ArtPipeline: $assetDir" }
$name = Split-Path $assetDir -Leaf

$imported = Get-ChildItem -Path (Join-Path $repo "Content\Characters") -Directory -Filter "Optimized" -Recurse -ErrorAction SilentlyContinue |
    ForEach-Object { Join-Path $_.FullName $name } | Where-Object { Test-Path $_ } |
    Where-Object { @(Get-ChildItem $_ -Filter "SK_*.uasset").Count -gt 0 }
if (-not $imported) { throw "No imported asset Content/Characters/*/Optimized/$name/SK_*.uasset - import first, nothing deleted" }

$tracked = @{}
Push-Location $repo
try { git ls-files -- (Resolve-Path -Relative $assetDir) | ForEach-Object { $tracked[(Join-Path $repo $_).Replace('/', '\')] = $true } }
finally { Pop-Location }

$finalMaps = '_(BaseColor|Normal|ORM|Sheen|Emissive|Subsurface)\.png$'
$victims = Get-ChildItem -Path $assetDir -File -Recurse | Where-Object {
    $file = $_  # inside switch, $_ is the switch value
    $rel = $file.FullName.Substring($assetDir.Length + 1)
    $stage = $rel.Split('\')[0]
    if ($tracked.ContainsKey($file.FullName)) { return $false }
    if ($file.Name -match '^content_.*_import\.json$') { return $true }
    if ($file.Extension -in '.md', '.json') { return $false }
    switch ($stage) {
        '04_Textures_Baked' { return -not ($file.Name -match $finalMaps) }
        '05_Export' { return $file.Extension -ne '.fbx' }
        default { return $stage -in '01_Source', '02_Textures_Source', '03_Blender', '06_Review' }
    }
}

$bytes = ($victims | Measure-Object Length -Sum).Sum
Write-Output ("{0}: {1} files, {2:N0} MB to delete (imported: {3})" -f $name, @($victims).Count, ($bytes / 1MB), ($imported -join ', '))
if ($DryRun) { $victims | ForEach-Object { "  would delete " + $_.FullName.Substring($assetDir.Length + 1) }; return }
$victims | Remove-Item -Force
Get-ChildItem -Path $assetDir -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending |
    Where-Object { @(Get-ChildItem $_.FullName -Force).Count -eq 0 -and $_.Parent.FullName -ne $assetDir } |
    Remove-Item -Force
Write-Output ("{0}: deleted, folder now {1:N0} MB" -f $name, ((Get-ChildItem $assetDir -File -Recurse | Measure-Object Length -Sum).Sum / 1MB))
