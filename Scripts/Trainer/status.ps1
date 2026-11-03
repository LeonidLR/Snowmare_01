# Night trainer status / control (docs/tools/night-trainer.md).
#   powershell -ExecutionPolicy Bypass -File Scripts\Trainer\status.ps1 [-RunId pmc-main]     current run, generation, best, ETA
#   ... -Pause | -Resume      PAUSE file: the trainer waits before its next wave of games (running games finish)
#   ... -Stop                 STOP file: the trainer stops cleanly after the running games (resumable); -Resume clears it too
param(
    [string]$RunId = "",
    [switch]$Pause,
    [switch]$Resume,
    [switch]$Stop
)
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Root = Join-Path $Repo "Saved\Trainer"
if (-not $RunId) {
    $Latest = Join-Path $Root "latest.json"
    if (-not (Test-Path $Latest)) { Write-Host "No trainer run yet ($Root)."; exit 1 }
    $RunId = (Get-Content $Latest -Raw | ConvertFrom-Json).run_id
}
$RunDir = Join-Path $Root $RunId
if (-not (Test-Path $RunDir)) { Write-Host "No run '$RunId' in $Root."; exit 1 }
if ($Pause) { New-Item -ItemType File -Force (Join-Path $RunDir "PAUSE") | Out-Null; Write-Host "Paused $RunId (PAUSE file)." }
if ($Stop) { New-Item -ItemType File -Force (Join-Path $RunDir "STOP") | Out-Null; Write-Host "Stop requested for $RunId (STOP file)." }
if ($Resume) {
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $RunDir "PAUSE"), (Join-Path $RunDir "STOP"), (Join-Path $Root "PAUSE")
    Write-Host "Resumed $RunId (PAUSE / STOP removed; a stopped run continues at its next start)."
}

$S = Get-Content (Join-Path $RunDir "status.json") -Raw -ErrorAction SilentlyContinue | ConvertFrom-Json
if (-not $S) { Write-Host "Run $RunId has no status yet."; exit 0 }
$Alive = [bool](Get-Process -Id $S.pid -ErrorAction SilentlyContinue)
$State = if ($S.state -in @("running", "paused", "waiting-lock", "waiting-editor", "clips") -and -not $Alive) { "$($S.state) (process gone: crashed or killed - resumable)" } else { $S.state }
Write-Host "Run        : $RunId $(if ($S.mock) { '(MOCK)' })"
Write-Host "State      : $State $(if ($S.note) { "- $($S.note)" })"
Write-Host "Generation : $($S.generation) (done: $($S.generation_done), max $($S.max_generations)); phase $($S.phase) jobs $($S.jobs_done)/$($S.jobs_total)"
Write-Host "Best       : $($S.best_fitness)   champion: $($S.champion)"
if ($S.eta) { Write-Host "ETA        : ~$($S.eta.avg_generation_s) s / generation, $($S.eta.generations_left) left, finish by $($S.eta.finish_by)" }
if ($S.deadline) { Write-Host "Stop at    : $($S.deadline)" }
Write-Host "Updated    : $($S.updated)"
Write-Host "Report     : $(Join-Path $RunDir 'report.html')"
foreach ($Flag in "PAUSE", "STOP") { if (Test-Path (Join-Path $RunDir $Flag)) { Write-Host "$Flag file present." -ForegroundColor Yellow } }
$Task = Get-ScheduledTask -TaskName "CodexTactics Night Trainer" -ErrorAction SilentlyContinue
if ($Task) { Write-Host "Scheduler  : $($Task.State), next run $((Get-ScheduledTaskInfo -TaskName $Task.TaskName).NextRunTime)" }
