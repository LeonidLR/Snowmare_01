# Builds a generated map from an ACCEPTED level-generator layout (docs/design/levelgen/LEVEL_GENERATOR.md section 7) and bakes its navmesh:
#   powershell -ExecutionPolicy Bypass -File Scripts\levelgen_build_map.ps1 [-Layout gen_ref_01_02_depot_1] [-Target /Game/Maps/Generated/L_Gen_Depot_1] [-Fresh] [-NoBake]
#     [-StatusFile <json>] [-LockWaitMinutes 60]
# Runs Scripts/Editor/build_levelgen_map.py in the editor commandlet (the project's editor must be closed; takes the shared build lock,
# $env:CODEX_AGENT names the agent), then Scripts/bake_navmesh.ps1 -Map <map>. Report: Saved/Logs/<map leaf>Layout.txt.
# Machine-readable result (Training Ground pipeline, 2026-11-02; Scripts/job_status.ps1, schema codex.job_status/1): -StatusFile (default
# Saved\LevelGen\build_map_status.json) - state running / waiting_lock / done / failed / refused, reason editor_open / lock_held / timeout /
# no_layout / not_saved / navmesh, result { layout, target, map, level_json, report, navbake_log, drop_links, actors }.
# Exit codes: 0 done, 2 refused (editor open / no layout), 3 lock held / timeout, 4 the map was not saved, 5 the navmesh bake failed.
param(
    [string]$Layout = "",
    [string]$Target = "",
    [switch]$Fresh,
    [switch]$NoBake,
    [int]$TimeoutSeconds = 1800,
    [string]$StatusFile = "",
    [int]$LockWaitMinutes = 60
)

$ErrorActionPreference = "Stop"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$ProjectDir = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"
$Script = Join-Path $ProjectDir "Scripts\Editor\build_levelgen_map.py"
$LogFile = Join-Path $ProjectDir "Saved\Logs\LevelGenBuildMap.log"
if (-not $StatusFile) { $StatusFile = Join-Path $ProjectDir "Saved\LevelGen\build_map_status.json" }

. (Join-Path $PSScriptRoot "agent_lock.ps1")
. (Join-Path $PSScriptRoot "job_status.ps1")
$Job = New-JobStatus $StatusFile "build_map" $LogFile
Set-JobStatus $Job @{ layout = $Layout; target = $Target }

if (Test-ProjectEditorOpen $Project) {
    $Msg = "Unreal Editor (CodexTactics) is open on this checkout: close it first (it holds the map packages)."
    Write-Host $Msg -ForegroundColor Yellow
    Complete-JobStatus $Job "refused" 2 $Msg "editor_open"
    exit 2
}

if ($Layout) {
    $LayoutPath = if ($Layout.EndsWith(".json")) { $Layout } else { Join-Path $ProjectDir "Content\Data\LevelGen\accepted\$Layout.json" }
    if (-not (Test-Path $LayoutPath)) {
        Write-Host "no accepted layout $LayoutPath" -ForegroundColor Red
        Complete-JobStatus $Job "refused" 2 "no accepted layout $LayoutPath" "no_layout"
        exit 2
    }
    $env:CODEX_LEVELGEN_LAYOUT = [IO.Path]::GetFullPath($LayoutPath)
    Set-JobStatus $Job @{ layout = $env:CODEX_LEVELGEN_LAYOUT }
}
if ($Target) { $env:CODEX_LEVELGEN_TARGET = $Target }
if ($Fresh) { $env:CODEX_LEVELGEN_FRESH = "1" }

$Owned = Wait-JobLock $Job "levelgen build map $Layout" $LockWaitMinutes
$Map = ""
$Report = $null
try {
    $CmdArgs = "`"$Project`" -run=pythonscript -script=`"$Script`" -unattended -nullrhi -nosplash -nosound -NoTelemetry `"-abslog=$LogFile`""
    Write-Host "levelgen: building the map from $($env:CODEX_LEVELGEN_LAYOUT) ..."
    Set-JobStatus $Job @{} "running" "building the map (editor commandlet)"
    $Proc = Start-Process "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -ArgumentList $CmdArgs -WindowStyle Hidden -PassThru
    $null = $Proc.Handle
    if (-not $Proc.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue
        Write-Host "levelgen: TIMEOUT after $TimeoutSeconds s ($LogFile)" -ForegroundColor Red
        Complete-JobStatus $Job "failed" 3 "the build commandlet timed out after $TimeoutSeconds s" "timeout"
        exit 3
    }
    $Report = Get-ChildItem (Join-Path $ProjectDir "Saved\Logs") -Filter "*Layout.txt" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($Report) { Write-Host "--- $($Report.Name) ---"; Get-Content $Report.FullName | Select-Object -Last 12 }
    $MapLine = if ($Report) { (Get-Content $Report.FullName | Select-String -Pattern 'saved (/Game/[^:]+): True' | Select-Object -First 1) } else { $null }
    $Map = if ($MapLine) { $MapLine.Matches[0].Groups[1].Value } else { "" }
    $Leaf = if ($Map) { ($Map -split '/')[-1] } else { "" }
    $Placed = if ($Report) { Get-Content $Report.FullName | Select-String -Pattern '^placed (\d+) actors' | Select-Object -First 1 } else { $null }
    $Drops = if ($Report) { @(Get-Content $Report.FullName | Select-String -Pattern '^\s+Drop_\S+: ').Count } else { 0 }
    Set-JobStatus $Job @{ map = $Map; report = $(if ($Report) { $Report.FullName } else { "" }); drop_links = $Drops
        actors = $(if ($Placed) { [int]$Placed.Matches[0].Groups[1].Value } else { 0 })
        level_json = $(if ($Leaf) { Join-Path $ProjectDir "Content\Data\LevelJson\$Leaf.json" } else { "" }) }
    if (-not $Map) {
        Write-Host "levelgen: the map was not saved (see $LogFile)" -ForegroundColor Red
        Complete-JobStatus $Job "failed" 4 "the map was not saved (see the report / log)" "not_saved"
        exit 4
    }
} finally {
    if ($Owned) { Exit-AgentLock }
}

if (-not $NoBake) {
    $Leaf = ($Map -split '/')[-1]
    Set-JobStatus $Job @{ navbake_log = (Join-Path $ProjectDir "Saved\Logs\NavBake-$Leaf.log") } "running" "baking the navmesh"
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "bake_navmesh.ps1") -Map $Map
    if ($LASTEXITCODE -ne 0) {
        Write-Host "levelgen: navmesh bake failed for $Leaf" -ForegroundColor Red
        Complete-JobStatus $Job "failed" 5 "the navmesh bake failed for $Leaf (exit $LASTEXITCODE)" "navmesh"
        exit 5
    }
}
Complete-JobStatus $Job "done" 0 "$Map ready"
Write-Host "levelgen: $Map ready (route check: Scripts\levelgen_route_smoke.ps1 -Map $Map)" -ForegroundColor Green
