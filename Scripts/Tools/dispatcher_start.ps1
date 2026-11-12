# Starts the CodexTactics chat dispatcher daemon in the background.
# Logs -> Saved/Orchestrator/dispatcher/daemon.log
# Kill switch: create Saved/Orchestrator/dispatcher/STOP or run dispatcher_stop.ps1.

param(
    [string]$ConfigPath = "",
    [switch]$ClearStop,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir "..\..") | Select-Object -ExpandProperty Path
$DispatcherPy = Join-Path $ScriptDir "chat_dispatcher.py"
if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
    $ConfigPath = Join-Path $ScriptDir "chat_dispatcher.json"
}

$StateDir = Join-Path $RepoRoot "Saved\Orchestrator\dispatcher"
New-Item -ItemType Directory -Force -Path $StateDir | Out-Null
$StopFile = Join-Path $StateDir "STOP"
$PidFile = Join-Path $StateDir "dispatcher.pid"
$OutLog = Join-Path $StateDir "daemon.out.log"
$ErrLog = Join-Path $StateDir "daemon.err.log"

if ($ClearStop -and (Test-Path -LiteralPath $StopFile)) {
    Remove-Item -LiteralPath $StopFile -Force
    Write-Host "[OK] Removed kill switch: $StopFile"
}
if (Test-Path -LiteralPath $StopFile) {
    Write-Host "[WARN] Kill switch exists ($StopFile). Remove it or pass -ClearStop before starting."
    exit 1
}

if (Test-Path -LiteralPath $PidFile) {
    $OldPidText = (Get-Content -LiteralPath $PidFile -ErrorAction SilentlyContinue | Select-Object -First 1)
    if ($OldPidText -match '^\d+$') {
        $Existing = Get-Process -Id ([int]$OldPidText) -ErrorAction SilentlyContinue
        if ($Existing) {
            Write-Host "[SKIP] Chat dispatcher is already running (PID: $($Existing.Id))."
            exit 0
        }
    }
}

$ArgList = @("`"$DispatcherPy`"", "--config", "`"$ConfigPath`"", "--state-dir", "`"$StateDir`"")
if ($DryRun) {
    $ArgList += "--dry-run"
}

$Proc = Start-Process -FilePath "python" `
    -ArgumentList $ArgList `
    -WorkingDirectory $RepoRoot `
    -RedirectStandardOutput $OutLog `
    -RedirectStandardError $ErrLog `
    -WindowStyle Hidden `
    -PassThru

Set-Content -LiteralPath $PidFile -Value $Proc.Id -Encoding ascii
Write-Host "[OK] Chat dispatcher started (PID: $($Proc.Id)). Log: $OutLog"
exit 0
