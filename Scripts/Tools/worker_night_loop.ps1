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
#   05:30-07:30 (-TrainerFrom .. -TrainerUntil): the weight trainer (night_trainer.py, run pmc-v6-qd2; designer 11-19: 2 h of
#   the night for the trainer, the rest for tasks).
param([string]$Repo = 'C:\Work\CodexTactics', [string]$Until = '07:45', [int]$Jobs = 4, [switch]$Once,
      [string]$TrainerFrom = '05:30', [string]$TrainerUntil = '07:30', [string]$TrainerRunId = 'pmc-v6-qd2',
      [string]$Py = 'C:\Users\User\AppData\Local\Programs\Python\Python312\python.exe')
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
function UpdateAndBuild {
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
function TrainerAt([string]$hhmm) { $t = [datetime]::ParseExact($hhmm, 'HH:mm', $null); return $deadline.Date.Add($t.TimeOfDay) }
$script:TrainerDone = $false
function MaybeTrainer {
    # Designer 2026-11-19: 2 hours of the night for the trainer (the end of the night), the rest for tasks.
    if ($script:TrainerDone -or (Get-Date) -lt (TrainerAt $TrainerFrom) -or (Get-Date) -ge (TrainerAt $TrainerUntil)) { return }
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
while ((Get-Date) -lt $deadline) {
    MaybeTrainer
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
