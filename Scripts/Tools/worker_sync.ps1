# Second machine (codex-worker) remote runner, executed on the MAIN PC. See docs/port/SECOND_MACHINE.md.
# 1. updates the worker checkout to origin/main (or -Ref; -ShipHead ships the -BinariesFrom checkout's HEAD, unpushed commits
#    go over SSH as a git bundle) + git lfs pull (designer packs are untracked and never touched),
# 2. copies changed editor binaries (Binaries\Win64, no *.pdb unless -WithPdb) built on this PC - the worker never compiles,
# 3. runs a smoke batch on the worker (Scripts\smoke_parallel.ps1) over SSH,
# 4. brings the result JSON + the logs back to Saved\Logs\Worker\<timestamp>\ here (all logs with -AllLogs).
# Usage:
#   powershell -ExecutionPolicy Bypass -File Scripts\Tools\worker_sync.ps1                         (sync + MovementSmoke)
#   powershell -ExecutionPolicy Bypass -File Scripts\Tools\worker_sync.ps1 -Commands all -Jobs 2
#   powershell -ExecutionPolicy Bypass -File Scripts\Tools\worker_sync.ps1 -SkipSmoke              (sync only)
#   powershell -ExecutionPolicy Bypass -File Scripts\Tools\worker_sync.ps1 -Ref <commit> -SkipSmoke -Frozen   (the 16:30 trainer freeze)
#   powershell -ExecutionPolicy Bypass -File Scripts\Tools\worker_sync.ps1 -SkipSync -Commands CoverSmoke
# Smokes from an agent worktree go through Scripts\smoke_parallel.ps1 -Remote auto (Scripts\Tools\smoke_remote.ps1), not here.
# Exit code: 0 all smokes passed (or no smokes run), 1 a smoke failed, 2 sync / connection error / worker busy.
param(
    [string]$Worker = "codex-worker",                       # ~/.ssh/config host (Tailscale MagicDNS name)
    [string]$RemoteRoot = "C:/Work/CodexTactics",
    [string]$Ref = "origin/main",
    [switch]$ShipHead,                                      # Ref = HEAD of -BinariesFrom; unpushed commits are bundled over SSH
    # Folder whose Binaries\Win64 is copied; default = the checkout this script lives in. Build it from the same commit as -Ref.
    [string]$BinariesFrom = "",
    [string[]]$Commands = @("MovementSmoke"),
    [int]$Jobs = 2,                                         # i7-6700K: 4 cores / 8 threads -> 2 engines side by side
    [int]$TimeoutSeconds = 0,                               # 0 = smoke_parallel default
    [string]$ExtraArgs = "-Multiprocess",
    [string]$SmokeExtra = "",                               # more smoke_parallel arguments, e.g. "-PreCmds 'X 1' -RealTime"
    [string]$ResultDir = "",                                # where results come back (default Saved\Logs\Worker\<timestamp>)
    [switch]$AllLogs,                                       # bring back every smoke log (zip), not only the failed ones
    [switch]$NoRetry,
    [switch]$WithPdb,                                       # also copy *.pdb (~420 MB; wired link, crash analysis on the worker)
    [switch]$NoPdb,                                         # obsolete: no PDB is the default now
    [switch]$Frozen,                                        # this sync is the evening trainer freeze: writes Saved\Worker\FROZEN
    [int]$WaitEngineMinutes = 60,                           # how long to wait for a running wave / batch on the worker
    [switch]$SkipSync,
    [switch]$SkipBinaries,
    [switch]$SkipSmoke
)

$ErrorActionPreference = "Stop"
$RepoDir = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $BinariesFrom) { $BinariesFrom = $RepoDir }
$LocalBin = Join-Path $BinariesFrom "Binaries\Win64"
$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$OutDir = $(if ($ResultDir) { $ResultDir } else { Join-Path $RepoDir "Saved\Logs\Worker\$Stamp" })
$Token = "{0}-{1}-{2}" -f $env:COMPUTERNAME, $PID, $Stamp     # this run's id in the worker's SYNC.<token> / agent.lock

function Say([string]$m, [string]$c = "Gray") { Write-Host ("[worker_sync {0:HH:mm:ss}] {1}" -f (Get-Date), $m) -ForegroundColor $c }

# Runs a PowerShell script block text on the worker (EncodedCommand: no quoting problems through ssh) and returns its output lines.
function Invoke-Worker([string]$Script, [switch]$AllowFail) {
    $b64 = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes("`$ErrorActionPreference='Continue'; `$ProgressPreference='SilentlyContinue'; " + $Script))
    $out = & ssh -o ConnectTimeout=20 -o ServerAliveInterval=30 -o BatchMode=yes $Worker "powershell -NoProfile -NonInteractive -EncodedCommand $b64"
    $code = $LASTEXITCODE
    if ($code -ne 0 -and -not $AllowFail) { $out | ForEach-Object { Write-Host "  $_" }; throw "worker command failed (exit $code)" }
    $script:LastWorkerExit = $code
    return $out
}

# ---- handshake with the worker's own jobs (worker_loop.ps1, trainer), SECOND_MACHINE.md "Continuous loop" -----------------
# Saved\Worker\SYNC.<token> = the loop starts no new phase; Saved\Trainer\PAUSE (text "worker_sync") = a running trainer
# waits between arena waves (no stop); Saved\agent.lock (manual, agent "worker-sync", purpose = token), created atomically,
# is held for the WHOLE run (sync + smokes; the smokes run with CODEX_LOCK_HELD=1): two main-PC callers never mix their
# code / binaries on the worker. The PAUSE goes when no other SYNC.* is left; a dead run is cleared by the loop.
$script:SyncHeld = $false
function Enter-WorkerSync {
    $r = @(Invoke-Worker @"
New-Item -ItemType Directory -Force '$RemoteRoot/Saved/Worker', '$RemoteRoot/Saved/Trainer' | Out-Null
Set-Content -Encoding UTF8 '$RemoteRoot/Saved/Worker/SYNC.$Token' 'worker_sync $Token since $(Get-Date -Format s)'
if (-not (Test-Path '$RemoteRoot/Saved/Trainer/PAUSE')) { Set-Content -Encoding UTF8 '$RemoteRoot/Saved/Trainer/PAUSE' 'worker_sync'; 'PAUSE set' } else { 'PAUSE already there' }
"@)
    $script:SyncHeld = $true
    Say "handshake: SYNC.$Token, $($r[-1]) - waiting for the worker's lock (running arena wave / smoke batch)"
    $deadline = (Get-Date).AddMinutes($WaitEngineMinutes)
    while ($true) {
        $state = @(Invoke-Worker @"
`$l = '$RemoteRoot/Saved/agent.lock'
if (Test-Path `$l) {
    try { `$j = Get-Content `$l -Raw | ConvertFrom-Json } catch { `$j = `$null }
    if (`$j -and (`$j.manual -or (Get-Process -Id `$j.pid -ErrorAction SilentlyContinue))) { 'busy lock: ' + `$j.agent + ' (' + `$j.purpose + ')'; exit 0 }
    Remove-Item `$l -ErrorAction SilentlyContinue   # stale
}
`$e = @(Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" | Where-Object { `$_.CommandLine -match 'CodexTactics' -or `$_.Name -eq 'UnrealEditor.exe' } | ForEach-Object { `$_.Name })
if (`$e -contains 'UnrealEditor.exe') { 'EDITOR'; exit 0 }
if (`$e.Count) { 'busy engines: ' + (`$e -join ', '); exit 0 }
try {
    `$fs = [IO.File]::Open(`$l, 'CreateNew', 'Write')
    `$b = [Text.Encoding]::UTF8.GetBytes((@{ agent = 'worker-sync'; purpose = '$Token'; pid = 0; manual = `$true; started = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss') } | ConvertTo-Json))
    `$fs.Write(`$b, 0, `$b.Length); `$fs.Close(); 'TAKEN'
} catch { 'busy lock: taken by someone else just now' }
"@)
        $s = $state[-1]
        if ($s -eq "EDITOR") { throw "the Unreal Editor is open on the worker (locks the DLLs) - close it there, then re-run" }
        if ($s -eq "TAKEN") { break }
        if ((Get-Date) -gt $deadline) { throw "worker still busy after $WaitEngineMinutes min: $s" }
        Say "worker $s - waiting..." "Yellow"
        Start-Sleep -Seconds 30
    }
    $script:LockHeld = $true
}
function Exit-WorkerSync {
    if (-not $script:SyncHeld) { return }
    Invoke-Worker -AllowFail @"
`$l = '$RemoteRoot/Saved/agent.lock'
if ((Test-Path `$l) -and ((Get-Content `$l -Raw) -match '$Token')) { Remove-Item `$l }
Remove-Item '$RemoteRoot/Saved/Worker/SYNC.$Token' -ErrorAction SilentlyContinue
`$p = '$RemoteRoot/Saved/Trainer/PAUSE'
if (-not (Get-ChildItem '$RemoteRoot/Saved/Worker/SYNC.*' -ErrorAction SilentlyContinue) -and (Test-Path `$p) -and ((Get-Content `$p -Raw) -match 'worker_sync')) { Remove-Item `$p }
"@ | Out-Null
    $script:SyncHeld = $false
    Say "handshake released (lock, SYNC.$Token, trainer PAUSE if no other sync)"
}

try {
    Say "worker $Worker -> $RemoteRoot"
    $probe = @(Invoke-Worker "hostname; Test-Path '$RemoteRoot/CodexTactics.uproject'")
    if ($probe[-1] -ne "True") { throw "no checkout at $RemoteRoot on $Worker (see docs/port/SECOND_MACHINE.md, first setup)" }
    if (-not ($SkipSync -and $SkipBinaries -and $SkipSmoke)) { Enter-WorkerSync }
    $oldHead = @(Invoke-Worker "Set-Location '$RemoteRoot'; git rev-parse --short HEAD")[-1].Trim()

    # ---- 1. code -------------------------------------------------------------------------------------------------------
    if (-not $SkipSync) {
        $fetchExtra = ""
        if ($ShipHead) {
            $Ref = (& git -C $BinariesFrom rev-parse HEAD).Trim()
            & git -C $BinariesFrom fetch --quiet origin 2>$null
            $onRemote = @(& git -C $BinariesFrom branch -r --contains $Ref 2>$null).Count -gt 0
            if (-not $onRemote) {
                # Unpushed commits: bundle HEAD minus origin/main, fetch it on the worker from the bundle file.
                $bundle = Join-Path ([IO.Path]::GetTempPath()) "worker_sync-$Token.bundle"
                & git -C $BinariesFrom bundle create $bundle HEAD "^origin/main" 2>&1 | Out-Null
                if ($LASTEXITCODE -ne 0) { throw "git bundle failed for $Ref" }
                & scp -q $bundle "${Worker}:$RemoteRoot/Saved/Worker/ship.bundle"
                if ($LASTEXITCODE -ne 0) { throw "scp of the bundle failed" }
                Remove-Item $bundle -ErrorAction SilentlyContinue
                $fetchExtra = "git fetch --quiet '$RemoteRoot/Saved/Worker/ship.bundle' HEAD 2>&1 | Out-Null; if (`$LASTEXITCODE -ne 0) { 'BUNDLE-FAILED'; exit 2 }"
                Say "ship HEAD $($Ref.Substring(0,8)) (unpushed: bundled)"
            } else { Say "ship HEAD $($Ref.Substring(0,8)) (on origin)" }
        }
        Say "git: fetch + reset to $Ref + lfs pull"
        $git = Invoke-Worker @"
Set-Location '$RemoteRoot'
git fetch --quiet origin 2>&1 | Out-Null
if (`$LASTEXITCODE -ne 0) { 'FETCH-FAILED'; exit 2 }
$fetchExtra
git reset --quiet --hard '$Ref' 2>&1 | Out-Null
if (`$LASTEXITCODE -ne 0) { 'RESET-FAILED'; exit 2 }
git lfs pull 2>&1 | Select-Object -Last 1
'HEAD ' + (git rev-parse --short HEAD) + ' ' + (git log -1 --format=%s)
"@
        $git | ForEach-Object { Say "  $_" }
    }
    $workerHead = @(Invoke-Worker "Set-Location '$RemoteRoot'; git rev-parse HEAD")[-1].Trim()

    # ---- 2. binaries ---------------------------------------------------------------------------------------------------
    $changed = @()
    if (-not $SkipBinaries) {
        if (-not (Test-Path $LocalBin)) { throw "no $LocalBin - build first (Scripts\build.ps1)" }
        $binHead = (& git -C $BinariesFrom rev-parse HEAD).Trim()
        if ($binHead -ne $workerHead) {
            Say "binaries come from $($binHead.Substring(0,8)) but the worker is at $($workerHead.Substring(0,8)) - results reflect the binaries' code for C++, the worker's for data/JSON" "Yellow"
        }
        $localId = (Get-Content (Join-Path $LocalBin "UnrealEditor.modules") -Raw | ConvertFrom-Json).BuildId
        $engineId = @(Invoke-Worker "(Get-Content 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.modules' -Raw | ConvertFrom-Json).BuildId")[-1].Trim()
        if ($localId -ne $engineId) { throw "BuildId mismatch: binaries $localId, worker engine $engineId - update the engine in the Epic launcher on one side" }

        # Manifest compare: relative path + size + last-write time (whole seconds; scp -p keeps it); copy only what differs.
        # No *.pdb by default (2026-11-17): ~30 MB per C++ update instead of ~450 MB over Wi-Fi.
        $remoteList = Invoke-Worker @"
`$b = '$RemoteRoot/Binaries/Win64'
if (Test-Path `$b) { Get-ChildItem `$b -Recurse -File | ForEach-Object { `$_.FullName.Substring((Resolve-Path `$b).Path.Length + 1) + '|' + `$_.Length + '|' + [int64](`$_.LastWriteTimeUtc - [DateTime]'1970-01-01').TotalSeconds } }
"@
        $remote = @{}
        foreach ($l in $remoteList) { $p = $l -split '\|'; if ($p.Count -eq 3) { $remote[$p[0]] = "$($p[1])|$($p[2])" } }
        $local = Get-ChildItem $LocalBin -Recurse -File | Where-Object { $WithPdb -or $_.Extension -ne ".pdb" }
        $changed = @($local | Where-Object {
            $rel = $_.FullName.Substring($LocalBin.Length + 1)
            $remote[$rel] -ne "$($_.Length)|$([int64]($_.LastWriteTimeUtc - [DateTime]'1970-01-01').TotalSeconds)"
        })
        $mb = [Math]::Round((($changed | Measure-Object Length -Sum).Sum) / 1MB)
        Say "binaries: $($changed.Count) of $($local.Count) files changed ($mb MB$(if (-not $WithPdb) { ', no PDB' }))"
        foreach ($f in $changed) {
            $rel = $f.FullName.Substring($LocalBin.Length + 1)
            $sub = (Split-Path $rel -Parent) -replace '\\', '/'
            $dstDir = $(if ($sub) { "$RemoteRoot/Binaries/Win64/$sub" } else { "$RemoteRoot/Binaries/Win64" })
            if ($sub) { Invoke-Worker "New-Item -ItemType Directory -Force '$dstDir' | Out-Null" | Out-Null }
            & scp -q -p $f.FullName "${Worker}:$dstDir/"
            if ($LASTEXITCODE -ne 0) { throw "scp failed for $rel" }
        }
    }
    # Code change point for the trainer report: Saved\Worker\sync_points.log (time, old -> new HEAD, binaries, trainer generation).
    if (-not ($SkipSync -and $SkipBinaries)) {
        $binNote = $(if ($SkipBinaries) { "binaries not copied" } else { "binaries $((& git -C $BinariesFrom rev-parse --short HEAD).Trim()), $($changed.Count) files" })
        $kind = $(if ($Frozen) { "FROZEN " } else { "" })
        $point = @(Invoke-Worker @"
`$s = Get-ChildItem '$RemoteRoot/Saved/Trainer/*/status.json' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
`$t = `$(if (`$s) { `$j = Get-Content `$s.FullName -Raw | ConvertFrom-Json; "trainer `$(`$j.run_id) gen `$(`$j.generation) `$(`$j.phase) `$(`$j.jobs_done)/`$(`$j.jobs_total)" } else { 'no trainer run' })
`$line = (Get-Date -Format s) + ' $kind$oldHead -> ' + (git -C '$RemoteRoot' rev-parse --short HEAD) + ' | $binNote | ' + `$t
Add-Content -Encoding UTF8 '$RemoteRoot/Saved/Worker/sync_points.log' `$line
if ('$kind') { Set-Content -Encoding UTF8 '$RemoteRoot/Saved/Worker/FROZEN' `$line }
`$line
"@)
        Say "sync point: $($point[-1])"
    }
} catch {
    Say "SYNC ERROR: $_" "Red"
    Exit-WorkerSync
    exit 2
}

# ---- 3. smokes on the worker (still under this run's lock: CODEX_LOCK_HELD=1) ----------------------------------------------
if ($SkipSmoke) { Say "smokes skipped"; Exit-WorkerSync; exit 0 }
$names = ($Commands | ForEach-Object { $_ -split '[,;\s]+' } | Where-Object { $_ }) -join ","
$smokeArgs = "-Commands '$names' -Jobs $Jobs -ExtraArgs '$ExtraArgs' -ResultJson '$RemoteRoot/Saved/Logs/SmokeParallel-worker.json' $SmokeExtra"
if ($NoRetry) { $smokeArgs += " -NoRetry" }
if ($TimeoutSeconds -gt 0) { $smokeArgs += " -TimeoutSeconds $TimeoutSeconds" }
Say "smokes on worker: $names (jobs $Jobs)"
$run = Invoke-Worker -AllowFail @"
Set-Location '$RemoteRoot'
`$env:CODEX_AGENT = 'worker'
`$env:CODEX_IGNORE_DESIGNER = '1'
`$env:CODEX_LOCK_HELD = '1'
`$env:CODEX_SMOKE_REMOTE = 'off'
Remove-Item 'Saved/Logs/SmokeParallel-worker.json' -ErrorAction SilentlyContinue
& powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 $smokeArgs *>&1 | ForEach-Object { "`$_" }
exit `$LASTEXITCODE
"@
$smokeExit = $script:LastWorkerExit
$run | ForEach-Object { Write-Host "  $_" }

# ---- 4. results back ------------------------------------------------------------------------------------------------------
New-Item -ItemType Directory -Force $OutDir | Out-Null
& scp -q "${Worker}:$RemoteRoot/Saved/Logs/SmokeParallel-worker.json" (Join-Path $OutDir "SmokeParallel-worker.json")
$json = Join-Path $OutDir "SmokeParallel-worker.json"
if (Test-Path $json) {
    $res = Get-Content $json -Raw | ConvertFrom-Json
    $want = @($res.results | Where-Object { $AllLogs -or $_.State -eq "FAIL" } | ForEach-Object { ($_.Log -replace '\\', '/') })
    if ($want.Count) {
        $list = ($want | ForEach-Object { "'" + $_ + "'" }) -join ","
        Invoke-Worker -AllowFail "Compress-Archive -Force -Path @($list) -DestinationPath '$RemoteRoot/Saved/Worker/logs-$Token.zip'" | Out-Null
        $zip = Join-Path $OutDir "logs.zip"
        & scp -q "${Worker}:$RemoteRoot/Saved/Worker/logs-$Token.zip" $zip
        if (Test-Path $zip) { Expand-Archive -Force $zip $OutDir; Remove-Item $zip }
        Invoke-Worker -AllowFail "Remove-Item '$RemoteRoot/Saved/Worker/logs-$Token.zip' -ErrorAction SilentlyContinue" | Out-Null
    }
    Say ("results: {0} -> {1}" -f (@($res.results | ForEach-Object { "$($_.Name)=$($_.State)" }) -join ", "), $OutDir) $(if ($smokeExit -eq 0) { "Green" } else { "Red" })
} else {
    Say "no result JSON came back (smoke_parallel exit $smokeExit)" "Red"
    if ($smokeExit -eq 0) { $smokeExit = 2 }
}
Exit-WorkerSync
exit $smokeExit
