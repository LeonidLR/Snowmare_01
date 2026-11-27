# codex-worker continuous night loop (designer 2026-11-19: "the process must work constantly, no jams tonight").
# Replaces the single 16:35 sweep run that ended at 16:51 and left the worker idle all night. Runs ON the worker, started and
# kept alive by Scripts\Tools\worker_night_guard.ps1 (task "CodexTactics Night Guard", every 10 min). Until -Until it cycles:
#   cycle k: A-00A crossroads x30 seeds -> A-00B alley x30 seeds -> X4 tactical arena x10 seeds -> PmcRoofDefence x10 ->
#            (every 3rd cycle) the PMC / tactical / levelgen scenario sweep. New seed ranges every cycle (more statistics).
# Each job: waits for day callers (Saved\Worker\SYNC.* / Saved\agent.lock, max 30 min, then the next job), takes the lock
# (agent night-loop, CODEX_LOCK_HELD=1 so build.ps1 does not wait on it), updates main to origin/main and builds when the
# HEAD moved, runs the job, RELEASES the lock (day callers get the worker between jobs). Every job start / end touches
# Saved\Worker\nightloop\heartbeat.txt (the guard restarts a loop whose heartbeat is older than 50 min).
# Results: Saved\Worker\nightloop\<date>\<job>-c<k>\ (worker_seed_batch result folders) + one line per job in
# Saved\Worker\nightloop\<date>\digest.log (dayloop_digest.py) - the morning report reads that file.
# Stop for a night: create Saved\Worker\STOP_NIGHTLOOP (the guard then neither starts nor restarts it).
#   05:30-07:30 (-TrainerFrom .. -TrainerUntil): the 2 h window at the end of the night (designer 11-19). -WindowMode:
#   ab (default since designer 2026-11-21 "Давай 2, потом 3": the window runs the paired A/B queue, the trainer stays disabled
#   until its evaluation is fixed after T-08) - every Saved\Worker\ab_queue\*.ps1 (untracked, survives the checkout reset; name
#   order) runs as its own process with -StopAt <window end> and is then moved to ab_queue\done\ (.log beside it); the item
#   uses its own checkout (CodexTactics-slot1), never this one; a still-running item is killed (process tree) 5 min after the
#   window end, so the night ends by -Until. Empty queue = normal jobs. trainer = the weight trainer (night_trainer.py, run
#   pmc-v6-qd2) as before.
param([string]$Repo = 'C:\Work\CodexTactics', [string]$Until = '07:45', [int]$Jobs = 4, [switch]$Once,
      [string]$TrainerFrom = '05:30', [string]$TrainerUntil = '07:30', [string]$TrainerRunId = 'pmc-v6-qd2',
      [ValidateSet('ab', 'trainer')][string]$WindowMode = 'ab',
      [string]$Py = 'C:\Users\User\AppData\Local\Programs\Python\Python312\python.exe',
      [int]$HealthReserveMinutes = 45,
      [switch]$SkipHealthCheck, [switch]$HealthCheckOnly, [switch]$DryRun,
      [switch]$QueueOnly) # one scheduled A/B queue window, then release the worker to the normal guard
$ErrorActionPreference = 'Continue'
Set-Location $Repo
$root = Join-Path $Repo 'Saved\Worker\nightloop'; New-Item -ItemType Directory -Force $root | Out-Null
$heart = Join-Path $root 'heartbeat.txt'
$lock = Join-Path $Repo 'Saved\agent.lock'
function Beat([string]$m) { "$(Get-Date -Format s) pid $PID $m" | Set-Content -Encoding UTF8 $heart }
function L([string]$m) {
    $d = Join-Path $root (Get-Date -Format 'yyyy-MM-dd'); New-Item -ItemType Directory -Force $d | Out-Null
    Add-Content -Encoding UTF8 (Join-Path $d 'loop.log') ('{0} {1}' -f (Get-Date -Format s), $m)
}
function EndTime {
    $t = [datetime]::ParseExact($Until, 'HH:mm', $null)
    $end = (Get-Date).Date.Add($t.TimeOfDay)
    if ($end -lt (Get-Date).AddMinutes(-1)) { $end = $end.AddDays(1) }   # started in the evening -> tomorrow morning
    return $end
}
$deadline = EndTime
function TakeLock {
    $until = (Get-Date).AddMinutes(30)
    while (((Get-ChildItem (Join-Path $Repo 'Saved\Worker') -Filter 'SYNC.*' -ErrorAction SilentlyContinue) -or (Test-Path $lock)) -and (Get-Date) -lt $until) {
        Beat 'waiting for day callers'; Start-Sleep 30
    }
    try {
        $fs = [IO.File]::Open($lock, 'CreateNew', 'Write')
        $b = [Text.Encoding]::UTF8.GetBytes((@{ agent = 'night-loop'; purpose = 'night loop job'; pid = $PID; manual = $true; started = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss') } | ConvertTo-Json))
        $fs.Write($b, 0, $b.Length); $fs.Close(); return $true
    } catch { return $false }
}
function ReleaseLock { if ((Test-Path $lock) -and ((Get-Content $lock -Raw) -match 'night-loop')) { Remove-Item $lock -Force } }
$script:BuiltHead = ''
function Get-FreezeState {
    $f = Join-Path $Repo 'Saved\Worker\FROZEN'
    if (-not (Test-Path $f)) { return @{ Active = $false; Sha = ''; Raw = '' } }
    $now = Get-Date
    $fromT = [datetime]::ParseExact('16:30', 'HH:mm', $null).TimeOfDay
    $start = if ($now.TimeOfDay -ge $fromT) { $now.Date.Add($fromT) } else { $now.Date.AddDays(-1).Add($fromT) }
    $fresh = (Get-Item $f).LastWriteTime -ge $start.Date.AddHours(8.5)
    if (-not $fresh) { return @{ Active = $false; Sha = ''; Raw = '' } }
    $raw = (Get-Content $f -Raw -ErrorAction SilentlyContinue).Trim()
    $sha = ''
    if ($raw -match 'binaries\s+([0-9a-fA-F]{7,40})') { $sha = $Matches[1] }
    elseif ($raw -match '->\s*([0-9a-fA-F]{7,40})') { $sha = $Matches[1] }
    return @{ Active = $true; Sha = $sha; Raw = $raw }
}
function UpdateAndBuild {
    $freeze = Get-FreezeState
    if ($freeze.Active) {
        $head = (git rev-parse --short HEAD 2>$null).Trim()
        if ($freeze.Sha -and $head -and -not ($head.StartsWith($freeze.Sha) -or $freeze.Sha.StartsWith($head))) {
            L "FROZEN active ($($freeze.Sha)) but HEAD is $head - refusing checkout/rebuild"
            return $false
        }
        $script:BuiltHead = $head
        L "FROZEN active: keeping HEAD $head and frozen binaries (skipping origin/main checkout & build)"
        return $true
    }
    $dirty = @(git status --porcelain --untracked-files=no)
    # Leftovers of a day caller's shipped patch (worker_sync -Patch): nobody edits C:\Work\CodexTactics by hand (the stand-in lead
    # works in CodexTactics-slot1), so the night resets them instead of idling; logged.
    if ($dirty.Count) { L "checkout had $($dirty.Count) tracked changes (a shipped patch) - reset to origin/main"; $script:BuiltHead = '' }
    git fetch --quiet origin 2>&1 | Out-Null
    git checkout --quiet -f -B main origin/main 2>&1 | Out-Null
    git lfs pull 2>&1 | Out-Null
    $head = (git rev-parse --short HEAD).Trim()
    if ($head -eq $script:BuiltHead) { return $true }
    $b = (& powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\build.ps1 2>&1 | Out-String)
    $ok = $LASTEXITCODE -eq 0
    L ("build $head exit $LASTEXITCODE " + (($b -split "`n" | Where-Object { $_ -match 'Result:' } | Select-Object -Last 1)))
    if ($ok) { $script:BuiltHead = $head }
    return $ok
}
function Digest([string]$dir, [string]$label) {
    $d = Join-Path $root (Get-Date -Format 'yyyy-MM-dd')
    $line = if (Test-Path $Py) { (& $Py -I Scripts\Tools\dayloop_digest.py $dir 2>&1 | Select-Object -First 3) -join ' | ' } else { "no python at $Py" }
    Add-Content -Encoding UTF8 (Join-Path $d 'digest.log') ("{0} {1} HEAD {2}: {3}" -f (Get-Date -Format s), $label, $script:BuiltHead, $line)
}
$seedBatch = Join-Path $Repo 'Scripts\Tools\worker_seed_batch.ps1'
$k = 0
Beat 'start'; L "night loop start pid $PID until $($deadline.ToString('s'))"
function TrainerAt([string]$hhmm) {
    $t = [datetime]::ParseExact($hhmm, 'HH:mm', $null)
    $at = $deadline.Date.Add($t.TimeOfDay)
    if ($at -gt $deadline) { $at = $at.AddDays(-1) }
    return $at
}
$script:TrainerDone = $false
function RunAbWindow {
    # Designer 2026-11-21: the window runs the paired A/B queue (enemy AI session), not the trainer.
    $qdir = Join-Path $Repo 'Saved\Worker\ab_queue'; $done = Join-Path $qdir 'done'
    New-Item -ItemType Directory -Force $done | Out-Null
    if (-not @(Get-ChildItem $qdir -Filter '*.ps1' -File -ErrorAction SilentlyContinue).Count) {
        L 'A/B window: queue empty - normal jobs'; $script:TrainerDone = $true; return
    }
    if (-not (TakeLock)) { L 'A/B window: worker busy 30 min - trying again after the next job'; return }
    try {
        $stopAt = TrainerAt $TrainerUntil
        while ((Get-Date) -lt $stopAt) {
            $item = Get-ChildItem $qdir -Filter '*.ps1' -File -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -First 1
            if (-not $item) { L 'A/B window: queue drained'; break }
            $tag = '{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmm'), $item.BaseName
            $run = Join-Path $done ($tag + '.ps1'); Move-Item $item.FullName $run -Force
            $ilog = Join-Path $done ($tag + '.log')
            L "A/B item start: $($item.Name) (stop at $($stopAt.ToString('s')))"
            $p = Start-Process powershell.exe -WindowStyle Hidden -PassThru -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass',
                '-Command', "& '$run' -StopAt '$($stopAt.ToString('s'))' *> '$ilog'; exit `$LASTEXITCODE")
            while (-not $p.HasExited) {
                if ((Get-Date) -ge $stopAt.AddMinutes(5)) { L "A/B item $($item.Name) past the window - killing its tree"; & taskkill.exe /T /F /PID $p.Id *> $null; break }
                Beat "A/B item $($item.Name) running until $($stopAt.ToString('s'))"; Start-Sleep 60
            }
            L "A/B item end: $($item.Name) exit $(if ($p.HasExited) { $p.ExitCode } else { 'killed' })"
        }
    } finally { ReleaseLock; $script:TrainerDone = $true; Beat 'after A/B window' }
}
function MaybeTrainer {
    # Designer 2026-11-19: 2 hours of the night for the trainer (the end of the night), the rest for tasks.
    if ($script:TrainerDone -or (Get-Date) -lt (TrainerAt $TrainerFrom) -or (Get-Date) -ge (TrainerAt $TrainerUntil)) { return }
    if ($WindowMode -eq 'ab') { RunAbWindow; return }
    if (-not (TakeLock)) { L 'trainer window: worker busy 30 min - trying again after the next job'; return }
    try {
        $env:CODEX_LOCK_HELD = '1'; $env:CODEX_AGENT = 'night-trainer'
        [void](UpdateAndBuild)
        $stopAt = (TrainerAt $TrainerUntil).ToString('yyyy-MM-ddTHH:mm')
        $cfg = Join-Path $Repo 'Saved\Worker\trainer_config_qd2_x4.json'
        $pargs = @('Scripts\Trainer\night_trainer.py', '--run-id', $TrainerRunId, '--stop-at', $stopAt, '--parallel', '2')
        if (Test-Path $cfg) { $pargs += @('--config', $cfg) }
        $tlog = Join-Path $root ((Get-Date -Format 'yyyy-MM-dd') + '\trainer.log')
        L "trainer start: $($pargs -join ' ')"
        $job = Start-Job -ScriptBlock { param($py, $a, $repo, $log) Set-Location $repo; & $py @a *>> $log } -ArgumentList $Py, $pargs, $Repo, $tlog
        while ($job.State -eq 'Running') { Beat "trainer $TrainerRunId running until $stopAt"; Start-Sleep 300 }
        L "trainer end: $($job.State)"; Remove-Job $job -Force
    } finally { ReleaseLock; $script:TrainerDone = $true; Beat 'after trainer' }
}
$script:HealthCheckDone = $false
function Get-RegisteredSmokeCount {
    $tm = Join-Path $Repo 'Scripts\test_map.json'
    if (-not (Test-Path $tm)) { return 0 }
    try { return @((Get-Content $tm -Raw -Encoding UTF8 | ConvertFrom-Json).smokeRegistry).Count } catch { return 0 }
}
function Get-OtherSlotActiveCount {
    $repoNorm = $Repo.Replace('/', '\').TrimEnd('\').ToLower()
    $others = @(Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" -ErrorAction SilentlyContinue | Where-Object {
        $cmd = "$($_.CommandLine)".Replace('/', '\').ToLower()
        $cmd -match 'codextactics' -and -not $cmd.Contains($repoNorm + '\')
    })
    return $others.Count
}
function Invoke-HealthChild([string]$Stage, [string]$PsCommand, [datetime]$StopAt) {
    $p = Start-Process powershell.exe -WindowStyle Hidden -PassThru -WorkingDirectory $Repo -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', $PsCommand
    )
    $started = Get-Date
    while (-not $p.HasExited) {
        if ((Get-Date) -ge $StopAt) {
            L "health ${Stage}: reached stop-at $($StopAt.ToString('s')) - killing process tree pid $($p.Id)"
            & taskkill.exe /T /F /PID $p.Id *> $null
            return @{ ExitCode = 124; TimedOut = $true }
        }
        $mins = [Math]::Round(((Get-Date) - $started).TotalMinutes, 1)
        Beat "nightly health check: $Stage (${mins}m)"
        Start-Sleep 30
    }
    return @{ ExitCode = $p.ExitCode; TimedOut = $false }
}
function RunNightlyHealthCheck {
    # 080/109 PROJECT HEALTH: Nightly FULL run on the worker, bounded by the 05:30 A/B window, freeze-aware and slot-aware.
    if ($SkipHealthCheck -or $script:HealthCheckDone) { return 'skipped' }
    $tFrom = TrainerAt $TrainerFrom; $tUntil = TrainerAt $TrainerUntil
    $now = Get-Date
    if ($now -ge $tFrom -and $now -lt $tUntil) { L 'health run: inside trainer/AB window - skipped'; return 'skipped-window' }
    $stopAt = if ($now -lt $tFrom) { $tFrom } else { $deadline }
    if (($stopAt - $now).TotalMinutes -lt $HealthReserveMinutes) {
        L ("health run: only {0:N1}m before stop-at ({1}) < reserve {2}m - skipped" -f ($stopAt - $now).TotalMinutes, $stopAt.ToString('s'), $HealthReserveMinutes)
        return 'skipped-budget'
    }
    if ($DryRun) {
        $otherSlots = Get-OtherSlotActiveCount
        $smokeCount = Get-RegisteredSmokeCount
        $freeze = Get-FreezeState
        L "health dry-run: stopAt=$($stopAt.ToString('s')) freeze=$($freeze.Active)($($freeze.Sha)) otherSlots=$otherSlots smokes=$smokeCount"
        return 'dry-run'
    }
    if (-not (TakeLock)) { L "health run: worker busy 30 min - trying again after next job"; return 'busy' }
    try {
        # Re-check window and remaining budget AFTER TakeLock (TakeLock may wait up to 30 min).
        $nowAfter = Get-Date
        if ($nowAfter -ge $tFrom -and $nowAfter -lt $tUntil) { L 'health run: entered trainer/AB window while waiting for lock - skipped'; return 'skipped-window' }
        if (($stopAt - $nowAfter).TotalMinutes -lt 10) { L 'health run: <10m left after lock wait - skipped'; return 'skipped-budget' }

        $env:CODEX_AGENT = 'night-loop'; $env:CODEX_LOCK_HELD = '1'; $env:CODEX_SMOKE_REMOTE = 'off'
        Beat "nightly health check build"
        if (-not (UpdateAndBuild)) { L "health run: build failed or freeze mismatch - skipping"; return 'build-failed' }

        $otherSlots = Get-OtherSlotActiveCount
        $healthJobs = if ($otherSlots -gt 0) {
            L "health run: $otherSlots other slot UE process(es) active - scaling Jobs from $Jobs to 1"
            1
        } else { $Jobs }

        $d = Join-Path $root (Get-Date -Format 'yyyy-MM-dd')
        New-Item -ItemType Directory -Force $d | Out-Null
        $tlog = Join-Path $d 'health_tests.log'
        $sjson = Join-Path $d 'health_smokes.json'
        $slog = Join-Path $d 'health_smokes.log'
        $smokeCount = Get-RegisteredSmokeCount

        L "health run: running all CodexTactics tests (stop-at $($stopAt.ToString('s')))"
        $testCmd = "& '$($Repo -replace "'","''")\Scripts\test.ps1' -Filter CodexTactics *> '$($tlog -replace "'","''")'; exit `$LASTEXITCODE"
        $tRes = Invoke-HealthChild 'all tests' $testCmd $stopAt
        $testExit = $tRes.ExitCode
        if ($tRes.TimedOut) {
            Add-Content -Encoding UTF8 (Join-Path $d 'digest.log') ("{0} {1} HEAD {2}: PARTIAL (stopped at {3} during tests)" -f (Get-Date -Format s), $(if ($HealthCheckOnly) { 'HEALTH-ONLY' } else { 'HEALTH' }), $script:BuiltHead, $stopAt.ToString('s'))
            $script:HealthCheckDone = $true
            return 'partial'
        }

        L "health run: running $smokeCount registered smokes (Jobs=$healthJobs, stop-at $($stopAt.ToString('s')))"
        $smokeCmd = "& '$($Repo -replace "'","''")\Scripts\smoke_parallel.ps1' -Remote off -Commands all -Jobs $healthJobs -ExtraArgs '-Multiprocess' -ResultJson '$($sjson -replace "'","''")' *> '$($slog -replace "'","''")'; exit `$LASTEXITCODE"
        $sRes = Invoke-HealthChild "$smokeCount smokes" $smokeCmd $stopAt
        $smokeExit = $sRes.ExitCode
        if ($sRes.TimedOut) {
            Add-Content -Encoding UTF8 (Join-Path $d 'digest.log') ("{0} {1} HEAD {2}: PARTIAL (stopped at {3} during smokes, testExit={4})" -f (Get-Date -Format s), $(if ($HealthCheckOnly) { 'HEALTH-ONLY' } else { 'HEALTH' }), $script:BuiltHead, $stopAt.ToString('s'), $testExit)
            $script:HealthCheckDone = $true
            return 'partial'
        }

        Beat "nightly health aggregator"
        L "health run: updating Saved\Health"
        $healthOut = Join-Path $Repo 'Saved\Health\health_latest.json'
        $debtOut = Join-Path $Repo 'Saved\Health\DEBT.md'
        $aggExit = 1
        $chatLine = if (Test-Path $Py) {
            $out = (& $Py -I (Join-Path $Repo 'Scripts\Tools\project_health.py') --repo $Repo --test-log $tlog --smoke-json $sjson --test-exit $testExit --smoke-exit $smokeExit --max-age-minutes 180 --strict --out-json $healthOut --debt-md $debtOut --chat-summary 2>&1)
            $aggExit = $LASTEXITCODE
            ($out | Select-Object -Last 1)
        } else {
            "no python at $Py"
        }
        $tag = if ($HealthCheckOnly) { 'HEALTH-ONLY' } else { 'HEALTH' }
        L "health run done (aggExit=$aggExit testExit=$testExit smokeExit=$smokeExit): $chatLine"
        Add-Content -Encoding UTF8 (Join-Path $d 'digest.log') ("{0} {1} HEAD {2}: {3}" -f (Get-Date -Format s), $tag, $script:BuiltHead, $chatLine)
        $script:HealthCheckDone = $true
        if ($aggExit -ne 0 -or $testExit -ne 0 -or $smokeExit -ne 0) { return 'fail' }
        return 'ok'
    } finally { ReleaseLock; Beat "after health run" }
}
if ($HealthCheckOnly) {
    $hStatus = RunNightlyHealthCheck
    L "health-check-only mode complete ($hStatus) - exit"
    Beat "health-check-only $hStatus"
    if ($hStatus -eq 'ok' -or $hStatus -eq 'dry-run') { exit 0 }
    exit 1
}
if ($QueueOnly) {
    if ($WindowMode -ne 'ab') { throw '-QueueOnly requires -WindowMode ab' }
    MaybeTrainer
    Beat 'queue-only ended'; exit 0
}
while ((Get-Date) -lt $deadline) {
    MaybeTrainer
    RunNightlyHealthCheck
    $k++
    $base = 1000 + 30 * $k
    $jobsList = @(
        @{ Label = "crossroads-c$k"; Cmd = 'CodexTactics.PmcCrossroadsSmoke'; Map = '/Game/Maps/Generated/L_Gen_Depot_1'; Json = 'L_PmcArenaDepot.json'; Args = 'Seed={seed} Out={out}'; Seeds = ($base..($base + 29)) },
        @{ Label = "alley-c$k"; Cmd = 'CodexTactics.PmcAlleySmoke'; Map = '/Game/Maps/Generated/L_Gen_Depot_1'; Json = 'L_PmcArenaDepot.json'; Args = 'Seed={seed} Out={out}'; Seeds = ($base..($base + 29)) },
        @{ Label = "x4tactical-c$k"; Cmd = 'CodexTactics.PmcArena'; Map = '/Game/Maps/Generated/L_Gen_Depot_X4'; Json = 'L_Gen_Depot_X4.json'; Args = 'Rounds=1 Seed={seed} Scenario=tactical Spawns=random Objective=1 Squad=3 Time=120 Opponent=pmc Out={out}'; Seeds = ($base..($base + 9)) },
        @{ Label = "roofdefence-c$k"; Preset = 'RoofDefence10' }
    )
    foreach ($j in $jobsList) {
        if ((Get-Date) -ge $deadline) { break }
        MaybeTrainer
        if (Test-Path (Join-Path $Repo 'Saved\Worker\STOP_NIGHTLOOP')) { L 'STOP_NIGHTLOOP - exit'; Beat 'stopped'; exit 0 }
        if (-not (TakeLock)) { L "worker busy 30 min - skipping $($j.Label)"; continue }
        try {
            $env:CODEX_AGENT = 'worker'; $env:CODEX_IGNORE_DESIGNER = '1'; $env:CODEX_SMOKE_REMOTE = 'off'; $env:CODEX_LOCK_HELD = '1'
            Beat "build check for $($j.Label)"
            if (-not (UpdateAndBuild)) { L "build failed - skipping $($j.Label)"; continue }
            $out = Join-Path $root ("{0}\{1}" -f (Get-Date -Format 'yyyy-MM-dd'), $j.Label)
            Beat "running $($j.Label)"; L "job $($j.Label) start"
            if ($j.Preset) { & $seedBatch -Preset $j.Preset -Jobs $Jobs -OutDir $out *> $null }
            else { & $seedBatch -Command $j.Cmd -Map $j.Map -LevelJson $j.Json -ExtraCmdArgs $j.Args -Seeds $j.Seeds -Jobs $Jobs -TimeoutSeconds 900 -OutDir $out *> $null }
            Digest $out $j.Label
            L "job $($j.Label) end"
        } finally { ReleaseLock; Beat "after $($j.Label)" }
    }
    if (($k % 3) -eq 0 -and (Get-Date) -lt $deadline -and (TakeLock)) {
        try {
            $env:CODEX_LOCK_HELD = '1'; Beat "scenario sweep c$k"
            $names = @((Get-Content (Join-Path $Repo 'Scripts\test_map.json') -Raw | ConvertFrom-Json).smokeRegistry | Where-Object { $_ -match 'Pmc|PMC|Tactical|LevelGen' })
            $json = Join-Path $root ("{0}\sweep-c{1}.json" -f (Get-Date -Format 'yyyy-MM-dd'), $k)
            & powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\smoke_parallel.ps1 -Remote off -Commands ($names -join ',') -Jobs $Jobs -ExtraArgs '-Multiprocess' -ResultJson $json *> $null
            if (Test-Path $json) {
                $r = @((Get-Content $json -Raw | ConvertFrom-Json).results)
                $bad = @($r | Where-Object { $_.State -notmatch '^PASS' } | ForEach-Object { $_.Name })
                Add-Content -Encoding UTF8 (Join-Path $root ("{0}\digest.log" -f (Get-Date -Format 'yyyy-MM-dd'))) ("{0} sweep-c{1} HEAD {2}: {3} run, FAIL [{4}]" -f (Get-Date -Format s), $k, $script:BuiltHead, $r.Count, ($bad -join ','))
            }
        } finally { ReleaseLock; Beat "after sweep c$k" }
    }
    if ($Once) { break }
}
L 'night loop end (deadline)'; Beat 'ended'
