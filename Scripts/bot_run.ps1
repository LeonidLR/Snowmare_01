# Runs the playtest bot (UPlaytestBotSubsystem) N times without rendering, fixed-step and as fast as the CPU allows,
# up to -Parallel games at once (Sprint 05-A). Every run writes its own log (Saved\Logs\Bot-<Profile>-<Run>.log) and
# its own run record; this script alone appends the records to Saved\Telemetry\raw_runs\runs.jsonl (no two writers) and
# keeps Saved\Telemetry\bot_status.json current for the Wave Editor's live progress (/api/bot-status).
# Usage: powershell -ExecutionPolicy Bypass -File Scripts\bot_run.ps1 [-Runs 1] [-Profile NORMAL] [-Loadout COLLECT]
#        [-Parallel 4] [-TimeoutSeconds 600] [-Extra "<more game arguments>"]
# -Profile CASUAL | NORMAL | VETERAN; -Loadout COLLECT (explore first) | UNIQUE | PRESET (straight to the fight).
# Every run gets -BotSeed=<run number> (the stealth bot's seeded caution / patience / approach side vary per run, the
# same run number repeats exactly). On an ambush / patrol map (e.g. -Map /Game/Maps/L_PatrolTest) the bot sneaks
# instead of pressing «Начать бой» (PlaytestBotSubsystem, [Stealth] log lines). A batch holds the shared agent lock.
# The Godot archive's speed is not needed: the runs are fixed-step (-benchmark -FPS=60), not time-scaled.
param(
    [int]$Runs = 1,
    [string]$Profile = "NORMAL",
    [string]$Loadout = "COLLECT",
    [int]$Parallel = 4,
    [int]$TimeoutSeconds = 600,
    [string]$Map = "/Game/Maps/L_MovementTest",
    # Extra game command-line arguments (e.g. -dpcvars=gc.TimeBetweenPurgingPendingKillObjects=1 for GC stress runs).
    [string]$Extra = "",
    [switch]$EarlyStop = $true
)

$ErrorActionPreference = "Stop"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$ProjectDir = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"
$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$TelemetryDir = Join-Path $ProjectDir "Saved\Telemetry"
$RunsFile = Join-Path $TelemetryDir "raw_runs\runs.jsonl"
$PartsDir = Join-Path $TelemetryDir "raw_runs\parts"
$StatusFile = Join-Path $TelemetryDir "bot_status.json"
$Utf8 = New-Object System.Text.UTF8Encoding($false)
$Profile = $Profile.ToUpper()
$Parallel = [Math]::Max(1, [Math]::Min($Parallel, $Runs))
New-Item -ItemType Directory -Force -Path $PartsDir | Out-Null

$Results = @{ VICTORY = 0; DEFEAT = 0; ABORTED = 0; ERROR = 0 }
$Started = Get-Date
$Finished = 0
$Active = New-Object System.Collections.ArrayList

function Write-Status([bool]$Running) {
    $Status = [ordered]@{
        isRunning = $Running; pid = $PID; profile = $Profile; totalRuns = $Runs; currentRun = $script:Finished
        activeRuns = $Active.Count; victories = $Results.VICTORY; defeats = $Results.DEFEAT
        aborted = $Results.ABORTED; errors = $Results.ERROR; parallel = $Parallel
        startedAtUtc = $Started.ToUniversalTime().ToString("o"); elapsedSec = [int]((Get-Date) - $Started).TotalSeconds
    }
    [IO.File]::WriteAllText($StatusFile, ($Status | ConvertTo-Json -Compress), $Utf8)
}

function Complete-Run($Job) {
    $Line = Select-String -Path $Job.Log -Pattern "\[Bot\] RESULT (\w+)" -ErrorAction SilentlyContinue | Select-Object -Last 1
    $Result = if ($Line) { $Line.Matches[0].Groups[1].Value } else { "ERROR" }
    $Results[$Result] = $Results[$Result] + 1
    $Waves = Select-String -Path $Job.Log -Pattern "\[Bot\] Wave (\d+) cleared" -ErrorAction SilentlyContinue | Select-Object -Last 1
    $Cleared = if ($Waves) { $Waves.Matches[0].Groups[1].Value } else { "0" }
    # The single writer of runs.jsonl: the run's own record goes in once its game has exited.
    if (Test-Path $Job.Part) {
        $Record = [IO.File]::ReadAllText($Job.Part, $Utf8)
        if ($Record.Trim()) { [IO.File]::AppendAllText($RunsFile, $Record, $Utf8) }
        Remove-Item $Job.Part -Force
    }
    $script:Finished++
    Write-Host ("    [{0} {1}/{2}] {3}, waves cleared {4}, {5:N0} s real (log {6})" -f $Profile, $Job.Run, $Runs, $Result, $Cleared,
        $Job.Watch.Elapsed.TotalSeconds, $Job.Log)
}

$Pending = New-Object System.Collections.Queue
1..$Runs | ForEach-Object { $Pending.Enqueue($_) }
Write-Host ("{0}: {1} runs, {2} at a time" -f $Profile, $Runs, $Parallel)

# The games load the module DLLs from Binaries\: a build while they run fails, so a batch holds the shared agent lock
# (Scripts\agent_lock.ps1; a long coach run takes it per batch, so other agents can build between batches).
. (Join-Path $PSScriptRoot "agent_lock.ps1")
$LockOwned = Enter-AgentLock "bot_run $Profile x$Runs" 240
try {
Write-Status $true

while ($Pending.Count -gt 0 -or $Active.Count -gt 0) {
    while ($Active.Count -lt $Parallel -and $Pending.Count -gt 0) {
        $Run = $Pending.Dequeue()
        $Log = Join-Path $ProjectDir "Saved\Logs\Bot-$Profile-$Run.log"
        $Part = Join-Path $PartsDir ("run_{0}_{1}_{2}.jsonl" -f $PID, $Profile, $Run)
        $GameArgs = "`"$Project`" $Map -game -nullrhi -nosound -nosplash -unattended -windowed -benchmark -FPS=60 -CodexBot " +
            "-BotProfile=$Profile -BotLoadout=$Loadout -BotTimeout=$TimeoutSeconds -BotSeed=$Run `"-abslog=$Log`" `"-TelemetryRunsFile=$Part`" $Extra"
        $Process = Start-Process -FilePath $Editor -ArgumentList $GameArgs -PassThru -WindowStyle Hidden
        [void]$Active.Add([pscustomobject]@{ Run = $Run; Process = $Process; Log = $Log; Part = $Part; Watch = [Diagnostics.Stopwatch]::StartNew() })
        Write-Host ("[{0} {1}/{2}] started" -f $Profile, $Run, $Runs)
        Write-Status $true
    }
    Start-Sleep -Milliseconds 500
    foreach ($Job in @($Active)) {
        $TimedOut = $Job.Watch.Elapsed.TotalSeconds -gt ($TimeoutSeconds + 120)
        if ($Job.Process.HasExited -or $TimedOut) {
            if (-not $Job.Process.HasExited) { $Job.Process.Kill() }
            [void]$Active.Remove($Job)
            Complete-Run $Job
            Write-Status ($Pending.Count -gt 0 -or $Active.Count -gt 0)

            # Jev-driven Early Stop check (Sprint 05-A / Fast Test pipeline)
            if ($EarlyStop -and $script:Finished -ge 3 -and ($script:Finished % 2 -eq 1)) {
                $TriagePy = Join-Path $PSScriptRoot "Tools\typesafe_triage.py"
                if (Test-Path $TriagePy) {
                    & python $TriagePy --early-stop --since $Started.ToUniversalTime().ToString("s") 2>$null
                    if ($LASTEXITCODE -eq 2) {
                        Write-Warning "[Jev EarlyStop] High systemic failure rate detected (>= 85%). Stopping remaining pending runs to conserve CPU time."
                        $Pending.Clear()
                    }
                }
            }
        }
    }
}
} finally {
    if ($LockOwned) { Exit-AgentLock }
}

Write-Status $false
Write-Host ""
Write-Host ("{0}: {1} runs - victories {2}, defeats {3}, timeouts {4}, errors {5} ({6:N0} s real, {7} at a time)" -f $Profile, $Runs,
    $Results.VICTORY, $Results.DEFEAT, $Results.ABORTED, $Results.ERROR, ((Get-Date) - $Started).TotalSeconds, $Parallel)
Write-Host ("Telemetry: {0}" -f $RunsFile)
