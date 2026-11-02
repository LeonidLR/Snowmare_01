# The route check of ANY generated map (docs/design/levelgen/LEVEL_GENERATOR.md section 8): CodexTactics.LevelGenRouteSmoke on the map
# given, headless (-nullrhi, fixed step): the bot's route mode walks the level JSON's "bot_route" with the pressure off (drops through the
# drop nav links), else the carrot walk. Scripts\smoke_parallel.ps1 -Commands LevelGenRouteSmoke runs it on L_Gen_Depot_1 (test_map.json).
#   powershell -ExecutionPolicy Bypass -File Scripts\levelgen_route_smoke.ps1 -Map /Game/Maps/Generated/L_Gen_Depot_1 [-Carrot] [-StatusFile <json>]
#     [-TimeoutSeconds 1500] [-LockWaitMinutes 60]
# Status (Scripts/job_status.ps1, codex.job_status/1; default Saved\LevelGen\route_smoke_<map>.json): running / waiting_lock / done / failed /
# refused, result { map, level_json, result "PASS" | "FAIL" | "", summary (the smoke's last line), checks [ok / FAIL / known lines], log }.
# Exit codes: 0 PASS, 1 FAIL, 3 the build lock is held / timeout, 2 bad arguments.
param(
    [Parameter(Mandatory = $true)][string]$Map,
    [switch]$Carrot,
    [string]$StatusFile = "",
    [int]$TimeoutSeconds = 1500,
    [int]$LockWaitMinutes = 60
)

$ErrorActionPreference = "Stop"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$ProjectDir = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"
$Leaf = ($Map -split '/')[-1]
if ($Leaf -notmatch '^[A-Za-z0-9_]{1,80}$') { Write-Host "-Map: a /Game/Maps/... path" -ForegroundColor Red; exit 2 }
$LevelJson = Join-Path $ProjectDir "Content\Data\LevelJson\$Leaf.json"
$Log = Join-Path $ProjectDir "Saved\Logs\LevelGenRouteSmoke-$Leaf.log"
if (-not $StatusFile) { $StatusFile = Join-Path $ProjectDir "Saved\LevelGen\route_smoke_$Leaf.json" }

. (Join-Path $PSScriptRoot "agent_lock.ps1")
. (Join-Path $PSScriptRoot "job_status.ps1")
$Job = New-JobStatus $StatusFile "route_smoke" $Log
Set-JobStatus $Job @{ map = $Map; level_json = $LevelJson; result = "" }
if (-not (Test-Path $LevelJson)) {
    Complete-JobStatus $Job "refused" 2 "no level JSON $LevelJson" "no_level_json"
    Write-Host "no level JSON $LevelJson" -ForegroundColor Red
    exit 2
}

$Owned = Wait-JobLock $Job "levelgen route smoke $Leaf" $LockWaitMinutes
try {
    $Cmd = "CodexTactics.LevelGenRouteSmoke map=$Leaf" + $(if ($Carrot) { " walk=carrot" } else { "" })
    $GameArgs = "`"$Project`" $Map -game -nullrhi -nosplash -nosound -unattended -windowed -FORCELOGFLUSH -NoTelemetry -Multiprocess -UseFixedTimeStep -FPS=60 -FixedSeed " +
        "`"-ExecCmds=$Cmd`" `"-abslog=$Log`" -LevelJson=$Leaf.json"
    Write-Host "route smoke: $Map ($Cmd)"
    $Proc = Start-Process "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -ArgumentList $GameArgs -WindowStyle Hidden -PassThru
    $null = $Proc.Handle
    if (-not $Proc.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue
        Complete-JobStatus $Job "failed" 3 "the smoke timed out after $TimeoutSeconds s" "timeout"
        exit 3
    }
} finally {
    if ($Owned) { Exit-AgentLock }
}

$Lines = if (Test-Path $Log) { @(Select-String -Path $Log -Pattern 'Smoke (ok  |FAIL|known): |Smoke: LevelGenRouteSmoke |Smoke RESULT: ' | ForEach-Object { $_.Line -replace '^.*?Display: ', '' }) } else { @() }
$ResultLine = $Lines | Where-Object { $_ -match 'Smoke RESULT: (\w+)' } | Select-Object -Last 1
$Result = if ($ResultLine -match 'Smoke RESULT: (\w+)') { $Matches[1] } else { "" }
$Summary = $Lines | Where-Object { $_ -match '^Smoke: LevelGenRouteSmoke ' } | Select-Object -Last 1
Set-JobStatus $Job @{ result = $Result; summary = "$Summary"; checks = @($Lines | Where-Object { $_ -match '^Smoke (ok  |FAIL|known): ' }) }
$Lines | ForEach-Object { Write-Host $_ }
if ($Result -eq "PASS") {
    Complete-JobStatus $Job "done" 0 "PASS: $Summary"
    exit 0
}
Complete-JobStatus $Job "failed" 1 ("FAIL: {0}" -f $(if ($Summary) { $Summary } else { "no result in the log" })) "smoke_failed"
exit 1
