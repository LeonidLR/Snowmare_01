# Kit dressing showcase map (docs/design/levelgen/KIT_DRESS.md): Scripts/Editor/kit_dress.py in the editor commandlet -> bake the navmesh ->
# LevelGenBuildingsSmoke -> rendered pictures. Needs the untracked Soviet kit pack in Content/SovietHouses; the map it writes (under
# /Game/Maps/KitPilot/) references the pack and is never committed.
#   powershell -ExecutionPolicy Bypass -File Scripts\kit_dress.ps1 [-Ref claude/building-generator] [-Layout <accepted layout .json>]
#       [-Target /Game/Maps/KitPilot/L_KitBlocks] [-Grey] [-Steps build,bake,smoke,shots]
# -Ref exports that branch's builder + its gen_bgen_plans_check layout with `kit_dress.py export` (git show, read only) and dresses those;
# -Grey builds the undressed copy (KIT_DRESS_DRESS=0, default target <Target>_Grey): the smoke baseline.
# Refuses while an editor (not a -game / -run= process) is open on this checkout; takes the shared build lock ($env:CODEX_AGENT).
param(
    [string]$Ref = "",
    [string]$LayoutName = "gen_bgen_plans_check",
    [string]$Layout = "",
    [string]$Target = "/Game/Maps/KitPilot/L_KitBlocks",
    [switch]$Grey,
    [string]$Steps = "build,bake,smoke,shots",
    [int]$TimeoutSeconds = 1800
)

$ErrorActionPreference = "Stop"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$ProjectDir = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"
$Script = Join-Path $ProjectDir "Scripts\Editor\kit_dress.py"
$StepList = $Steps -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ }
if ($Grey -and $Target -notlike "*_Grey") { $Target = "${Target}_Grey" }
$Leaf = ($Target -split '/')[-1]

. (Join-Path $PSScriptRoot "agent_lock.ps1")
. (Join-Path $PSScriptRoot "job_status.ps1")
. (Join-Path $PSScriptRoot "hidden_process.ps1")
if (Test-ProjectEditorOpen $Project) {
    Write-Host "Unreal Editor (CodexTactics) is open on this checkout: close it first (it holds the map packages)." -ForegroundColor Yellow
    exit 2
}

if ($Ref) {
    & python $Script export $Ref $LayoutName
    if ($LASTEXITCODE -ne 0) { Write-Host "export of $Ref failed" -ForegroundColor Red; exit 3 }
    $Safe = $Ref -replace '[/\\:]', '_'
    $env:KIT_DRESS_BUILDER = Join-Path $ProjectDir "Saved\KitBlocks\builder\$Safe"
    if (-not $Layout) { $Layout = Join-Path $env:KIT_DRESS_BUILDER "Content\Data\LevelGen\accepted\$LayoutName.json" }
}
if ($Layout) { $env:KIT_DRESS_LAYOUT = [IO.Path]::GetFullPath($Layout) }
$env:KIT_DRESS_TARGET = $Target
$env:KIT_DRESS_DRESS = $(if ($Grey) { "0" } else { "1" })

function Invoke-Hidden([string]$Exe, [string]$ArgString, [string]$What) {
    $Proc = Start-HiddenProcess $Exe $ArgString
    $null = $Proc.Handle
    if (-not $Proc.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue
        Write-Host "$What : TIMEOUT after $TimeoutSeconds s" -ForegroundColor Red
        return $false
    }
    return $true
}

$Failed = $false
if ($StepList -contains "build") {
    $Owned = Enter-AgentLock "kit dress build $Leaf"
    try {
        $Log = Join-Path $ProjectDir "Saved\Logs\KitDressBuild.log"
        Write-Host "kit dress: building $Target from $($env:KIT_DRESS_LAYOUT) (builder $($env:KIT_DRESS_BUILDER)) ..."
        $null = Invoke-Hidden "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "`"$Project`" -run=pythonscript `"-script=$Script`" -unattended -nullrhi -nosplash -nosound -NoTelemetry `"-abslog=$Log`"" "build"
    } finally {
        if ($Owned) { Exit-AgentLock }
    }
    $Rep = Join-Path $ProjectDir "Saved\Logs\KitDressLayout.txt"
    Get-Content $Rep | Where-Object { $_ -match '^(ERROR|placed|saved|B_|kit_dress)' } | ForEach-Object { Write-Host "  $_" }
    if (-not (Select-String -Path $Rep -Pattern "^saved .*: True" -Quiet)) { Write-Host "kit dress: the map was not saved ($Rep)" -ForegroundColor Red; exit 4 }
}
if ($StepList -contains "bake") {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "bake_navmesh.ps1") -Map $Target
    if ($LASTEXITCODE -ne 0) { Write-Host "kit dress: navmesh bake failed" -ForegroundColor Red; exit 5 }
}
if ($StepList -contains "smoke") {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "smoke.ps1") -Command "CodexTactics.LevelGenBuildingsSmoke" -Map $Target `
        -Extra "-LevelGenBuildingsSmokeMaxFail=10" -Log "KitDressSmoke-$Leaf.log" -TimeoutSeconds 900
    if ($LASTEXITCODE -ne 0) { $Failed = $true; Write-Host "kit dress: LevelGenBuildingsSmoke failed" -ForegroundColor Red }
}
if ($StepList -contains "shots" -and -not $Grey) {
    # `py <file>` from -ExecCmds cannot take a path with spaces
    $Tmp = "C:\Temp\kit_dress_shots"
    New-Item -ItemType Directory -Force $Tmp | Out-Null
    Copy-Item $Script (Join-Path $Tmp "kit_dress.py") -Force
    $env:KIT_DRESS_MODE = "shots"
    $Owned = Enter-AgentLock "kit dress shots $Leaf"
    try {
        $Log = Join-Path $ProjectDir "Saved\Logs\KitDressShots.log"
        $null = Invoke-Hidden "$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe" "`"$Project`" $Target -game -RenderOffscreen -ResX=1920 -ResY=1080 -nosound -unattended -NoTelemetry `"-ExecCmds=py $Tmp\kit_dress.py`" `"-abslog=$Log`"" "shots"
    } finally {
        if ($Owned) { Exit-AgentLock }
        Remove-Item Env:KIT_DRESS_MODE -ErrorAction SilentlyContinue
    }
    $Rep = Join-Path $ProjectDir "Saved\KitBlocks\kit_dress_shots.txt"
    if (Test-Path $Rep) { Get-Content $Rep | Where-Object { $_ -match 'proxy check|in game|copied|RESULT|EXCEPTION' } | ForEach-Object { Write-Host "  $_" } }
    if (-not (Test-Path $Rep) -or -not (Select-String -Path $Rep -Pattern "RESULT: PASS" -Quiet)) { $Failed = $true; Write-Host "kit dress: shots failed ($Rep)" -ForegroundColor Red }
}
if ($Failed) { exit 1 }
exit 0
