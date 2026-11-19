# codex-worker nightly scenario regression sweep (designer 2026-11-19, smart-enemies roadmap 5a, commit 99f0595e): the
# 16:30-08:00 window runs the PMC / tactical / levelgen smokes on fresh origin/main instead of the weight trainer (paused until
# T-10; Saved\Worker\STOP keeps the Worker Loop / run_night_trainer.ps1 off). Until Antigravity's T-10 scenario runner (091)
# exists this is the stand-in. Runs ON the worker (task "CodexTactics Night Sweep", daily 16:35):
#   1. waits for day callers (Saved\Worker\SYNC.* / Saved\agent.lock, max 90 min), then takes the agent.lock (agent night-sweep);
#   2. git fetch + checkout main = origin/main (no clean: untracked packs stay) + lfs pull; Scripts\build.ps1;
#   3. smoke_parallel -Remote off over test_map.json smokeRegistry names matching -Pattern, -Jobs 4 (failures re-run once: a
#      PASS* is a flaky seed);
#   4. Saved\Worker\nightsweep\<yyyy-MM-dd>.json (+ .log) and one summary line in Saved\Worker\nightsweep\sweeps.log.
# The morning report (worker-ops) compares tonight's JSON with the previous night's: regressions / stable / flaky.
param([string]$Pattern = 'Pmc|PMC|Tactical|LevelGen', [int]$Jobs = 4, [string]$Repo = 'C:\Work\CodexTactics', [switch]$DryRun)
$ErrorActionPreference = 'Continue'
Set-Location $Repo
$dir = Join-Path $Repo 'Saved\Worker\nightsweep'; New-Item -ItemType Directory -Force $dir | Out-Null
$day = Get-Date -Format 'yyyy-MM-dd'; $log = Join-Path $dir "$day.log"
function L([string]$m) { $line = '{0} {1}' -f (Get-Date -Format s), $m; Add-Content -Encoding UTF8 $log $line }
$lock = Join-Path $Repo 'Saved\agent.lock'
$deadline = (Get-Date).AddMinutes(90)
while (((Get-ChildItem (Join-Path $Repo 'Saved\Worker') -Filter 'SYNC.*' -ErrorAction SilentlyContinue) -or (Test-Path $lock)) -and (Get-Date) -lt $deadline) { Start-Sleep 30 }
try {
    $fs = [IO.File]::Open($lock, 'CreateNew', 'Write')
    $b = [Text.Encoding]::UTF8.GetBytes((@{ agent = 'night-sweep'; purpose = "night sweep $day"; pid = $PID; manual = $true; started = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss') } | ConvertTo-Json))
    $fs.Write($b, 0, $b.Length); $fs.Close()
} catch { L "worker busy (lock / sync after 90 min) - sweep skipped"; exit 2 }
try {
    $env:CODEX_AGENT = 'worker'; $env:CODEX_IGNORE_DESIGNER = '1'; $env:CODEX_SMOKE_REMOTE = 'off'; $env:CODEX_LOCK_HELD = '1'
    git fetch --quiet origin 2>&1 | Out-Null
    git checkout --quiet -B main origin/main 2>&1 | Out-Null
    git lfs pull 2>&1 | Out-Null
    $head = (git rev-parse --short HEAD).Trim()
    $names = @((Get-Content (Join-Path $Repo 'Scripts\test_map.json') -Raw | ConvertFrom-Json).smokeRegistry | Where-Object { $_ -match $Pattern })
    L "HEAD $head, $($names.Count) smokes match '$Pattern', jobs $Jobs"
    if ($DryRun) { L ('dry: ' + ($names -join ',')); return }
    $b = (& powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\build.ps1 2>&1 | Out-String)
    $bc = $LASTEXITCODE
    L ("build exit $bc " + (($b -split "`n" | Where-Object { $_ -match 'Result:' } | Select-Object -Last 1)))
    $json = Join-Path $dir "$day.json"
    if ($bc -eq 0) {
        & powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\smoke_parallel.ps1 -Remote off -Commands ($names -join ',') -Jobs $Jobs `
            -ExtraArgs '-Multiprocess' -ResultJson $json *>&1 | Add-Content -Encoding UTF8 $log
    }
    $sum = 'no result'
    if (Test-Path $json) {
        $r = @((Get-Content $json -Raw | ConvertFrom-Json).results)
        $bad = @($r | Where-Object { $_.State -notmatch '^PASS' } | ForEach-Object { $_.Name })
        $flaky = @($r | Where-Object { $_.State -eq 'PASS*' } | ForEach-Object { $_.Name })
        $sum = '{0} run, {1} FAIL [{2}], {3} flaky [{4}]' -f $r.Count, $bad.Count, ($bad -join ','), $flaky.Count, ($flaky -join ',')
    }
    Add-Content -Encoding UTF8 (Join-Path $dir 'sweeps.log') ("$day HEAD $head build $bc | $sum")
    L "done: $sum"
} finally {
    if ((Test-Path $lock) -and ((Get-Content $lock -Raw) -match 'night-sweep')) { Remove-Item $lock -Force }
}
