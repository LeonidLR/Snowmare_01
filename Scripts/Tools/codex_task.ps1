# Runs one Codex task headless (codex exec) in an agent lane, started by the Lead (designer OK 2026-11-27: full access, no sandbox -
# the Codex sandbox blocks .git and the worker SSH). The task file is fed on stdin (without it codex waits on stdin forever).
#   powershell -ExecutionPolicy Bypass -File Scripts/Tools/codex_task.ps1 -Lane C:/Work/Lanes/CodexTactics-147 -PromptFile docs/port/codex/inbox/147-x.md -Label 147
# Posts START / heartbeat / ENDED to AgentChat (topic codex-<Label>) so the task board shows the run as in progress.
param(
    [Parameter(Mandatory)][string]$Lane,
    [Parameter(Mandatory)][string]$PromptFile,
    [string]$Label = "codex",
    [int]$TimeoutMinutes = 240
)
$ErrorActionPreference = "Stop"
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not (Test-Path $Lane)) { throw "Lane $Lane is missing" }
$PromptPath = (Resolve-Path $PromptFile).Path
$Codex = Join-Path $env:LOCALAPPDATA "Programs\OpenAI\Codex\bin\codex.exe"
if (-not (Test-Path $Codex)) { $Codex = (Get-Command codex -ErrorAction Stop).Source }
. (Join-Path $PSScriptRoot "agent_run_common.ps1")

$RunDir = Join-Path $Repo "Saved\Orchestrator\codex_runs"
New-Item -ItemType Directory -Force $RunDir | Out-Null
$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$Log = Join-Path $RunDir "$Stamp-$Label.log"
$Last = Join-Path $RunDir "$Stamp-$Label.last.txt"
$Topic = "codex-$Label"
$LaneW = (Resolve-Path $Lane).Path
$CodexArgs = @("exec", "--skip-git-repo-check", "--dangerously-bypass-approvals-and-sandbox", "-C", "`"$LaneW`"", "-o", "`"$Last`"", "-")

Write-Host "[codex_task] $Label in $Lane, log $Log"
Send-AgentChat $Repo "codex" $Topic "$Label START | headless codex exec in $(Split-Path $Lane -Leaf), started by the Lead"
$Proc = Start-Process -FilePath $Codex -ArgumentList $CodexArgs -WorkingDirectory $LaneW -NoNewWindow -PassThru `
    -RedirectStandardInput $PromptPath -RedirectStandardOutput $Log -RedirectStandardError "$Log.stderr"
$null = $Proc.Handle  # cache the handle, else ExitCode stays empty for Start-Process -PassThru
$Beat = Start-AgentHeartbeat $Repo "codex" $Topic $Label $Proc.Id
if (-not $Proc.WaitForExit($TimeoutMinutes * 60 * 1000)) {
    Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue
    Write-Host "[codex_task] $Label TIMEOUT after $TimeoutMinutes min - killed"
}
$Exit = $Proc.ExitCode
Stop-Job $Beat -ErrorAction SilentlyContinue; Remove-Job $Beat -Force -ErrorAction SilentlyContinue
if (Test-Path "$Log.stderr") { Get-Content "$Log.stderr" | Add-Content -Encoding utf8 $Log; Remove-Item "$Log.stderr" -ErrorAction SilentlyContinue }
$Limit = Select-String -Path $Log -Pattern '^ERROR: You.ve hit your usage limit' -Quiet  # anchored: the agent may print this script
Write-Host "[codex_task] $Label exit $Exit$(if ($Limit) { ' - USAGE LIMIT' })"
Send-AgentChat $Repo "codex" $Topic "$Label ENDED | headless codex exit $Exit$(if ($Limit) { ' - USAGE LIMIT, relaunch after reset' }) | last message Saved/Orchestrator/codex_runs/$(Split-Path $Last -Leaf)" -Status
exit $Exit
