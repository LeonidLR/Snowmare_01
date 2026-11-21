# Test farm (2026-11-17, docs/port/SECOND_MACHINE.md "Test farm"): runs a smoke_parallel batch of THIS checkout on the second
# machine codex-worker during the farm window (default 08:00-16:30) instead of on the main PC. Called by Scripts\smoke_parallel.ps1
# (-Remote auto | on; env CODEX_SMOKE_REMOTE overrides), not by hand. Steps: worker_sync.ps1 -ShipHead (the worker gets this
# checkout's HEAD - unpushed commits as a git bundle - and this checkout's Binaries\Win64 without PDB; the worker never compiles),
# smokes there with -Multiprocess, then every smoke log comes back to Saved\Logs\Smoke-<Name>[-alone].log here and the result JSON
# to -ResultJson (default Saved\Logs\SmokeParallel-last.json) with local log paths, so callers see what a local run leaves.
# Exit code: smoke_parallel's (0 pass / 1 fail), or 99 = "run it locally" (mode auto and: outside the window, on the worker,
# uncommitted game files here, worker unreachable or busy for -WaitMinutes, sync error). Mode on never returns 99 (sync error = 2).
# No console windows: everything runs in the caller's console (ssh / scp / git are started in-process).
param(
    [ValidateSet("auto", "on")][string]$Mode = "auto",
    [string[]]$Commands = @(),
    [int]$Jobs = 0,
    [string]$Map = "/Game/Maps/L_MovementTest",
    [int]$TimeoutSeconds = 300,
    [switch]$Serial,
    [switch]$NoRetry,
    [switch]$RealTime,
    [string]$ResultJson = "",
    [string]$PreCmds = "",
    [string]$ExtraArgs = "",
    [string]$Worker = "codex-worker",
    [string]$WindowFrom = "08:00",
    [string]$WindowTo = "16:30",
    [int]$WaitMinutes = 20,
    [int]$MaxJobs = 2,                      # i7-6700K
    [int]$Repeat = 1,
    [switch]$RepeatSeeds,
    [string]$SmokeArgs = ""
)
$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$LogDir = Join-Path $ProjectDir "Saved\Logs"
function Say([string]$m, [string]$c = "Gray") { Write-Host "[smoke_remote] $m" -ForegroundColor $c }
function Local([string]$why) { if ($Mode -eq "on") { Say "cannot run on the worker: $why" "Red"; exit 2 }; Say "local run: $why"; exit 99 }

# ---- may this batch go to the worker? ------------------------------------------------------------------------------------
if ($env:COMPUTERNAME -eq "CODEX-WORKER" -or $env:CODEX_AGENT -eq "worker") { exit 99 }  # already on the worker: always local
# One-off farm windows (e.g. the 2026-11-17 19:00-24:00 build-farm trial): %LOCALAPPDATA%\CodexTactics\farm_windows.json
# [{ "from": "2026-11-17T19:00", "to": "2026-11-18T00:00" }, ...] - auto also sends batches to the worker inside them.
$extraWindow = $false
try {
    $fw = Join-Path $env:LOCALAPPDATA "CodexTactics\farm_windows.json"
    if (Test-Path $fw) { foreach ($w in @(Get-Content $fw -Raw | ConvertFrom-Json)) { if ((Get-Date) -ge [datetime]$w.from -and (Get-Date) -lt [datetime]$w.to) { $extraWindow = $true } } }
} catch { }
if ($Mode -eq "auto" -and -not $extraWindow) {
    $now = Get-Date
    $from = $now.Date + [TimeSpan]::Parse($WindowFrom); $to = $now.Date + [TimeSpan]::Parse($WindowTo)
    if ($now -lt $from -or $now -ge $to) { exit 99 }                                         # trainer time: quietly local
}
& ssh -o ConnectTimeout=6 -o BatchMode=yes $Worker "exit 0" 2>$null
if ($LASTEXITCODE -ne 0) { Local "$Worker unreachable" }
# Same build: right after build.ps1 -Remote worker the worker's tree + binaries ARE this checkout's (uncommitted work included):
# run the smokes there without any sync. Otherwise the worker gets HEAD + this checkout's DLLs.
$myToken = @(& (Join-Path $PSScriptRoot "tree_token.ps1") -ProjectDir $ProjectDir)[-1]
$tree = $null
try { $tree = (& ssh -o ConnectTimeout=6 -o BatchMode=yes $Worker "Get-Content C:/Work/CodexTactics/Saved/Worker/tree.json -Raw" 2>$null | Out-String) | ConvertFrom-Json } catch { }
$sameBuild = $tree -and $tree.token -eq $myToken -and $tree.binaries -eq "built-on-worker"
if (-not $sameBuild) {
    if (-not (Test-Path (Join-Path $ProjectDir "Binaries\Win64\UnrealEditor.modules"))) { Local "no Binaries\Win64 here" }
    # The worker gets HEAD: uncommitted changes to game files would be missing there (Source too: the binaries were built from them).
    $dirty = @(& git -C $ProjectDir status --porcelain --untracked-files=no -- Source Content Config Plugins Scripts/test_map.json 2>$null)
    if ($dirty.Count) { Local "uncommitted game files here ($($dirty.Count), e.g. $($dirty[0].Trim())) - commit them or build with build.ps1 -Remote worker" }
}

# ---- run on the worker -----------------------------------------------------------------------------------------------------
$width = $(if ($Serial) { 1 } elseif ($Jobs -gt 0) { [Math]::Min($Jobs, $MaxJobs) } else { $MaxJobs })
$extra = @()
if ($Map -ne "/Game/Maps/L_MovementTest") { $extra += "-Map '$Map'" }
if ($PreCmds) { $extra += "-PreCmds '$($PreCmds -replace "'", "''")'" }
if ($RealTime) { $extra += "-RealTime" }
if ($SmokeArgs) { $extra += "-SmokeArgs '$($SmokeArgs -replace "'", "''")'" }
$engineArgs = (@($ExtraArgs -split '\s+' | Where-Object { $_ }) + "-Multiprocess" | Select-Object -Unique) -join " "
$resultDir = Join-Path $LogDir ("Worker\farm-{0}-{1}" -f (Get-Date -Format "yyyyMMdd-HHmmss"), $PID)
Say "batch -> $Worker (jobs $width): $($Commands -join ',')"
$sync = Join-Path $PSScriptRoot "worker_sync.ps1"
$syncMode = $(if ($sameBuild) { @{ SkipSync = $true; SkipBinaries = $true } } else { @{ ShipHead = $true } })
if ($sameBuild) { Say "worker already holds this checkout's build ($($myToken.Substring(0, 8))...): smokes only, no sync" }
& $sync @syncMode -Worker $Worker -BinariesFrom $ProjectDir -Commands $Commands -Jobs $width -TimeoutSeconds $TimeoutSeconds `
    -ExtraArgs $engineArgs -SmokeExtra ($extra -join " ") -ResultDir $resultDir -AllLogs -NoRetry:$NoRetry -WaitEngineMinutes $WaitMinutes `
    -Repeat $Repeat -RepeatSeeds:$RepeatSeeds
$code = $LASTEXITCODE
$json = Join-Path $resultDir "SmokeParallel-worker.json"
if ($code -eq 2 -or -not (Test-Path $json)) { Local "worker run failed (worker_sync exit $code, $resultDir)" }

# ---- results where a local run leaves them -----------------------------------------------------------------------------------
$res = Get-Content $json -Raw | ConvertFrom-Json
foreach ($r in @($res.results)) {
    $name = Split-Path ($r.Log -replace '/', '\') -Leaf
    $src = Join-Path $resultDir $name
    $dst = Join-Path $LogDir $name
    if (Test-Path $src) { Copy-Item -Force $src $dst }
    $r.Log = $dst
}
$res | Add-Member -Force NoteProperty worker ([ordered]@{ host = $Worker; head = (& git -C $ProjectDir rev-parse --short HEAD).Trim(); results = $resultDir })
if (-not $ResultJson) { $ResultJson = Join-Path $LogDir "SmokeParallel-last.json" }
$res | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 $ResultJson
Say ("worker results: {0} smokes, {1} FAIL; logs in Saved\Logs, json {2}" -f @($res.results).Count, @($res.results | Where-Object { $_.State -eq "FAIL" }).Count, $ResultJson) $(if ($code -eq 0) { "Green" } else { "Red" })
exit $code
