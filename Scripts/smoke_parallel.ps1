# Runs headless in-game checks (dev console commands) side by side, each in its own UnrealEditor-Cmd process with its own
# -abslog (Saved\Logs\Smoke-<Name>.log), and prints a PASS / FAIL table. Exit code 1 when any check fails (a smokeOptions.knownFail
# check is reported but does not fail the run).
# Usage:
#   powershell -ExecutionPolicy Bypass -File Scripts\smoke_parallel.ps1 -Commands CoverSmoke,CoverMoveSmoke,CornerHoldSmoke [-Jobs 3]
#   powershell -ExecutionPolicy Bypass -File Scripts\smoke_parallel.ps1 -Commands all            (the whole smokeRegistry)
#   ... -Serial        one at a time (same code path, Jobs = 1)
# Names may carry the "CodexTactics." prefix or not. Per-check options (exclusive / extra args / knownFail) come from Scripts\test_map.json.
# Shared files: SaveLoadSmoke / PauseMenuSmoke (Saved\SmokeSaves) and EventBusSmoke (Saved\SaveGames slot) are "exclusive": they run alone
# after the parallel batch. Every other check only writes its own log. A check that fails in the parallel batch is re-run alone once
# (load can slow it down; the table marks that as PASS*); -NoRetry disables that.
# Build lock: taken ONCE for the whole batch (children are plain engine processes, they never touch it). If the caller already holds it
# (CODEX_LOCK_HELD=1: verify_all, test.ps1 -Changed) nothing is taken here.
param(
    [string[]]$Commands = @(),
    [int]$Jobs = 0,
    [string]$Map = "/Game/Maps/L_MovementTest",
    [int]$TimeoutSeconds = 300,
    [switch]$Serial,
    [switch]$NoRetry,
    [string]$ResultJson = ""
)

$ErrorActionPreference = "Stop"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$ProjectDir = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"
$LogDir = Join-Path $ProjectDir "Saved\Logs"
. (Join-Path $PSScriptRoot "verify_lib.ps1")
. (Join-Path $PSScriptRoot "agent_lock.ps1")

$TestMap = Get-TestMap
# -File passes "a,b,c" as ONE string: split it ourselves.
$Names = @($Commands | ForEach-Object { $_ -split '[,;\s]+' } | Where-Object { $_ } | ForEach-Object { $_ -replace '^CodexTactics\.', '' })
if ($Names -contains "all") { $Names = @($TestMap.smokeRegistry) }
$Names = @($Names | Select-Object -Unique)
if ($Names.Count -eq 0) { Write-Host "No smokes given (-Commands a,b,c | all)."; exit 0 }
if ($Serial) { $Jobs = 1 }
elseif ($Jobs -le 0) { $Jobs = [Math]::Max(1, [Math]::Min(4, [int]([Environment]::ProcessorCount / 4))) }

function Get-Option([string]$Name, [string]$Key, $Default) {
    $o = $TestMap.smokeOptions.PSObject.Properties[$Name]
    if ($o -and $o.Value.PSObject.Properties[$Key]) { return $o.Value.$Key }
    return $Default
}

function Start-SmokeProcess([string]$Name, [string]$Suffix) {
    $log = Join-Path $LogDir "Smoke-$Name$Suffix.log"
    Remove-Item $log -ErrorAction SilentlyContinue # a stale log must never pass for this run's result
    $extra = Get-Option $Name "extra" ""
    $map = Get-Option $Name "map" $Map
    # Start-Process takes one argument string: quote the paths (the project path has a space).
    $gameArgs = "`"$Project`" $map -game -nullrhi -nosplash -nosound -unattended -windowed -FORCELOGFLUSH -NoTelemetry `"-ExecCmds=CodexTactics.$Name`" `"-abslog=$log`" $extra"
    $proc = Start-Process "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -ArgumentList $gameArgs -WindowStyle Hidden -PassThru
    return [pscustomobject]@{ Name = $Name; Suffix = $Suffix; Proc = $proc; Log = $log; Started = Get-Date; Timeout = [int](Get-Option $Name "timeout" $TimeoutSeconds) }
}

function Get-Verdict($Job) {
    return ((Test-Path $Job.Log) -and (Select-String -Path $Job.Log -Pattern "Smoke RESULT: PASS" -Quiet))
}

# Runs the names with at most $Width engine processes at a time; returns {Name, Pass, Seconds, TimedOut, Log}.
function Invoke-Batch([string[]]$Batch, [int]$Width, [string]$Suffix) {
    $queue = [System.Collections.Generic.Queue[string]]::new()
    $Batch | ForEach-Object { $queue.Enqueue($_) }
    $running = @()
    $done = @()
    while ($queue.Count -gt 0 -or $running.Count -gt 0) {
        while ($running.Count -lt $Width -and $queue.Count -gt 0) { $running += Start-SmokeProcess $queue.Dequeue() $Suffix }
        Start-Sleep -Milliseconds 400
        foreach ($job in @($running)) {
            $elapsed = ((Get-Date) - $job.Started).TotalSeconds
            $timedOut = $false
            if (-not $job.Proc.HasExited -and $elapsed -gt $job.Timeout) {
                Stop-Process -Id $job.Proc.Id -Force -ErrorAction SilentlyContinue
                $timedOut = $true
            }
            if ($job.Proc.HasExited -or $timedOut) {
                $job.Proc.WaitForExit()
                $running = @($running | Where-Object { $_ -ne $job })
                $r = [pscustomobject]@{ Name = $job.Name; Pass = (Get-Verdict $job); Seconds = [Math]::Round($elapsed, 1); TimedOut = $timedOut; Log = $job.Log }
                Write-Host ("  {0,-26} {1,-7} {2,6:N1}s{3}" -f $r.Name, $(if ($r.Pass) { "PASS" } else { "FAIL" }), $r.Seconds, $(if ($timedOut) { "  (TIMEOUT)" } else { "" }))
                $done += $r
            }
        }
    }
    return $done
}

$Owned = Enter-AgentLock "smokes x$($Names.Count) (jobs $Jobs)"
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$final = @()
try {
    $exclusive = @($Names | Where-Object { Get-Option $_ "exclusive" $false })
    $shared = @($Names | Where-Object { $_ -notin $exclusive })
    Write-Host ("[smoke_parallel] {0} smokes, {1} at a time{2}" -f $Names.Count, $Jobs, $(if ($exclusive.Count) { ", exclusive alone afterwards: " + ($exclusive -join ", ") } else { "" }))
    $results = @()
    if ($shared.Count) { $results += Invoke-Batch $shared $Jobs "" }
    $retried = @{}
    if (-not $NoRetry -and $Jobs -gt 1) {
        $failed = @($results | Where-Object { -not $_.Pass } | ForEach-Object Name)
        if ($failed.Count) {
            Write-Host "[smoke_parallel] re-running alone: $($failed -join ', ')"
            $again = Invoke-Batch $failed 1 "-alone"
            foreach ($a in $again) { $retried[$a.Name] = $a }
        }
    }
    if ($exclusive.Count) { $results += Invoke-Batch $exclusive 1 "" }
    foreach ($r in $results) {
        $state = $(if ($r.Pass) { "PASS" } else { "FAIL" })
        $secs = $r.Seconds
        $log = $r.Log
        if ($retried.ContainsKey($r.Name)) {
            $a = $retried[$r.Name]
            $secs = [Math]::Round($secs + $a.Seconds, 1)
            $log = $a.Log
            $state = $(if ($a.Pass) { "PASS*" } else { "FAIL" })
        }
        if ($state -eq "FAIL" -and (Get-Option $r.Name "knownFail" $false)) { $state = "KNOWN-FAIL" }
        $final += [pscustomobject]@{ Name = $r.Name; State = $state; Seconds = $secs; Log = $log }
    }
} finally {
    if ($Owned) { Exit-AgentLock }
}
$sw.Stop()

Write-Host ""
Write-Host ("{0,-26} {1,-11} {2,8}" -f "Smoke", "Result", "Seconds")
foreach ($f in ($final | Sort-Object Name)) { Write-Host ("{0,-26} {1,-11} {2,8:N1}" -f $f.Name, $f.State, $f.Seconds) }
$bad = @($final | Where-Object { $_.State -eq "FAIL" })
$flaky = @($final | Where-Object { $_.State -eq "PASS*" })
$known = @($final | Where-Object { $_.State -eq "KNOWN-FAIL" })
Write-Host ("[smoke_parallel] {0} run, {1} failed, {2} passed only on the re-run (PASS*), {3} known-fail; wall {4:N0} s, sum of checks {5:N0} s" -f
    $final.Count, $bad.Count, $flaky.Count, $known.Count, $sw.Elapsed.TotalSeconds, ($final | Measure-Object Seconds -Sum).Sum)
if (-not $ResultJson) { $ResultJson = Join-Path $LogDir "SmokeParallel-last.json" }
@{ jobs = $Jobs; wallSeconds = [Math]::Round($sw.Elapsed.TotalSeconds, 1); results = $final } | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $ResultJson
if ($bad.Count -gt 0) {
    Write-Host ("FAILED: {0}  (logs: Saved\Logs\Smoke-<Name>[-alone].log)" -f (($bad | ForEach-Object Name) -join ", ")) -ForegroundColor Red
    exit 1
}
exit 0
