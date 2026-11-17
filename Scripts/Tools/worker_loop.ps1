# Continuous job loop of the second machine codex-worker (docs/port/SECOND_MACHINE.md, section "Continuous loop").
# Runs ON the worker as the scheduled task "CodexTactics Worker Loop" (S4U, at startup + a daily watchdog trigger). Day cycle:
#   1. smoke batch (-SmokeCommands, default the whole registry) once per day after -DailyAt, results in Saved\Worker\smokes\;
#   2. optional bot batch (-BotArgs, passed to Scripts\bot_run.ps1; empty = none);
#   3. trainer window: Scripts\Trainer\run_night_trainer.ps1 -RunId <RunId> until the next -DailyAt (the run resumes; at the
#      end of the window the trainer makes the highlight clips + report.html), then back to 1.
# Code updates come only from worker_sync.ps1 on the main PC. Handshake: worker_sync writes Saved\Worker\SYNC and
# Saved\Trainer\PAUSE (the trainer yields between arena waves without stopping) and takes Saved\agent.lock while it copies;
# this loop does not start a phase while SYNC exists (a SYNC older than -SyncStaleHours is a dead sync and is removed).
# Control (files on the worker, under C:\Work\CodexTactics):
#   Saved\Worker\STOP                -> the loop ends after the current phase (to cut a trainer window short also create
#                                       Saved\Trainer\<RunId>\STOP; the loop removes both when it exits)
#   Saved\Trainer\<RunId>\STOP alone -> the trainer ends its window cleanly (clips) and the loop restarts it at once: the way
#                                       to apply a synced trainer_config / pmc_commander.json (python reads them at start)
#   Saved\Worker\loop.status.json    -> current phase; Saved\Worker\loop.log -> the loop's log
# Install / reinstall the task (on the worker, as the worker user):
#   powershell -ExecutionPolicy Bypass -File Scripts\Tools\worker_loop.ps1 -Install [-RunId pmc-v5] [-Start]
param(
    [string]$RunId = "pmc-v5",
    [int]$Parallel = 2,                       # arena games side by side (i7-6700K: 2)
    [string]$DailyAt = "08:00",               # end of the trainer window = morning report time; smokes follow it
    [string]$SmokeCommands = "all",
    [int]$SmokeJobs = 2,
    [string]$SmokeExtraArgs = "-Multiprocess",
    [string]$BotArgs = "",                    # e.g. "-Runs 4 -Parallel 2 -Profile NORMAL"; empty = no bot batch
    [double]$SyncStaleHours = 3,
    [switch]$Install,
    [switch]$Start
)
$ErrorActionPreference = "Stop"
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$WorkDir = Join-Path $Repo "Saved\Worker"
$TrainerDir = Join-Path $Repo "Saved\Trainer"
$TaskName = "CodexTactics Worker Loop"
New-Item -ItemType Directory -Force $WorkDir, $TrainerDir | Out-Null
$Log = Join-Path $WorkDir "loop.log"
$StateFile = Join-Path $WorkDir "loop.state.json"
$StatusFile = Join-Path $WorkDir "loop.status.json"
$SyncFile = Join-Path $WorkDir "SYNC"
$StopFile = Join-Path $WorkDir "STOP"
$PauseFile = Join-Path $TrainerDir "PAUSE"
$PauseMarker = "worker_sync"                  # worker_sync.ps1 writes this text into the PAUSE it creates

function Say([string]$m) { Add-Content -Encoding UTF8 $Log "[$((Get-Date).ToString('s'))] $m" }

function Set-Status([string]$Phase, [string]$Note = "") {
    $s = [ordered]@{ phase = $Phase; note = $Note; since = (Get-Date).ToString("s"); run_id = $RunId; pid = $PID }
    try { $s | ConvertTo-Json | Set-Content -Encoding UTF8 $StatusFile } catch { }
}

function Get-State {
    try { return (Get-Content $StateFile -Raw | ConvertFrom-Json) } catch { return [pscustomobject]@{ last_smokes = $null } }
}

function Save-State($State) { $State | ConvertTo-Json | Set-Content -Encoding UTF8 $StateFile }

# Most recent -DailyAt boundary at or before now.
function Get-LastBoundary {
    $t = [datetime]::ParseExact($DailyAt, "HH:mm", $null)
    $b = (Get-Date).Date.AddHours($t.Hour).AddMinutes($t.Minute)
    if ($b -gt (Get-Date)) { $b = $b.AddDays(-1) }
    return $b
}

function Get-TrainerPids {
    return @(Get-CimInstance Win32_Process -Filter "Name like 'python%'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine -like "*night_trainer.py*" -and $_.CommandLine -notlike "*--mock*" } |
        ForEach-Object { $_.ProcessId })
}

# Blocks while a worker_sync from the main PC is updating code / binaries (or running its own smokes).
function Wait-Sync {
    $logged = $false
    while (Test-Path $SyncFile) {
        $age = ((Get-Date) - (Get-Item $SyncFile).LastWriteTime).TotalHours
        if ($age -gt $SyncStaleHours) {
            Say ("SYNC is {0:N1} h old - dead sync, removing it (+ its trainer PAUSE)" -f $age)
            Remove-Item $SyncFile -ErrorAction SilentlyContinue
            if ((Test-Path $PauseFile) -and ((Get-Content $PauseFile -Raw) -match $PauseMarker)) { Remove-Item $PauseFile -ErrorAction SilentlyContinue }
            break
        }
        if (-not $logged) { Say "waiting: worker_sync in progress ($((Get-Content $SyncFile -Raw).Trim()))"; $logged = $true }
        Set-Status "waiting-sync" ((Get-Content $SyncFile -Raw).Trim())
        Start-Sleep -Seconds 30
    }
    if ($logged) { Say "sync done" }
}

# A trainer started outside the loop (old night task, by hand) owns the machine until it ends.
function Wait-TrainerIdle {
    $logged = $false
    while ((Get-TrainerPids).Count -gt 0) {
        if (-not $logged) { Say "waiting: night_trainer.py already running outside the loop (pid $((Get-TrainerPids) -join ', '))"; $logged = $true }
        Set-Status "waiting-trainer" "foreign night_trainer.py running"
        Start-Sleep -Seconds 60
    }
}

# Runs "& '<Script>' <ArgLine>" in a child PowerShell (EncodedCommand: values like '-Multiprocess' stay values), appends its
# output to OutLog, returns its exit code.
function Invoke-Script([string]$Script, [string]$ArgLine, [string]$OutLog) {
    $ErrorActionPreference = "Continue" # child stderr must not become terminating errors (PS 5.1)
    $b64 = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes("& '$Script' $ArgLine; exit `$LASTEXITCODE"))
    & powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand $b64 *>&1 | ForEach-Object { Add-Content -Encoding UTF8 $OutLog "$_" }
    return $LASTEXITCODE
}

function Invoke-Smokes {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $dir = Join-Path $WorkDir "smokes"
    New-Item -ItemType Directory -Force $dir | Out-Null
    $json = Join-Path $dir "$stamp.json"
    $out = Join-Path $dir "$stamp.log"
    Set-Status "smokes" "$SmokeCommands (jobs $SmokeJobs) -> $json"
    Say "smokes start: $SmokeCommands, jobs $SmokeJobs, HEAD $((& git -C $Repo rev-parse --short HEAD))"
    $env:CODEX_AGENT = "worker"; $env:CODEX_IGNORE_DESIGNER = "1"
    $code = Invoke-Script (Join-Path $Repo "Scripts\smoke_parallel.ps1") `
        "-Commands '$SmokeCommands' -Jobs $SmokeJobs -ExtraArgs '$SmokeExtraArgs' -ResultJson '$json'" $out
    $summary = "no result JSON"
    if (Test-Path $json) {
        $r = @((Get-Content $json -Raw | ConvertFrom-Json).results)
        $fails = @($r | Where-Object { $_.State -ne "PASS" } | ForEach-Object { "$($_.Name)=$($_.State)" })
        $summary = "{0} smokes, {1} not PASS{2}" -f $r.Count, $fails.Count, $(if ($fails) { ": " + ($fails -join ", ") } else { "" })
    }
    Say "smokes end (exit $code): $summary"
}

function Invoke-Bots {
    if (-not $BotArgs) { return }
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $out = Join-Path $WorkDir "bot-$stamp.log"
    Set-Status "bots" $BotArgs
    Say "bot batch start: $BotArgs"
    $env:CODEX_AGENT = "worker"; $env:CODEX_IGNORE_DESIGNER = "1"
    $code = Invoke-Script (Join-Path $Repo "Scripts\bot_run.ps1") $BotArgs $out
    Say "bot batch end (exit $code), log $out"
}

function Invoke-TrainerWindow {
    Set-Status "trainer" "$RunId until next $DailyAt"
    $t0 = Get-Date
    Say "trainer window start: $RunId until next $DailyAt (parallel $Parallel), HEAD $((& git -C $Repo rev-parse --short HEAD))"
    $env:CODEX_AGENT = "night-trainer"
    $code = Invoke-Script (Join-Path $Repo "Scripts\Trainer\run_night_trainer.ps1") `
        "-RunId '$RunId' -Parallel $Parallel -UntilTime '$DailyAt'" (Join-Path $WorkDir "trainer-window.log")
    $mins = ((Get-Date) - $t0).TotalMinutes
    Say ("trainer window end (exit {0}) after {1:N0} min" -f $code, $mins)
    # Saved\Trainer\<RunId>\STOP without Saved\Worker\STOP = restart the trainer (re-reads trainer_config / commander JSON).
    $runStop = Join-Path $TrainerDir "$RunId\STOP"
    if ((Test-Path $runStop) -and -not (Test-Path $StopFile)) {
        Remove-Item $runStop -ErrorAction SilentlyContinue
        Say "run STOP consumed - trainer restarts (new config / data from the last sync)"
        return
    }
    # A trainer that returns at once (run finished / STOP file left / crash on start) must not spin the loop.
    if ($mins -lt 5 -and -not (Test-Path $StopFile)) {
        Say "trainer returned within 5 min - check Saved\Trainer\$RunId\ (STOP file? max generations?); idle 30 min"
        Set-Status "idle" "trainer returned at once (exit $code)"
        Start-Sleep -Seconds 1800
    }
}

# ---- install ----------------------------------------------------------------------------------------------------------
if ($Install) {
    $argLine = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -RunId $RunId -Parallel $Parallel -DailyAt $DailyAt " +
        "-SmokeCommands $SmokeCommands -SmokeJobs $SmokeJobs"
    if ($BotArgs) { $argLine += " -BotArgs `"$BotArgs`"" }
    $action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument $argLine -WorkingDirectory $Repo
    $boot = New-ScheduledTaskTrigger -AtStartup
    $boot.Delay = "PT2M"
    $watch = New-ScheduledTaskTrigger -Daily -At ([datetime]::ParseExact($DailyAt, "HH:mm", $null).AddMinutes(10))
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable `
        -MultipleInstances IgnoreNew -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 5)
    $principal = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType S4U -RunLevel Highest
    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger @($boot, $watch) -Settings $settings -Principal $principal -Force | Out-Null
    # First morning after install: smokes are due at the next -DailyAt, not right now.
    if (-not (Test-Path $StateFile)) { Save-State ([pscustomobject]@{ last_smokes = (Get-Date).ToString("s") }) }
    Say "task '$TaskName' installed: $argLine"
    Write-Host "installed '$TaskName' ($argLine)"
    if ($Start) { Start-ScheduledTask -TaskName $TaskName; Write-Host "started" }
    exit 0
}

# ---- loop -------------------------------------------------------------------------------------------------------------
$mutex = New-Object System.Threading.Mutex($false, "Global\CodexTacticsWorkerLoop")
if (-not $mutex.WaitOne(0)) { Say "another worker_loop is running - exit"; exit 0 }
try {
    Say "loop start (pid $PID): run $RunId, daily at $DailyAt, smokes '$SmokeCommands', bots '$BotArgs'"
    while (-not (Test-Path $StopFile)) {
        Wait-Sync
        Wait-TrainerIdle
        Wait-Sync
        $state = Get-State
        $last = $(if ($state.last_smokes) { [datetime]$state.last_smokes } else { [datetime]::MinValue })
        if ($SmokeCommands -and $last -lt (Get-LastBoundary)) {
            Invoke-Smokes
            $state.last_smokes = (Get-Date).ToString("s"); Save-State $state
            if (Test-Path $StopFile) { break }
            Wait-Sync
            Invoke-Bots
            if (Test-Path $StopFile) { break }
            Wait-Sync
        }
        Invoke-TrainerWindow
    }
    Say "STOP file - loop ends"
    Remove-Item $StopFile, (Join-Path $TrainerDir "$RunId\STOP") -ErrorAction SilentlyContinue
    Set-Status "stopped" "STOP file"
} catch {
    Say "loop error: $_"
    Set-Status "error" "$_"
    exit 1
} finally {
    $mutex.ReleaseMutex()
}
exit 0
