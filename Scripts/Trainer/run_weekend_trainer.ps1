# New isolated night entry point. Does not change the shared night loop or scheduler.
# Lead queues this only after merge/sync and a fresh training gate pass.
param(
    [Parameter(Mandatory=$true)][string]$Dataset,
    [Parameter(Mandatory=$true)][string]$Champion,
    [string]$RunId = ('weekend-' + (Get-Date -Format yyyyMMdd)),
    [ValidateRange(0.1, 24)][double]$TrainingHours = 2,
    [ValidateRange(0.1, 24)][double]$EvaluationHours = 10,
    [string]$Py = 'python'
)
$ErrorActionPreference = 'Stop'
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if ($RunId -notmatch '^[A-Za-z0-9_-]+$') { throw 'RunId must be a simple folder name' }
$Gate = Join-Path $Repo 'Saved/Trainer/gate_pass.json'
if (-not (Test-Path -LiteralPath $Gate)) { throw 'Run night_trainer.py --check-gate first (existing trainer gate requirement)' }
$GateDoc = Get-Content -LiteralPath $Gate -Raw -Encoding UTF8 | ConvertFrom-Json
if ($GateDoc.passed -ne $true -or ((Get-Date) - (Get-Item -LiteralPath $Gate).LastWriteTime).TotalHours -gt 24) {
    throw 'Training gate is blocked or older than 24h; refresh before the weekend run'
}
$Prepared = Join-Path $Repo "Saved/Trainer/$RunId-input/trainer_config.json"
Push-Location $Repo
try {
    # Keep an existing preparation immutable on resume; night_trainer also snapshots run.json.
    if (-not (Test-Path -LiteralPath $Prepared)) {
        & $Py Scripts/Trainer/prepare_demo_trainer.py $Dataset --champion $Champion --weekend Scripts/Trainer/weekend_trainer.json --out $Prepared
        if ($LASTEXITCODE -ne 0) { throw 'Demo trainer preparation failed' }
    }
    & $Py Scripts/Trainer/night_trainer.py --config $Prepared --run-id "$RunId-learn" --hours $TrainingHours --parallel 1 --yield-to-editor
    if ($LASTEXITCODE -ne 0) { throw 'Training failed; no evaluation launched' }
    $Candidate = Join-Path $Repo "Saved/Trainer/$RunId-learn/champion_weights.json"
    $State = Join-Path $Repo "Saved/Trainer/$RunId-learn/state.json"
    if (-not (Test-Path -LiteralPath $Candidate) -or (Get-Content -LiteralPath $State -Raw | ConvertFrom-Json).generation_done -lt 0) {
        throw 'No completed training generation; inspect the learning report'
    }
    $FrozenChampion = Join-Path (Split-Path $Prepared -Parent) 'approved_champion.json'
    & $Py Scripts/Trainer/weekend_trainer.py --trainer-config $Prepared --champion $FrozenChampion --candidate $Candidate --run-id "$RunId-ab" --hours $EvaluationHours
    if ($LASTEXITCODE -ne 0) { throw 'Paired evaluation incomplete; inspect morning.md and resume this run' }
    Write-Host "Morning report: Saved/Trainer/$RunId-ab/morning.md. Lead approval is required to apply any champion."
} finally { Pop-Location }
