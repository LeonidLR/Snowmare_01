# Seeded / repeated engine runs of ONE console command, in parallel, for the second machine (codex-worker, repo C:\Work\CodexTactics).
# Every run is its own UnrealEditor-Cmd process with its own -abslog and (for PmcArena / PmcRoofDefenceSmoke) its own result JSON (Out=...).
# The engine command line follows Scripts\smoke_parallel.ps1: map, -LevelJson, the fixed game step (-UseFixedTimeStep -FPS=60 -FixedSeed),
# -SquadStart=formation (test_map.json smokeDefaults.squadStart), -nullrhi, -Multiprocess. Pass / fail = "Smoke RESULT: PASS" in the log
# (smoke_parallel's Get-Verdict); a FAIL smoke is DATA, not a script error. A non-smoke command (PmcArena) has no verdict: DONE when its
# result JSON was written, NOJSON otherwise.
# Writes <OutDir>\summary.json + <OutDir>\summary.txt (per-run rows + p50 / mean across runs). The LAST line printed is SEEDBATCH_OUT=<OutDir>.
# Exit: 0 = every run finished (any verdict), 1 = script error (bad arguments, engine / project missing).
#
# Usage on the worker:
#   powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\Tools\worker_seed_batch.ps1 -Preset RoofDefence10
#   powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\Tools\worker_seed_batch.ps1 -Preset ArenaX4Before
#   ... -Preset ArenaX4Before -Seeds 1,2,3 -Jobs 3 -DryRun        (print the engine command lines, start nothing)
#   ... -Command CodexTactics.PmcArena -Map /Game/Maps/Generated/L_Gen_Depot_X4 -LevelJson L_Gen_Depot_X4.json `
#       -ExtraCmdArgs "Rounds=1 Seed={seed} Scenario=tactical Out={out}" -Seeds 5,6,7
# Placeholders in -ExtraCmdArgs: {seed} = this run's seed (or the run number with -Repeat), {out} = this run's result JSON path (project-relative,
# forward slashes, the Out= convention), {n} = the run number.
#
# From the main PC (this script never builds; the worker never compiles):
#   1. powershell -ExecutionPolicy Bypass -File Scripts/Tools/worker_sync.ps1 -Ref origin/main -SkipSmoke        (syncs code + binaries)
#   2. ssh codex-worker "powershell -NoProfile -ExecutionPolicy Bypass -File C:\Work\CodexTactics\Scripts\Tools\worker_seed_batch.ps1 -Preset ArenaX4Before"
#      (the output ends with SEEDBATCH_OUT=C:\Work\CodexTactics\Saved\Logs\SeedBatch\<stamp>)
#   3. scp -r codex-worker:C:/Work/CodexTactics/Saved/Logs/SeedBatch/<stamp> Saved/Logs/Worker/
#
# Presets:
#   RoofDefence10   CodexTactics.PmcRoofDefenceSmoke x10 repeats on /Game/Maps/Generated/L_Gen_Depot_1 (-LevelJson=L_PmcArenaDepot.json, timeout 900 s,
#                   test_map.json smokeOptions) - same command, distinct logs; PASS/FAIL + defence block (spread_m, guard_share, shared_cover_pairs, objective_share).
#   ArenaX4Before   CodexTactics.PmcArena, one tactical round per seed 11,22,...,110 on /Game/Maps/Generated/L_Gen_Depot_X4 (-LevelJson=L_Gen_Depot_X4.json):
#                   the PmcArenaTacticalSmoke settings (Scenario=tactical Spawns=random Objective=1 Squad=3 Time=120 Opponent=pmc) without the validation.
#
# Metrics read from each result JSON (rounds[0], schema codex.pmc_arena/1):
#   defence.spread_m / guard_share / shared_cover_pairs / objective_share        (roofs rounds only; absent on a tactical round -> "missing")
#   tactical.posts[] (kind per defender), sides.<defender>.shots, sides.<defender>.behaviour.metrics.{overwatch_kills,window_positions_used}.value,
#   stats.<defender>.post_moves / tactical_fall_backs.
#   NOT in the result (reported as missing, no C++ added): defenders' time on post, shots fired FROM posts (only side totals; the per-shot events
#   with an "overwatch" flag are in the round's track file next to the JSON, not summarised here).
param(
    [string]$Command = "",
    [string]$Map = "",
    [string]$LevelJson = "",
    [string]$ExtraCmdArgs = "",
    [int[]]$Seeds = @(),
    [int]$Repeat = 0,
    [int]$Jobs = 5,
    [int]$TimeoutSeconds = 0,
    [string]$OutDir = "",
    [ValidateSet("", "RoofDefence10", "ArenaX4Before")][string]$Preset = "",
    [string]$EngineExe = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Project = Join-Path $Repo "CodexTactics.uproject"

# Presets fill whatever the caller left empty.
switch ($Preset) {
    "RoofDefence10" {
        if (-not $Command) { $Command = "CodexTactics.PmcRoofDefenceSmoke" }
        if (-not $Map) { $Map = "/Game/Maps/Generated/L_Gen_Depot_1" }
        if (-not $LevelJson) { $LevelJson = "L_PmcArenaDepot.json" }
        if (-not $ExtraCmdArgs) { $ExtraCmdArgs = "Out={out}" }
        if ($Seeds.Count -eq 0 -and $Repeat -le 0) { $Repeat = 10 }
        if ($TimeoutSeconds -le 0) { $TimeoutSeconds = 900 }
    }
    "ArenaX4Before" {
        if (-not $Command) { $Command = "CodexTactics.PmcArena" }
        if (-not $Map) { $Map = "/Game/Maps/Generated/L_Gen_Depot_X4" }
        if (-not $LevelJson) { $LevelJson = "L_Gen_Depot_X4.json" }
        if (-not $ExtraCmdArgs) { $ExtraCmdArgs = "Rounds=1 Seed={seed} Scenario=tactical Spawns=random Objective=1 Squad=3 Time=120 Opponent=pmc Out={out}" }
        if ($Seeds.Count -eq 0 -and $Repeat -le 0) { $Seeds = @(11, 22, 33, 44, 55, 66, 77, 88, 99, 110) }
        if ($TimeoutSeconds -le 0) { $TimeoutSeconds = 900 }
    }
}
if (-not $Command) { Write-Host "No -Command / -Preset given." -ForegroundColor Red; exit 1 }
if (-not $Map) { Write-Host "No -Map given." -ForegroundColor Red; exit 1 }
if ($Seeds.Count -eq 0 -and $Repeat -le 0) { Write-Host "Give -Seeds or -Repeat." -ForegroundColor Red; exit 1 }
if ($TimeoutSeconds -le 0) { $TimeoutSeconds = 900 }
if ($Jobs -lt 1) { $Jobs = 1 }
if (-not $DryRun) {
    if (-not (Test-Path $EngineExe)) { Write-Host "Engine not found: $EngineExe" -ForegroundColor Red; exit 1 }
    if (-not (Test-Path $Project)) { Write-Host "Project not found: $Project" -ForegroundColor Red; exit 1 }
}
if ($Command -notmatch '^CodexTactics\.') { $Command = "CodexTactics.$Command" }
$IsSmoke = $Command -match 'Smoke$'

$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
if (-not $OutDir) { $OutDir = Join-Path $Repo "Saved\Logs\SeedBatch\$Stamp" }
if (-not [System.IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path $Repo $OutDir }
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
if (-not $DryRun) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }

# The run list: seeds, or Repeat runs (run number as {seed}).
$Runs = @()
if ($Seeds.Count -gt 0) {
    $i = 0
    foreach ($s in $Seeds) { $i++; $Runs += [pscustomobject]@{ N = $i; Seed = $s; Tag = "seed$s" } }
} else {
    for ($i = 1; $i -le $Repeat; $i++) { $Runs += [pscustomobject]@{ N = $i; Seed = $i; Tag = ("run{0:D2}" -f $i) } }
}

# Out= is project-relative when the OutDir is inside the repo (the engine resolves it against the project dir), else absolute.
function Get-OutArg([string]$Tag) {
    $abs = Join-Path $OutDir "$Tag.json"
    $root = $Repo.TrimEnd('\') + '\'
    $p = $(if ($abs.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) { $abs.Substring($root.Length) } else { $abs })
    return ($p -replace '\\', '/')
}

function Get-EngineArgs($Run) {
    $log = Join-Path $OutDir "$($Run.Tag).log"
    $extra = $ExtraCmdArgs.Replace("{seed}", "$($Run.Seed)").Replace("{n}", "$($Run.N)").Replace("{out}", (Get-OutArg $Run.Tag))
    $exec = $(if ($extra.Trim()) { "$Command $($extra.Trim())" } else { $Command })
    $lj = $(if ($LevelJson) { " -LevelJson=$LevelJson" } else { "" })
    return "`"$Project`" $Map -game -nullrhi -nosplash -nosound -unattended -windowed -FORCELOGFLUSH -NoTelemetry `"-ExecCmds=$exec`" `"-abslog=$log`"$lj -UseFixedTimeStep -FPS=60 -FixedSeed -SquadStart=formation -Multiprocess"
}

Write-Host ("[seed_batch] {0}: {1} runs, {2} at a time, timeout {3} s, out {4}" -f $Command, $Runs.Count, $Jobs, $TimeoutSeconds, $OutDir)
if ($DryRun) {
    foreach ($r in $Runs) { Write-Host ("[dry-run] {0}: `"{1}`" {2}" -f $r.Tag, $EngineExe, (Get-EngineArgs $r)) }
    Write-Host "SEEDBATCH_OUT=$OutDir"
    exit 0
}

# ---- run ----
$queue = [System.Collections.Generic.Queue[object]]::new()
foreach ($r in $Runs) { $queue.Enqueue($r) }
$running = @()
$done = @()
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($queue.Count -gt 0 -or $running.Count -gt 0) {
    while ($running.Count -lt $Jobs -and $queue.Count -gt 0) {
        $r = $queue.Dequeue()
        $log = Join-Path $OutDir "$($r.Tag).log"
        $json = Join-Path $OutDir "$($r.Tag).json"
        Remove-Item $log, $json -ErrorAction SilentlyContinue
        $proc = Start-Process $EngineExe -ArgumentList (Get-EngineArgs $r) -WindowStyle Hidden -PassThru
        $running += [pscustomobject]@{ Run = $r; Proc = $proc; Log = $log; Json = $json; Started = Get-Date }
    }
    Start-Sleep -Milliseconds 500
    foreach ($job in @($running)) {
        $elapsed = ((Get-Date) - $job.Started).TotalSeconds
        $timedOut = $false
        if (-not $job.Proc.HasExited -and $elapsed -gt $TimeoutSeconds) {
            Stop-Process -Id $job.Proc.Id -Force -ErrorAction SilentlyContinue
            $timedOut = $true
        }
        if ($job.Proc.HasExited -or $timedOut) {
            $job.Proc.WaitForExit()
            $running = @($running | Where-Object { $_ -ne $job })
            $pass = (Test-Path $job.Log) -and (Select-String -Path $job.Log -Pattern "Smoke RESULT: PASS" -Quiet)
            $hasJson = Test-Path $job.Json
            $state = $(if ($timedOut) { "TIMEOUT" } elseif ($IsSmoke) { $(if ($pass) { "PASS" } else { "FAIL" }) } else { $(if ($hasJson) { "DONE" } else { "NOJSON" }) })
            $done += [pscustomobject]@{ Run = $job.Run; State = $state; Seconds = [Math]::Round($elapsed, 1); Log = $job.Log; Json = $(if ($hasJson) { $job.Json } else { $null }) }
            Write-Host ("  {0,-10} {1,-8} {2,7:N1}s" -f $job.Run.Tag, $state, $elapsed)
        }
    }
}
$sw.Stop()

# ---- metrics ----
# Reads a dotted path from a JSON object; $null when any hop is missing / JSON null.
function Get-Path($Obj, [string]$Path) {
    $cur = $Obj
    foreach ($hop in $Path.Split('.')) {
        if ($null -eq $cur) { return $null }
        $prop = $cur.PSObject.Properties[$hop]
        if ($null -eq $prop) { return $null }
        $cur = $prop.Value
    }
    return $cur
}
function Get-Median([double[]]$V) {
    if ($V.Count -eq 0) { return $null }
    $s = @($V | Sort-Object)
    $m = [int][Math]::Floor($s.Count / 2)
    if ($s.Count % 2 -eq 1) { return $s[$m] }
    return ($s[$m - 1] + $s[$m]) / 2
}

$MetricNames = @(
    "defence_spread_m", "defence_guard_share", "defence_shared_cover_pairs", "defence_objective_share",
    "defender_shots", "defender_overwatch_kills", "defender_window_positions_used", "defender_post_moves", "defender_tactical_fall_backs",
    "posts_total", "posts_overwatch", "attacker_kills", "defender_kills", "duration_s"
)
$Rows = @()
foreach ($d in ($done | Sort-Object { $_.Run.N })) {
    $row = [ordered]@{ tag = $d.Run.Tag; seed = $d.Run.Seed; state = $d.State; seconds = $d.Seconds; log = $d.Log; json = $d.Json }
    foreach ($m in $MetricNames) { $row[$m] = $null }
    $row["winner"] = $null; $row["end_reason"] = $null; $row["scenario"] = $null
    if ($d.Json) {
        try {
            $doc = Get-Content -Raw -Encoding UTF8 $d.Json | ConvertFrom-Json
            $round = @($doc.rounds)[0]
            if ($round) {
                $row.winner = Get-Path $round "winner"
                $row.end_reason = Get-Path $round "end_reason"
                $row.scenario = Get-Path $round "scenario"
                $row.duration_s = Get-Path $round "duration_s"
                $row.defence_spread_m = Get-Path $round "defence.spread_m"
                $row.defence_guard_share = Get-Path $round "defence.guard_share"
                $row.defence_shared_cover_pairs = Get-Path $round "defence.shared_cover_pairs"
                $row.defence_objective_share = Get-Path $round "defence.objective_share"
                # The defender side letter: the side whose role is "defender".
                $defSide = $null
                foreach ($side in @("A", "B")) { if ((Get-Path $round "sides.$side.role") -eq "defender") { $defSide = $side } }
                if ($defSide) {
                    $atkSide = $(if ($defSide -eq "A") { "B" } else { "A" })
                    $row.defender_shots = Get-Path $round "sides.$defSide.shots"
                    $row.defender_kills = Get-Path $round "sides.$defSide.kills"
                    $row.attacker_kills = Get-Path $round "sides.$atkSide.kills"
                    $row.defender_overwatch_kills = Get-Path $round "sides.$defSide.behaviour.metrics.overwatch_kills.value"
                    $row.defender_window_positions_used = Get-Path $round "sides.$defSide.behaviour.metrics.window_positions_used.value"
                    $row.defender_post_moves = Get-Path $round "stats.$defSide.post_moves"
                    $row.defender_tactical_fall_backs = Get-Path $round "stats.$defSide.tactical_fall_backs"
                }
                $posts = Get-Path $round "tactical.posts"
                if ($null -ne $posts) {
                    $row.posts_total = @($posts).Count
                    $row.posts_overwatch = @($posts | Where-Object { $_.kind -eq "overwatch" }).Count
                }
            }
        } catch { $row["parse_error"] = $_.Exception.Message }
    }
    $Rows += [pscustomobject]$row
}

$Agg = [ordered]@{}
foreach ($m in $MetricNames) {
    $vals = @($Rows | Where-Object { $null -ne $_.$m } | ForEach-Object { [double]$_.$m })
    if ($vals.Count -eq 0) { $Agg[$m] = [ordered]@{ n = 0; p50 = $null; mean = $null; note = "missing in every result JSON" }; continue }
    $Agg[$m] = [ordered]@{ n = $vals.Count; p50 = [Math]::Round((Get-Median $vals), 3); mean = [Math]::Round((($vals | Measure-Object -Average).Average), 3) }
}
$Missing = @("defender time on post (s): not in the result JSON", "shots fired from posts: only side totals (sides.<defender>.shots); per-shot events are in the track file")
$counts = [ordered]@{}
foreach ($g in ($Rows | Group-Object state)) { $counts[$g.Name] = $g.Count }
$Summary = [ordered]@{
    command = $Command; map = $Map; level_json = $LevelJson; extra_cmd_args = $ExtraCmdArgs; preset = $Preset; jobs = $Jobs; timeout_s = $TimeoutSeconds
    stamp = $Stamp; wall_seconds = [Math]::Round($sw.Elapsed.TotalSeconds, 1); states = $counts; runs = $Rows; aggregate = $Agg; missing_metrics = $Missing
}
$Summary | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 (Join-Path $OutDir "summary.json")

# ---- text table ----
function Fmt($v) { if ($null -eq $v) { return "-" } return "$v" }
$cols = @("defence_spread_m", "defence_guard_share", "defence_shared_cover_pairs", "defence_objective_share", "defender_shots", "defender_overwatch_kills", "defender_post_moves")
$short = @("spread", "guard", "sharedCov", "objShare", "defShots", "owKills", "postMoves")
$lines = @()
$lines += ("{0,-10} {1,-8} {2,7} {3,-9} {4,-12}" -f "run", "state", "sec", "winner", "end") + (($short | ForEach-Object { " {0,9}" -f $_ }) -join "")
foreach ($r in $Rows) {
    $lines += ("{0,-10} {1,-8} {2,7:N1} {3,-9} {4,-12}" -f $r.tag, $r.state, $r.seconds, (Fmt $r.winner), (Fmt $r.end_reason)) + ((@($cols | ForEach-Object { " {0,9}" -f (Fmt $r.$_) })) -join "")
}
$lines += ("{0,-10} {1,-8} {2,7} {3,-9} {4,-12}" -f "p50", "", "", "", "") + ((@($cols | ForEach-Object { " {0,9}" -f (Fmt $Agg[$_].p50) })) -join "")
$lines += ("{0,-10} {1,-8} {2,7} {3,-9} {4,-12}" -f "mean", "", "", "", "") + ((@($cols | ForEach-Object { " {0,9}" -f (Fmt $Agg[$_].mean) })) -join "")
$lines += ""
$lines += "states: " + (($counts.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ", ") + ("; wall {0:N0} s" -f $sw.Elapsed.TotalSeconds)
$lines += "missing metrics: " + ($Missing -join "; ")
$lines | Set-Content -Encoding UTF8 (Join-Path $OutDir "summary.txt")
$lines | ForEach-Object { Write-Host $_ }
Write-Host "SEEDBATCH_OUT=$OutDir"
exit 0
