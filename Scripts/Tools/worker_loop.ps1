# Continuous job loop of the second machine codex-worker (docs/port/SECOND_MACHINE.md, section "Continuous loop").
# Runs ON the worker as the scheduled task "CodexTactics Worker Loop" (S4U, at startup + a daily watchdog trigger).
# Day plan (designer, 2026-11-17):
#   TEST FARM  -FarmFrom (08:00) .. -TrainerAt (16:30): the worker serves smoke batches sent from the main PC
#              (smoke_parallel.ps1 -Remote auto, build.ps1 -Remote worker -> worker_sync.ps1). The loop itself runs one daily
#              subset at 08:00 (Scripts\Tools\worker_daily_smokes.json, ~30-40 min; -SmokeCommands overrides), then waits.
#   TRAINER    -TrainerAt .. next -FarmFrom: waits for the evening freeze (worker_sync.ps1 -Frozen -> Saved\Worker\FROZEN,
#              at most -FreezeWaitMinutes after -TrainerAt), then ONE window run_night_trainer.ps1 -RunId <RunId>
#              -UntilTime <FarmFrom> (the run resumes; clips + report.html at the end of the window).
# Code updates come only from worker_sync.ps1 on the main PC: it writes Saved\Worker\SYNC.<token> + Saved\Trainer\PAUSE (the
# trainer yields between arena waves) and holds Saved\agent.lock; the loop starts no phase while a SYNC* exists (one older
# than -SyncStaleHours is a dead sync: removed, with its worker-sync lock and PAUSE).
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
    [string]$FarmFrom = "08:00",              # end of the trainer window = morning report time = start of the test farm
    [string]$TrainerAt = "16:30",             # end of the test farm, the lead posts the frozen commit (chat trainer-night)
    [int]$FreezeWaitMinutes = 90,             # no FROZEN by TrainerAt + this: train on the current code (logged)
    [string]$SmokeCommands = "",              # own batch at 08:00 ("" = the list in Scripts\Tools\worker_daily_smokes.json)
    [int]$SmokeJobs = 2,
    [string]$SmokeExtraArgs = "-Multiprocess",
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
$StopFile = Join-Path $WorkDir "STOP"
$FrozenFile = Join-Path $WorkDir "FROZEN"
$PauseFile = Join-Path $TrainerDir "PAUSE"
$LockFile = Join-Path $Repo "Saved\agent.lock"

function Say([string]$m) { Add-Content -Encoding UTF8 $Log "[$((Get-Date).ToString('s'))] $m" }

function Set-Status([string]$Phase, [string]$Note = "") {
    $s = [ordered]@{ phase = $Phase; note = $Note; since = (Get-Date).ToString("s"); run_id = $RunId; pid = $PID }
    try { $s | ConvertTo-Json | Set-Content -Encoding UTF8 $StatusFile } catch { }
}

function Get-State {
    try { return (Get-Content $StateFile -Raw | ConvertFrom-Json) } catch { return [pscustomobject]@{ last_smokes = $null } }
}

function At([datetime]$Day, [string]$HHmm) { return $Day.Date + [TimeSpan]::Parse($HHmm) }

function Get-TrainerPids {
    return @(Get-CimInstance Win32_Process -Filter "Name like 'python%'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine -like "*night_trainer.py*" -and $_.CommandLine -notlike "*--mock*" } |
        ForEach-Object { $_.ProcessId })
}

# Blocks while a worker_sync from the main PC is updating code / binaries or running its smokes.
function Wait-Sync {
    $logged = $false
    while ($true) {
        $syncs = @(Get-ChildItem (Join-Path $WorkDir "SYNC.*") -ErrorAction SilentlyContinue)
        if (-not $syncs) { break }
        foreach ($s in $syncs) {
            if (((Get-Date) - $s.LastWriteTime).TotalHours -gt $SyncStaleHours) {
                Say "dead sync $($s.Name) ($([Math]::Round(((Get-Date) - $s.LastWriteTime).TotalHours, 1)) h) - removing it, its lock and the trainer PAUSE"
                $token = $s.Name -replace '^SYNC\.?', ''
                if ((Test-Path $LockFile) -and ((Get-Content $LockFile -Raw) -match 'worker-sync') -and (-not $token -or (Get-Content $LockFile -Raw) -match [regex]::Escape($token))) { Remove-Item $LockFile -ErrorAction SilentlyContinue }
                Remove-Item $s.FullName -ErrorAction SilentlyContinue
            }
        }
        if (-not (Get-ChildItem (Join-Path $WorkDir "SYNC.*") -ErrorAction SilentlyContinue)) {
            if ((Test-Path $PauseFile) -and ((Get-Content $PauseFile -Raw) -match "worker_sync")) { Remove-Item $PauseFile -ErrorAction SilentlyContinue }
            break
        }
        if (-not $logged) { Say "waiting: worker_sync in progress ($(($syncs | ForEach-Object Name) -join ', '))"; $logged = $true }
        Set-Status "waiting-sync" (($syncs | ForEach-Object Name) -join ", ")
        Start-Sleep -Seconds 30
    }
    if ($logged) { Say "sync done" }
}

# A trainer started outside the loop (by hand) owns the machine until it ends.
function Wait-TrainerIdle {
    $logged = $false
    while ((Get-TrainerPids).Count -gt 0) {
        if (-not $logged) { Say "waiting: night_trainer.py already running outside the loop (pid $((Get-TrainerPids) -join ', '))"; $logged = $true }
        Set-Status "waiting-trainer" "foreign night_trainer.py running"
        Start-Sleep -Seconds 60
    }
}

# Sleeps until $Until (STOP ends it early); a SYNC during the wait is fine (that is the farm working).
function Wait-Until([datetime]$Until, [string]$Phase, [string]$Note) {
    Set-Status $Phase $Note
    while ((Get-Date) -lt $Until -and -not (Test-Path $StopFile)) { Start-Sleep -Seconds 60 }
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
    Say "own smoke batch start: $SmokeCommands, jobs $SmokeJobs, HEAD $((& git -C $Repo rev-parse --short HEAD))"
    $env:CODEX_AGENT = "worker"; $env:CODEX_IGNORE_DESIGNER = "1"; $env:CODEX_SMOKE_REMOTE = "off"
    $code = Invoke-Script (Join-Path $Repo "Scripts\smoke_parallel.ps1") `
        "-Commands '$SmokeCommands' -Jobs $SmokeJobs -ExtraArgs '$SmokeExtraArgs' -ResultJson '$json'" $out
    $summary = "no result JSON"
    if (Test-Path $json) {
        $r = @((Get-Content $json -Raw | ConvertFrom-Json).results)
        $fails = @($r | Where-Object { $_.State -ne "PASS" } | ForEach-Object { "$($_.Name)=$($_.State)" })
        $summary = "{0} smokes, {1} not PASS{2}" -f $r.Count, $fails.Count, $(if ($fails) { ": " + ($fails -join ", ") } else { "" })
    }
    Say "own smoke batch end (exit $code): $summary"
}

function Invoke-TrainerWindow {
    Set-Status "trainer" "$RunId until next $FarmFrom"
    $t0 = Get-Date
    Say "trainer window start: $RunId until next $FarmFrom (parallel $Parallel), HEAD $((& git -C $Repo rev-parse --short HEAD))"
    $env:CODEX_AGENT = "night-trainer"
    $code = Invoke-Script (Join-Path $Repo "Scripts\Trainer\run_night_trainer.ps1") `
        "-RunId '$RunId' -Parallel $Parallel -UntilTime '$FarmFrom'" (Join-Path $WorkDir "trainer-window.log")
    $mins = ((Get-Date) - $t0).TotalMinutes
    Say ("trainer window end (exit {0}) after {1:N0} min" -f $code, $mins)
    # Saved\Trainer\<RunId>\STOP without Saved\Worker\STOP = restart the trainer (re-reads trainer_config / commander JSON).
    $runStop = Join-Path $TrainerDir "$RunId\STOP"
    if ((Test-Path $runStop) -and -not (Test-Path $StopFile)) {
        Remove-Item $runStop -ErrorAction SilentlyContinue
        Say "run STOP consumed - trainer restarts (new config / data from the last sync)"
        return
    }
    # A trainer that returns at once inside its window (run finished / crash on start) must not spin the loop.
    $farm = At (Get-Date) $FarmFrom
    if ($mins -lt 5 -and -not (Test-Path $StopFile) -and -not ((Get-Date) -ge $farm -and (Get-Date) -lt (At (Get-Date) $TrainerAt))) {
        Say "trainer returned within 5 min - check Saved\Trainer\$RunId\ (STOP file? max generations?); idle 30 min"
        Set-Status "idle" "trainer returned at once (exit $code)"
        Start-Sleep -Seconds 1800
    }
}

# ---- install ----------------------------------------------------------------------------------------------------------
if ($Install) {
    $argLine = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -RunId $RunId -Parallel $Parallel -FarmFrom $FarmFrom " +
        "-TrainerAt $TrainerAt -FreezeWaitMinutes $FreezeWaitMinutes"
    if ($SmokeCommands) { $argLine += " -SmokeCommands $SmokeCommands -SmokeJobs $SmokeJobs" }
    $action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument $argLine -WorkingDirectory $Repo
    $boot = New-ScheduledTaskTrigger -AtStartup
    $boot.Delay = "PT2M"
    $watch = New-ScheduledTaskTrigger -Daily -At ((Get-Date).Date + [TimeSpan]::Parse($FarmFrom) + [TimeSpan]::FromMinutes(10))
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable `
        -MultipleInstances IgnoreNew -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 5)
    # $env:USERDOMAIN is empty in an SSH session: take the account from the process token.
    $principal = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType S4U -RunLevel Highest
    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger @($boot, $watch) -Settings $settings -Principal $principal -Force | Out-Null
    Say "task '$TaskName' installed: $argLine"
    Write-Host "installed '$TaskName' ($argLine)"
    if ($Start) { Start-ScheduledTask -TaskName $TaskName; Write-Host "started" }
    exit 0
}

# ---- loop -------------------------------------------------------------------------------------------------------------
$mutex = New-Object System.Threading.Mutex($false, "Global\CodexTacticsWorkerLoop")
if (-not $mutex.WaitOne(0)) { Say "another worker_loop is running - exit"; exit 0 }
try {
    Say "loop start (pid $PID): run $RunId, farm $FarmFrom-$TrainerAt, trainer $TrainerAt-$FarmFrom, own smokes '$SmokeCommands'"
    while (-not (Test-Path $StopFile)) {
        Wait-Sync
        Wait-TrainerIdle
        $now = Get-Date
        $farmStart = At $now $FarmFrom
        $farmEnd = At $now $TrainerAt
        if ($now -ge $farmStart -and $now -lt $farmEnd) {
            # ---- test farm ----
            $state = Get-State
            $last = $(if ($state.last_smokes) { [datetime]$state.last_smokes } else { [datetime]::MinValue })
            $daily = $SmokeCommands
            $cfg = Join-Path $Repo "Scripts\Tools\worker_daily_smokes.json"
            if (-not $daily -and (Test-Path $cfg)) {
                try { $c = Get-Content $cfg -Raw | ConvertFrom-Json; $daily = @($c.smokes) -join ","; if ($c.jobs) { $script:SmokeJobs = [int]$c.jobs } } catch { Say "bad $cfg : $_" }
            }
            if ($daily -and $last -lt $farmStart) {
                $script:SmokeCommands = $daily
                Invoke-Smokes
                $script:SmokeCommands = $(if ($PSBoundParameters.ContainsKey("SmokeCommands")) { $SmokeCommands } else { "" })
                $state | Add-Member -Force NoteProperty last_smokes (Get-Date).ToString("s")
                $state | ConvertTo-Json | Set-Content -Encoding UTF8 $StateFile
                continue
            }
            Say "test farm until $($farmEnd.ToString('HH:mm')) - serving worker_sync / smoke_parallel -Remote from the main PC"
            Wait-Until $farmEnd "farm" "serving remote smoke batches until $($farmEnd.ToString('HH:mm'))"
            continue
        }
        # ---- trainer time: wait for this evening's freeze, then one window until FarmFrom ----
        $evening = $(if ($now -ge $farmEnd) { $farmEnd } else { $farmEnd.AddDays(-1) })   # after midnight: yesterday's 16:30
        $frozenOk = (Test-Path $FrozenFile) -and ((Get-Item $FrozenFile).LastWriteTime -ge $evening.AddMinutes(-60))
        $waitEnd = $evening.AddMinutes($FreezeWaitMinutes)
        if (-not $frozenOk -and $now -lt $waitEnd) {
            Set-Status "waiting-freeze" "worker_sync -Frozen expected (chat trainer-night), at most until $($waitEnd.ToString('HH:mm'))"
            Say "waiting for the evening freeze (worker_sync -Frozen) until $($waitEnd.ToString('HH:mm'))"
            while (-not (Test-Path $StopFile) -and (Get-Date) -lt $waitEnd) {
                if ((Test-Path $FrozenFile) -and ((Get-Item $FrozenFile).LastWriteTime -ge $evening.AddMinutes(-60))) { break }
                Start-Sleep -Seconds 30
            }
            Wait-Sync
            if ((Test-Path $FrozenFile) -and ((Get-Item $FrozenFile).LastWriteTime -ge $evening.AddMinutes(-60))) {
                Say "freeze: $((Get-Content $FrozenFile -Raw).Trim())"
            } elseif (-not (Test-Path $StopFile)) {
                Say "NO FREEZE by $($waitEnd.ToString('HH:mm')) - training on the current code, HEAD $((& git -C $Repo rev-parse --short HEAD))"
            }
            continue
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
