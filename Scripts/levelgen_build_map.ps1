# Builds a generated map from an ACCEPTED level-generator layout (docs/design/levelgen/LEVEL_GENERATOR.md section 7) and bakes its navmesh:
#   powershell -ExecutionPolicy Bypass -File Scripts\levelgen_build_map.ps1 [-Layout gen_ref_01_02_depot_1] [-Target /Game/Maps/Generated/L_Gen_Depot_1] [-Fresh] [-NoBake]
# Runs Scripts/Editor/build_levelgen_map.py in the editor commandlet (the project's editor must be closed; takes the shared build lock,
# $env:CODEX_AGENT names the agent), then Scripts/bake_navmesh.ps1 -Map <map>. Report: Saved/Logs/<map leaf>Layout.txt.
param(
    [string]$Layout = "",
    [string]$Target = "",
    [switch]$Fresh,
    [switch]$NoBake,
    [int]$TimeoutSeconds = 1800
)

$ErrorActionPreference = "Stop"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$ProjectDir = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"
$Script = Join-Path $ProjectDir "Scripts\Editor\build_levelgen_map.py"

$ProjectFull = [IO.Path]::GetFullPath($Project)
if (Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor.exe'" | Where-Object {
        $_.CommandLine -and ($_.CommandLine.Replace('/', '') -like "*$ProjectFull*") -and ($_.CommandLine -notmatch '-run=|-game') }) {
    Write-Host "Unreal Editor (CodexTactics) is open on this checkout: close it first (it holds the map packages)." -ForegroundColor Yellow
    exit 2
}

if ($Layout) {
    $LayoutPath = if ($Layout.EndsWith(".json")) { $Layout } else { Join-Path $ProjectDir "Content\Data\LevelGen\accepted\$Layout.json" }
    if (-not (Test-Path $LayoutPath)) { Write-Host "no accepted layout $LayoutPath" -ForegroundColor Red; exit 2 }
    $env:CODEX_LEVELGEN_LAYOUT = [IO.Path]::GetFullPath($LayoutPath)
}
if ($Target) { $env:CODEX_LEVELGEN_TARGET = $Target }
if ($Fresh) { $env:CODEX_LEVELGEN_FRESH = "1" }

. (Join-Path $PSScriptRoot "agent_lock.ps1")
$Owned = Enter-AgentLock "levelgen build map $Layout"
try {
    $LogFile = Join-Path $ProjectDir "Saved\Logs\LevelGenBuildMap.log"
    $CmdArgs = "`"$Project`" -run=pythonscript -script=`"$Script`" -unattended -nullrhi -nosplash -nosound -NoTelemetry `"-abslog=$LogFile`""
    Write-Host "levelgen: building the map from $($env:CODEX_LEVELGEN_LAYOUT) ..."
    $Proc = Start-Process "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -ArgumentList $CmdArgs -WindowStyle Hidden -PassThru
    $null = $Proc.Handle
    if (-not $Proc.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue
        Write-Host "levelgen: TIMEOUT after $TimeoutSeconds s ($LogFile)" -ForegroundColor Red
        exit 3
    }
    $Report = Get-ChildItem (Join-Path $ProjectDir "Saved\Logs") -Filter "*Layout.txt" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($Report) { Write-Host "--- $($Report.Name) ---"; Get-Content $Report.FullName | Select-Object -Last 12 }
    $MapLine = if ($Report) { (Get-Content $Report.FullName | Select-String -Pattern 'saved (/Game/[^:]+):' | Select-Object -First 1) } else { $null }
    $Map = if ($MapLine) { $MapLine.Matches[0].Groups[1].Value } elseif ($Target) { $Target } else { "" }
    if (-not $Map) { Write-Host "levelgen: the map was not saved (see $LogFile)" -ForegroundColor Red; exit 4 }
} finally {
    if ($Owned) { Exit-AgentLock }
}

if (-not $NoBake) {
    $Leaf = ($Map -split '/')[-1]
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "bake_navmesh.ps1") -Map $Map
    if ($LASTEXITCODE -ne 0) { Write-Host "levelgen: navmesh bake failed for $Leaf" -ForegroundColor Red; exit $LASTEXITCODE }
}
Write-Host "levelgen: $Map ready (smoke: Scripts\smoke_parallel.ps1 -Commands LevelGenRouteSmoke -ExtraArgs `"-Multiprocess`")" -ForegroundColor Green
