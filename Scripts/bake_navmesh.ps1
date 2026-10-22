# Bakes a map's navmesh headlessly (commandlet UCodexBuildNavCommandlet, Source/CodexTacticsEditor) and saves only that map.
# Replaces "open the map in the editor and save it" after a scripted layout pass (docs/port/TRAPS.md, navmesh rows):
# ResavePackages -BuildNavigationData and RebuildNavigation cannot build in a commandlet (editor AsyncLoadLock 0x20).
# Usage: powershell -ExecutionPolicy Bypass -File Scripts\bake_navmesh.ps1 -Map L_Demo01_B_FrozenFactory [-NoSave] [-MinTiles 1]
#        -Map takes several names ("L_A,L_B") and /Game/... paths. Exit code != 0 when any map failed (nothing saved for it).
# Refuses while this checkout's editor is open (it would hold the map package). Takes the shared build lock.
param(
    [Parameter(Mandatory = $true)][string]$Map,
    [switch]$NoSave,
    [int]$MinTiles = 1,
    [int]$TimeoutSeconds = 1800
)

$ErrorActionPreference = "Stop"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$ProjectDir = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"

$ProjectFull = [IO.Path]::GetFullPath($Project)
if (Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor.exe'" | Where-Object {
        $_.CommandLine -and ($_.CommandLine.Replace('/', '') -like "*$ProjectFull*") -and ($_.CommandLine -notmatch '-run=|-game') }) {
    Write-Host "Unreal Editor (CodexTactics) is open on this checkout: close it first (it holds the map packages)." -ForegroundColor Yellow
    exit 2
}

. (Join-Path $PSScriptRoot "agent_lock.ps1")
$Owned = Enter-AgentLock "bake navmesh $Map"
$Failed = @()
try {
    foreach ($Name in ($Map -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
        $Leaf = ($Name -split '/')[-1]
        $LogFile = Join-Path $ProjectDir "Saved\Logs\NavBake-$Leaf.log"
        $Extra = $(if ($NoSave) { "-NoSave" } else { "" })
        $CmdArgs = "`"$Project`" -run=CodexTacticsEditor.CodexBuildNav -Map=$Name -MinTiles=$MinTiles $Extra -unattended -nullrhi -nosplash -nosound -NoTelemetry `"-abslog=$LogFile`""
        $Proc = Start-Process "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -ArgumentList $CmdArgs -WindowStyle Hidden -PassThru
        $null = $Proc.Handle # without a cached handle ExitCode stays $null after WaitForExit
        if (-not $Proc.WaitForExit($TimeoutSeconds * 1000)) {
            Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue
            Write-Host "$Name : TIMEOUT after $TimeoutSeconds s ($LogFile)" -ForegroundColor Red
            $Failed += $Name
            continue
        }
        $Lines = @(Select-String -Path $LogFile -Pattern '\[NavBake\]' -ErrorAction SilentlyContinue | ForEach-Object { $_.Line -replace '^.*\[NavBake\]\s*', '' })
        $Lines | ForEach-Object { Write-Host "$Name : $_" }
        if ($Proc.ExitCode -ne 0 -or -not ($Lines | Where-Object { $_ -like 'PASS*' })) {
            Write-Host "$Name : FAILED (exit $($Proc.ExitCode), $LogFile)" -ForegroundColor Red
            $Failed += $Name
        }
    }
} finally {
    if ($Owned) { Exit-AgentLock }
}
if ($Failed.Count) { exit 1 }
exit 0
