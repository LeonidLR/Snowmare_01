# Night trainer entry point for Windows Task Scheduler (docs/tools/night-trainer.md). Runs (or resumes) one training run
# until the end of the window, then makes the night's highlight clips and the report.
#   powershell -ExecutionPolicy Bypass -File Scripts\Trainer\run_night_trainer.ps1 -RunId pmc-main -UntilTime 08:00
#   powershell -ExecutionPolicy Bypass -File Scripts\Trainer\run_night_trainer.ps1 -RunId pmc-main -UntilDay Monday -UntilTime 07:00
#   ... [-Hours 9.5] [-MaxGenerations 200] [-Parallel 2] [-YieldToEditor] [-Mock]
# The stop time is the NEXT -UntilDay/-UntilTime after now (today if still ahead, otherwise tomorrow / next week).
# Output: Saved\Trainer\<RunId>\ (report.html, champion_weights.json, ...); this wrapper's log: Saved\Trainer\scheduler.log.
param(
    [string]$RunId = "pmc-main",
    [string]$UntilTime = "08:00",
    [string]$UntilDay = "",
    [double]$Hours = 0,
    [int]$MaxGenerations = 0,
    [int]$Parallel = 0,
    [switch]$YieldToEditor,
    [switch]$Mock
)
$ErrorActionPreference = "Stop"
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $env:CODEX_AGENT) { $env:CODEX_AGENT = "night-trainer" }

$Now = Get-Date
$Stop = [datetime]::ParseExact($UntilTime, "HH:mm", $null)
$Stop = $Now.Date.AddHours($Stop.Hour).AddMinutes($Stop.Minute)
if ($UntilDay) {
    $Target = [System.DayOfWeek]$UntilDay
    $Days = ([int]$Target - [int]$Now.DayOfWeek + 7) % 7
    $Stop = $Stop.AddDays($Days)
    if ($Stop -le $Now) { $Stop = $Stop.AddDays(7) }
} elseif ($Stop -le $Now) {
    $Stop = $Stop.AddDays(1)
}

$PyArgs = @((Join-Path $PSScriptRoot "night_trainer.py"), "--run-id", $RunId, "--stop-at", $Stop.ToString("yyyy-MM-ddTHH:mm"))
if ($Hours -gt 0) { $PyArgs += @("--hours", "$Hours") }
if ($MaxGenerations -gt 0) { $PyArgs += @("--max-generations", "$MaxGenerations") }
if ($Parallel -gt 0) { $PyArgs += @("--parallel", "$Parallel") }
if ($YieldToEditor) { $PyArgs += "--yield-to-editor" }
if ($Mock) { $PyArgs += "--mock" }

$LogDir = Join-Path $Repo "Saved\Trainer"
New-Item -ItemType Directory -Force $LogDir | Out-Null
$Log = Join-Path $LogDir "scheduler.log"

# Watchdog rule (2026-11-06): "is the trainer running?" = a live python process with night_trainer.py on its command line,
# NEVER the scheduled task's state (a task can read Ready while its python child still runs - a false restart started a
# duplicate). night_trainer.py's own single-instance lock (Saved\Trainer\night_trainer.lock) backs this up.
$Live = @(Get-CimInstance Win32_Process -Filter "Name like 'python%'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -and $_.CommandLine -like "*night_trainer.py*" -and $_.CommandLine -notlike "*--mock*" })
if ($Live.Count -gt 0) {
    Add-Content -Encoding UTF8 $Log "[$($Now.ToString('s'))] skip start of $RunId - night_trainer.py already running (pid $(($Live | ForEach-Object { $_.ProcessId }) -join ', '))"
    exit 0
}
Add-Content -Encoding UTF8 $Log "[$($Now.ToString('s'))] start run $RunId until $($Stop.ToString('s')): python $($PyArgs -join ' ')"
Push-Location $Repo
$ErrorActionPreference = "Continue" # python's stderr lines must not become terminating errors (PS 5.1)
try {
    & python @PyArgs 2>&1 | ForEach-Object { Add-Content -Encoding UTF8 $Log "$_" }
    $Code = $LASTEXITCODE
} finally {
    Pop-Location
}
Add-Content -Encoding UTF8 $Log "[$((Get-Date).ToString('s'))] end run $RunId (exit $Code)"
exit $Code
