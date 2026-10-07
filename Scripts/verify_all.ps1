# Full verification: build, all automation tests, every headless in-game check (the smokeRegistry in Scripts\test_map.json).
# The checks run a few at a time via smoke_parallel.ps1 (-Parallel N, default min(4, cores/4); each writes Saved\Logs\Smoke-<Name>.log);
# the ones sharing files alone afterwards, a check that fails in the parallel batch is re-run alone once. -Serial = one at a time.
# The Unreal Editor must be closed for the build (it locks the module DLLs); -SkipBuild runs alongside it.
# Usage: powershell -ExecutionPolicy Bypass -File Scripts\verify_all.ps1 [-SkipBuild] [-Parallel 3] [-Serial] [-SkipTests]
param([switch]$SkipBuild, [int]$Parallel = 0, [switch]$Serial, [switch]$SkipTests)

$ProjectDir = Split-Path $PSScriptRoot -Parent
$Failed = @()
$Timer = [System.Diagnostics.Stopwatch]::StartNew()

# Only an editor of this project locks our DLLs (another project's editor may stay open); -SkipBuild runs alongside it.
if (-not $SkipBuild -and (Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor.exe'" | Where-Object { $_.CommandLine -like "*CodexTactics.uproject*" })) {
    Write-Host "Unreal Editor is running: close it first (it locks the DLLs)." -ForegroundColor Yellow
    exit 2
}

# The shared build lock for the whole run (build, tests, every smoke); the child scripts inherit it.
. (Join-Path $PSScriptRoot "agent_lock.ps1")
$OwnedLock = Enter-AgentLock "verify_all"
Register-EngineEvent PowerShell.Exiting -Action { if ($OwnedLock) { Exit-AgentLock } } | Out-Null

if (-not $SkipBuild) {
    powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") | Select-Object -Last 2
    if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED" -ForegroundColor Red; if ($OwnedLock) { Exit-AgentLock }; exit 1 }
}

if (-not $SkipTests) {
    $TestSummary = powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "test.ps1") | Select-Object -Last 1
    $TestExit = $LASTEXITCODE
    Write-Host "Tests: $TestSummary"
    # A non-zero exit also catches an engine crash in the middle of the run (the summary then counts only the tests run).
    if ($TestSummary -notmatch " 0 failed" -or $TestExit -ne 0) { $Failed += "tests (exit $TestExit)" }
}
$TestSeconds = $Timer.Elapsed.TotalSeconds

$SmokeArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "smoke_parallel.ps1"), "-Commands", "all")
if ($Serial) { $SmokeArgs += "-Serial" } elseif ($Parallel -gt 0) { $SmokeArgs += @("-Jobs", $Parallel) }
powershell @SmokeArgs
if ($LASTEXITCODE -ne 0) { $Failed += "smokes (see the FAILED line above)" }

Write-Host ("verify_all: tests done at {0:N0} s, total {1:N0} s ({2})" -f $TestSeconds, $Timer.Elapsed.TotalSeconds, $(if ($Serial) { "serial smokes" } else { "parallel smokes" }))
if ($Failed.Count -gt 0) {
    Write-Host "FAILED: $($Failed -join ', ')  (details: Saved\Logs\Smoke-<Name>.log, Saved\TestReport)" -ForegroundColor Red
    if ($OwnedLock) { Exit-AgentLock }
    exit 1
}
Write-Host "ALL GREEN" -ForegroundColor Green
if ($OwnedLock) { Exit-AgentLock }
exit 0
