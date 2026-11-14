# Emergency kill switch & stop script for the CodexTactics chat dispatcher.
# Creates Saved/Orchestrator/dispatcher/STOP and stops the daemon PID if running.

param(
    [string]$Reason = "Stopped by dispatcher_stop.ps1"
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir "..\..") | Select-Object -ExpandProperty Path
$StateDir = Join-Path $RepoRoot "Saved\Orchestrator\dispatcher"
New-Item -ItemType Directory -Force -Path $StateDir | Out-Null

$StopFile = Join-Path $StateDir "STOP"
$PidFile = Join-Path $StateDir "dispatcher.pid"

Set-Content -LiteralPath $StopFile -Value $Reason -Encoding utf8
Write-Host "[OK] Kill switch created: $StopFile"

if (Test-Path -LiteralPath $PidFile) {
    $PidText = (Get-Content -LiteralPath $PidFile -ErrorAction SilentlyContinue | Select-Object -First 1)
    if ($PidText -match '^\d+$') {
        $Proc = Get-Process -Id ([int]$PidText) -ErrorAction SilentlyContinue
        if ($Proc) {
            Stop-Process -Id $Proc.Id -Force
            Write-Host "[OK] Stopped dispatcher process (PID: $($Proc.Id))."
        }
    }
    Remove-Item -LiteralPath $PidFile -Force -ErrorAction SilentlyContinue
}
exit 0
