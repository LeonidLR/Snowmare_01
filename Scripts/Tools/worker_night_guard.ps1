# codex-worker night guard (designer 2026-11-19: no silent stops). Task "CodexTactics Night Guard" runs it every 10 min.
# Installed OUTSIDE the repo (C:\Work\NightLoop\ holds copies of this file + worker_night_loop.ps1): day callers switch the
# worker checkout to branches that may not have these scripts.
# Inside the night window (-From .. -To, worker clock) and without Saved\Worker\STOP_NIGHTLOOP it makes sure exactly one
# Scripts\Tools\worker_night_loop.ps1 runs: none running -> start it; running but heartbeat older than -StaleMin -> kill its
# process tree and start a new one. Every decision goes to Saved\Worker\nightloop\guard.log (the lead / stand-in lead reads it).
param([string]$Repo = 'C:\Work\CodexTactics', [string]$From = '16:55', [string]$To = '07:45', [int]$StaleMin = 50)
$ErrorActionPreference = 'Continue'
$root = Join-Path $Repo 'Saved\Worker\nightloop'; New-Item -ItemType Directory -Force $root | Out-Null
$log = Join-Path $root 'guard.log'
function G([string]$m) { Add-Content -Encoding UTF8 $log ('{0} {1}' -f (Get-Date -Format s), $m) }
$now = (Get-Date).TimeOfDay
$f = [datetime]::ParseExact($From, 'HH:mm', $null).TimeOfDay; $t = [datetime]::ParseExact($To, 'HH:mm', $null).TimeOfDay
$inWindow = if ($f -gt $t) { $now -ge $f -or $now -lt $t } else { $now -ge $f -and $now -lt $t }
if (-not $inWindow) { exit 0 }
if (Test-Path (Join-Path $Repo 'Saved\Worker\STOP_NIGHTLOOP')) { exit 0 }
$procs = @(Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" | Where-Object { "$($_.CommandLine)" -match 'worker_night_loop\.ps1' })
$heart = Join-Path $root 'heartbeat.txt'
$age = if (Test-Path $heart) { ((Get-Date) - (Get-Item $heart).LastWriteTime).TotalMinutes } else { 9999 }
function Start-Loop([string]$why) {
    Start-Process powershell.exe -WindowStyle Hidden -WorkingDirectory $Repo -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'worker_night_loop.ps1'), '-Repo', $Repo)
    G "started the night loop ($why)"
}
function Kill-Tree([int]$id) {
    Get-CimInstance Win32_Process -Filter "ParentProcessId=$id" | ForEach-Object { Kill-Tree $_.ProcessId }
    Stop-Process -Id $id -Force -ErrorAction SilentlyContinue
}
if ($procs.Count -eq 0) { Start-Loop 'none running'; exit 0 }
if ($age -gt $StaleMin) {
    foreach ($p in $procs) {
        Kill-Tree $p.ProcessId
        & taskkill.exe /T /F /PID $($p.ProcessId) *> $null
    }
    $l = Join-Path $Repo 'Saved\agent.lock'
    if ((Test-Path $l) -and ((Get-Content $l -Raw) -match 'night-loop|night-trainer')) { Remove-Item $l -Force }
    G ("heartbeat {0:N0} min old - killed {1} loop process(es)" -f $age, $procs.Count)
    Start-Loop 'restart after a stale heartbeat'
    exit 0
}
if ($procs.Count -gt 1) {
    $procs | Sort-Object CreationDate | Select-Object -Skip 1 | ForEach-Object { Kill-Tree $_.ProcessId }
    G "killed $($procs.Count - 1) duplicate loop(s)"
}
