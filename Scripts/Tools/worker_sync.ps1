# Second machine (codex-worker) remote runner, executed on the MAIN PC. See docs/port/SECOND_MACHINE.md.
# 1. updates the worker checkout to origin/main (or -Ref) + git lfs pull (designer packs are untracked and never touched),
# 2. copies changed editor binaries (Binaries\Win64) built on this PC - the worker never compiles (slow 2016 CPU),
# 3. runs a smoke batch on the worker (Scripts\smoke_parallel.ps1) over SSH,
# 4. brings the result JSON + the logs of failed smokes back to Saved\Logs\Worker\<timestamp>\ here.
# Usage:
#   powershell -ExecutionPolicy Bypass -File Scripts\Tools\worker_sync.ps1                         (sync + MovementSmoke)
#   powershell -ExecutionPolicy Bypass -File Scripts\Tools\worker_sync.ps1 -Commands all -Jobs 2
#   powershell -ExecutionPolicy Bypass -File Scripts\Tools\worker_sync.ps1 -SkipSmoke              (sync only)
#   powershell -ExecutionPolicy Bypass -File Scripts\Tools\worker_sync.ps1 -SkipSync -Commands CoverSmoke
# Exit code: 0 all smokes passed (or no smokes run), 1 a smoke failed, 2 sync / connection error.
param(
    [string]$Worker = "codex-worker",                       # ~/.ssh/config host (Tailscale MagicDNS name)
    [string]$RemoteRoot = "C:/Work/CodexTactics",
    [string]$Ref = "origin/main",
    # Folder whose Binaries\Win64 is copied; default = the checkout this script lives in. Build it from the same commit as -Ref.
    [string]$BinariesFrom = "",
    [string[]]$Commands = @("MovementSmoke"),
    [int]$Jobs = 2,                                         # i7-6700K: 4 cores / 8 threads -> 2 engines side by side
    [int]$TimeoutSeconds = 0,                               # 0 = smoke_parallel default
    [string]$ExtraArgs = "-Multiprocess",
    [switch]$NoRetry,
    [switch]$NoPdb,                                         # skip *.pdb (smaller copy, no symbols in worker crash call stacks)
    [int]$WaitEngineMinutes = 60,                           # how long to wait for running smoke / bot engines before copying DLLs
    [switch]$SkipSync,
    [switch]$SkipBinaries,
    [switch]$SkipSmoke
)

$ErrorActionPreference = "Stop"
$RepoDir = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $BinariesFrom) { $BinariesFrom = $RepoDir }
$LocalBin = Join-Path $BinariesFrom "Binaries\Win64"
$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$OutDir = Join-Path $RepoDir "Saved\Logs\Worker\$Stamp"

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

# Handshake with the worker's own jobs (worker_loop.ps1, night trainer), see SECOND_MACHINE.md "Continuous loop":
# Saved\Worker\SYNC = the loop starts no new phase; Saved\Trainer\PAUSE (text "worker_sync") = a running trainer waits
# between arena waves (no stop, the run goes on afterwards); Saved\agent.lock (manual, agent worker-sync) while code and
# binaries change. Both files are removed at the end (PAUSE only if this script made it); a dead sync is cleared by the loop.
$script:SyncHeld = $false
function Enter-WorkerSync {
    $r = @(Invoke-Worker @"
New-Item -ItemType Directory -Force '$RemoteRoot/Saved/Worker', '$RemoteRoot/Saved/Trainer' | Out-Null
Set-Content -Encoding UTF8 '$RemoteRoot/Saved/Worker/SYNC' 'worker_sync from $env:COMPUTERNAME since $(Get-Date -Format s)'
if (-not (Test-Path '$RemoteRoot/Saved/Trainer/PAUSE')) { Set-Content -Encoding UTF8 '$RemoteRoot/Saved/Trainer/PAUSE' 'worker_sync'; 'PAUSE set' } else { 'PAUSE already there' }
"@)
    $script:SyncHeld = $true
    Say "handshake: SYNC set, $($r[-1]) - waiting for the running arena wave / smoke batch to finish"
    $deadline = (Get-Date).AddMinutes($WaitEngineMinutes)
    while ($true) {
        $busy = @(Invoke-Worker @"
`$l = '$RemoteRoot/Saved/agent.lock'
if (Test-Path `$l) { try { `$j = Get-Content `$l -Raw | ConvertFrom-Json; if (`$j.manual -or (Get-Process -Id `$j.pid -ErrorAction SilentlyContinue)) { 'lock: ' + `$j.agent + ' (' + `$j.purpose + ')' } } catch { } }
Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" | Where-Object { `$_.CommandLine -match 'CodexTactics' -or `$_.Name -eq 'UnrealEditor.exe' } | ForEach-Object { 'engine: ' + `$_.Name }
"@)
        if ($busy -contains "engine: UnrealEditor.exe") { throw "the Unreal Editor is open on the worker (locks the DLLs) - close it there, then re-run" }
        if ($busy.Count -eq 0) { break }
        if ((Get-Date) -gt $deadline) { throw "worker still busy after $WaitEngineMinutes min: $($busy -join '; ')" }
        Say "worker busy: $($busy -join '; ') - waiting..." "Yellow"
        Start-Sleep -Seconds 30
    }
    Invoke-Worker "[pscustomobject]@{ agent = 'worker-sync'; purpose = 'worker_sync.ps1 from $env:COMPUTERNAME'; pid = 0; manual = `$true; started = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss') } | ConvertTo-Json | Set-Content -Encoding UTF8 '$RemoteRoot/Saved/agent.lock'" | Out-Null
}
function Exit-WorkerLock {
    if (-not $script:SyncHeld) { return }
    Invoke-Worker -AllowFail "`$l = '$RemoteRoot/Saved/agent.lock'; if ((Test-Path `$l) -and ((Get-Content `$l -Raw) -match 'worker-sync')) { Remove-Item `$l }" | Out-Null
}
function Exit-WorkerSync {
    if (-not $script:SyncHeld) { return }
    Exit-WorkerLock
    Invoke-Worker -AllowFail @"
Remove-Item '$RemoteRoot/Saved/Worker/SYNC' -ErrorAction SilentlyContinue
`$p = '$RemoteRoot/Saved/Trainer/PAUSE'
if ((Test-Path `$p) -and ((Get-Content `$p -Raw) -match 'worker_sync')) { Remove-Item `$p }
"@ | Out-Null
    $script:SyncHeld = $false
    Say "handshake released (SYNC + trainer PAUSE removed)"
}

try {
    Say "worker $Worker -> $RemoteRoot"
    $probe = @(Invoke-Worker "hostname; Test-Path '$RemoteRoot/CodexTactics.uproject'")
    if ($probe[-1] -ne "True") { throw "no checkout at $RemoteRoot on $Worker (see docs/port/SECOND_MACHINE.md, first setup)" }
    if (-not ($SkipSync -and $SkipBinaries -and $SkipSmoke)) { Enter-WorkerSync }
    $oldHead = @(Invoke-Worker "Set-Location '$RemoteRoot'; git rev-parse --short HEAD")[-1].Trim()

    # ---- 1. code -------------------------------------------------------------------------------------------------------
    if (-not $SkipSync) {
        Say "git: fetch + reset to $Ref + lfs pull"
        $git = Invoke-Worker @"
Set-Location '$RemoteRoot'
git fetch --quiet origin 2>&1 | Out-Null
if (`$LASTEXITCODE -ne 0) { 'FETCH-FAILED'; exit 2 }
git reset --quiet --hard '$Ref' 2>&1 | Out-Null
git lfs pull 2>&1 | Select-Object -Last 1
'HEAD ' + (git rev-parse --short HEAD) + ' ' + (git log -1 --format=%s)
"@
        $git | ForEach-Object { Say "  $_" }
    }
    $workerHead = @(Invoke-Worker "Set-Location '$RemoteRoot'; git rev-parse HEAD")[-1].Trim()

    # ---- 2. binaries ---------------------------------------------------------------------------------------------------
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
        $remoteList = Invoke-Worker @"
`$b = '$RemoteRoot/Binaries/Win64'
if (Test-Path `$b) { Get-ChildItem `$b -Recurse -File | ForEach-Object { `$_.FullName.Substring((Resolve-Path `$b).Path.Length + 1) + '|' + `$_.Length + '|' + [int64](`$_.LastWriteTimeUtc - [DateTime]'1970-01-01').TotalSeconds } }
"@
        $remote = @{}
        foreach ($l in $remoteList) { $p = $l -split '\|'; if ($p.Count -eq 3) { $remote[$p[0]] = "$($p[1])|$($p[2])" } }
        $local = Get-ChildItem $LocalBin -Recurse -File | Where-Object { -not ($NoPdb -and $_.Extension -eq ".pdb") }
        $changed = @($local | Where-Object {
            $rel = $_.FullName.Substring($LocalBin.Length + 1)
            $remote[$rel] -ne "$($_.Length)|$([int64]($_.LastWriteTimeUtc - [DateTime]'1970-01-01').TotalSeconds)"
        })
        $mb = [Math]::Round((($changed | Measure-Object Length -Sum).Sum) / 1MB)
        Say "binaries: $($changed.Count) of $($local.Count) files changed ($mb MB)"
        # A running engine locks the project DLLs ("dest open ... Failure"). Smoke / bot engines finish on their own: wait for them;
        # an open editor (UnrealEditor.exe) is someone working on the worker: stop with a clear message instead of killing it.
        if ($changed.Count -gt 0) {
            $deadline = (Get-Date).AddMinutes($WaitEngineMinutes)
            while ($true) {
                $engines = @(Invoke-Worker "Get-CimInstance Win32_Process -Filter `"Name like 'UnrealEditor%'`" | Where-Object { `$_.CommandLine -match 'CodexTactics' -or `$_.Name -eq 'UnrealEditor.exe' } | ForEach-Object { `$_.Name }")
                if ($engines -contains "UnrealEditor.exe") { throw "the Unreal Editor is open on the worker (locks the DLLs) - close it there, then re-run" }
                if ($engines.Count -eq 0) { break }
                if ((Get-Date) -gt $deadline) { throw "worker engines still running after $WaitEngineMinutes min: $($engines -join ', ')" }
                Say "worker busy ($($engines.Count) engine process(es)); waiting for them before copying binaries..." "Yellow"
                Start-Sleep -Seconds 30
            }
        }
        foreach ($f in $changed) {
            $rel = $f.FullName.Substring($LocalBin.Length + 1)
            $sub = (Split-Path $rel -Parent) -replace '\\', '/'
            $dstDir = $(if ($sub) { "$RemoteRoot/Binaries/Win64/$sub" } else { "$RemoteRoot/Binaries/Win64" })
            Invoke-Worker "New-Item -ItemType Directory -Force '$dstDir' | Out-Null" | Out-Null
            & scp -q -p $f.FullName "${Worker}:$dstDir/"
            if ($LASTEXITCODE -ne 0) { throw "scp failed for $rel" }
        }
    }
    # Code change point for the trainer report: Saved\Worker\sync_points.log (time, old -> new HEAD, binaries, trainer generation).
    if (-not ($SkipSync -and $SkipBinaries)) {
        $binNote = $(if ($SkipBinaries) { "binaries not copied" } else { "binaries $((& git -C $BinariesFrom rev-parse --short HEAD).Trim()), $($changed.Count) files" })
        $point = @(Invoke-Worker @"
`$s = Get-ChildItem '$RemoteRoot/Saved/Trainer/*/status.json' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
`$t = `$(if (`$s) { `$j = Get-Content `$s.FullName -Raw | ConvertFrom-Json; "trainer `$(`$j.run_id) gen `$(`$j.generation) `$(`$j.phase) `$(`$j.jobs_done)/`$(`$j.jobs_total)" } else { 'no trainer run' })
`$line = (Get-Date -Format s) + ' $oldHead -> ' + (git -C '$RemoteRoot' rev-parse --short HEAD) + ' | $binNote | ' + `$t
Add-Content -Encoding UTF8 '$RemoteRoot/Saved/Worker/sync_points.log' `$line
`$line
"@)
        Say "sync point: $($point[-1])"
    }
    Exit-WorkerLock
} catch {
    Say "SYNC ERROR: $_" "Red"
    Exit-WorkerSync
    exit 2
}

# ---- 3. smokes on the worker ---------------------------------------------------------------------------------------------
# (SYNC + trainer PAUSE stay set until the batch ends: the trainer would otherwise take the lock between waves.)
if ($SkipSmoke) { Say "smokes skipped"; Exit-WorkerSync; exit 0 }
$names = ($Commands | ForEach-Object { $_ -split '[,;\s]+' } | Where-Object { $_ }) -join ","
$smokeArgs = "-Commands '$names' -Jobs $Jobs -ExtraArgs '$ExtraArgs' -ResultJson '$RemoteRoot/Saved/Logs/SmokeParallel-worker.json'"
if ($NoRetry) { $smokeArgs += " -NoRetry" }
if ($TimeoutSeconds -gt 0) { $smokeArgs += " -TimeoutSeconds $TimeoutSeconds" }
Say "smokes on worker: $names (jobs $Jobs)"
$run = Invoke-Worker -AllowFail @"
Set-Location '$RemoteRoot'
`$env:CODEX_AGENT = 'worker'
`$env:CODEX_IGNORE_DESIGNER = '1'
Remove-Item 'Saved/Logs/SmokeParallel-worker.json' -ErrorAction SilentlyContinue
& powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 $smokeArgs *>&1 | ForEach-Object { "`$_" }
exit `$LASTEXITCODE
"@
$smokeExit = $script:LastWorkerExit
Exit-WorkerSync
$run | ForEach-Object { Write-Host "  $_" }

# ---- 4. results back ------------------------------------------------------------------------------------------------------
New-Item -ItemType Directory -Force $OutDir | Out-Null
& scp -q "${Worker}:$RemoteRoot/Saved/Logs/SmokeParallel-worker.json" (Join-Path $OutDir "SmokeParallel-worker.json")
$json = Join-Path $OutDir "SmokeParallel-worker.json"
if (Test-Path $json) {
    $res = Get-Content $json -Raw | ConvertFrom-Json
    foreach ($r in @($res.results | Where-Object { $_.State -eq "FAIL" })) {
        $remoteLog = ($r.Log -replace '\\', '/')
        & scp -q "${Worker}:$remoteLog" $OutDir 2>$null
    }
    Say ("results: {0} -> {1}" -f (@($res.results | ForEach-Object { "$($_.Name)=$($_.State)" }) -join ", "), $OutDir) $(if ($smokeExit -eq 0) { "Green" } else { "Red" })
} else {
    Say "no result JSON came back (smoke_parallel exit $smokeExit)" "Red"
    if ($smokeExit -eq 0) { $smokeExit = 2 }
}
exit $smokeExit

