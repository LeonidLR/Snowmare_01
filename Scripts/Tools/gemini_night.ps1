# Runs a queue of Gemini CLI tasks one after another in the CodexTactics-gemcli worktree (2026-11-16, replaces Antigravity at night).
#   powershell -ExecutionPolicy Bypass -File Scripts/Tools/gemini_night.ps1 -Tasks 071,074,075 [-Model <id>] [-MaxMinutes 150]
# Each task = docs/port/gemini/CLI_RULES.md + the task file docs/port/gemini/inbox/<NNN>-*.md, run through gemini_agent.ps1 in yolo mode
# (edits + commands, only inside the worktree; the pre-push guard keeps it off main). Logs: Saved/Orchestrator/gemini_runs/.
# After each task one chat line goes to Claude; the run stops early if the task log shows a quota / auth error.
param(
    [Parameter(Mandatory)][string[]]$Tasks,
    [string]$Model = "",
    [int]$MaxMinutes = 150
)
$ErrorActionPreference = "Stop"
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Tree = Join-Path (Split-Path $Repo -Parent) "CodexTactics-gemcli"
if (-not (Test-Path $Tree)) { throw "Worktree $Tree is missing - create it: Scripts/Tools/new_agent_worktree.ps1 -Name gemcli -Branch gemini/cli-base" }
$Rules = Get-Content -Raw -Encoding utf8 (Join-Path $Repo "docs\port\gemini\CLI_RULES.md")
$Chat = Join-Path $Repo "Scripts\Tools\agent_chat.py"
$env:PYTHONIOENCODING = "utf-8"
$RunDir = Join-Path $Repo "Saved\Orchestrator\gemini_runs"
New-Item -ItemType Directory -Force $RunDir | Out-Null

foreach ($Raw in ($Tasks -join ",").Split(",")) {
    $Id = $Raw.Trim()
    if (-not $Id) { continue }
    $TaskFile = Get-ChildItem (Join-Path $Repo "docs\port\gemini\inbox") -Filter "$Id-*.md" | Select-Object -First 1
    if (-not $TaskFile) { Write-Host "[gemini-night] no task file for $Id - skipped"; continue }
    $Prompt = Join-Path $RunDir "prompt-$Id.md"
    $Body = $Rules + "`n`n# YOUR TASK $Id (file docs/port/gemini/inbox/$($TaskFile.Name), read it in full)`n`n" +
        (Get-Content -Raw -Encoding utf8 $TaskFile.FullName) +
        "`n`nWork through the whole task now, following every rule above. Hard time limit: $MaxMinutes minutes - then commit WIP and report."
    Set-Content -Encoding utf8 $Prompt $Body
    $Started = Get-Date
    Write-Host "[gemini-night] $Id started $($Started.ToString('HH:mm'))"
    $Job = Start-Job -ScriptBlock {
        param($Script, $Tree, $Prompt, $Model, $Id)
        $a = @{ Worktree = $Tree; PromptFile = $Prompt; Mode = "yolo"; Label = "night-$Id" }
        if ($Model) { $a.Model = $Model }
        & powershell -ExecutionPolicy Bypass -File $Script @a
    } -ArgumentList (Join-Path $PSScriptRoot "gemini_agent.ps1"), $Tree, $Prompt, $Model, $Id
    if (-not (Wait-Job $Job -Timeout ($MaxMinutes * 60 + 600))) {
        Stop-Job $Job
        Write-Host "[gemini-night] $Id TIMEOUT after $MaxMinutes min"
    }
    Remove-Job $Job -Force
    $Log = Get-ChildItem $RunDir -Filter "*-night-$Id.txt" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $Mins = [int]((Get-Date) - $Started).TotalMinutes
    $Head = (git -C $Tree log --oneline -1 2>$null)
    $Fatal = $Log -and (Select-String -Path $Log.FullName -Pattern 'RESOURCE_EXHAUSTED|quota|API key not valid|PERMISSION_DENIED|IneligibleTier' -Quiet)
    $Text = "gemini-cli $Id finished in $Mins min; worktree HEAD: $Head; log: Saved/Orchestrator/gemini_runs/$($Log.Name)" + $(if ($Fatal) { " - QUOTA/AUTH ERROR, queue stopped" } else { "" })
    & python $Chat post --as gemini --to claude --topic "gemini-cli" --text $Text | Out-Null
    Write-Host "[gemini-night] $Text"
    if ($Fatal) { break }
}
Write-Host "[gemini-night] queue done"
