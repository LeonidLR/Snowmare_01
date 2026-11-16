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
    if ($Id -match '^\d+$') { $Id = $Id.PadLeft(3, "0") }  # "071" arrives as 71 through -Tasks 071,074
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
        # plain argument list: a hashtable splat turns the -Json switch into "-Json:True", which powershell -File rejects
        $a = @("-ExecutionPolicy", "Bypass", "-File", $Script, "-Worktree", $Tree, "-PromptFile", $Prompt, "-Mode", "yolo", "-Label", "night-$Id", "-Json")
        if ($Model) { $a += @("-Model", $Model) }
        & powershell @a 2>&1
    } -ArgumentList (Join-Path $PSScriptRoot "gemini_agent.ps1"), $Tree, $Prompt, $Model, $Id
    if (-not (Wait-Job $Job -Timeout ($MaxMinutes * 60 + 600))) {
        Stop-Job $Job
        Write-Host "[gemini-night] $Id TIMEOUT after $MaxMinutes min"
    }
    $JobOut = (Receive-Job $Job -ErrorAction SilentlyContinue | Select-Object -Last 5) -join " / "
    Remove-Job $Job -Force
    $Log = Get-ChildItem $RunDir -Filter "*-night-$Id.txt" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $Mins = [int]((Get-Date) - $Started).TotalMinutes
    $Head = (git -C $Tree log --oneline -1 2>$null)
    $Fatal = $Log -and (Select-String -Path $Log.FullName -Pattern 'RESOURCE_EXHAUSTED|quota|API key not valid|PERMISSION_DENIED|IneligibleTier|429|"code": 402|code\\": 402|402' -Quiet)
    # Did Gemini itself post its FINISH message since the task started? If not (crash / quota / timeout), the runner reports for it.
    $Since = $Started.ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ss")
    $Recent = (& python $Chat read --last 15 2>$null) -join "`n"
    $SaidFinish = $Recent -match "\[gemini-$Id\] gemini -> claude:\s*\r?\n\s*$Id FINISH"
    $Branch = (git -C $Tree branch --show-current 2>$null)
    $Commits = ((git -C $Tree log --oneline main..HEAD 2>$null) | Select-Object -First 8) -join "; "
    $Report = Get-ChildItem (Join-Path $Tree "docs\port\gemini\outbox") -Filter "$Id-*.report.md" -ErrorAction SilentlyContinue | Select-Object -First 1
    $Errors = if ($Log) { ((Select-String -Path $Log.FullName -Pattern 'Error|error:|FAIL' | Select-Object -Last 3).Line -join " / ") } else { "no log; runner output: $JobOut" }
    $Usage = if ($Log) { (& python (Join-Path $PSScriptRoot "gemini_usage.py") $Log.FullName --task $Id --model $Model) -join " " } else { "tokens n/a" }
    $Text = "gemini-cli $Id ended after $Mins min | $Usage" + $(if ($Fatal) { " - QUOTA/AUTH ERROR, queue STOPPED, Claude must take over the rest" } else { "" }) +
        " | Gemini FINISH message: " + $(if ($SaidFinish) { "yes" } else { "MISSING - treat as unfinished" }) +
        " | branch: $Branch | commits: $Commits | report: " + $(if ($Report) { $Report.Name } else { "MISSING" }) +
        " | last errors in log: $Errors | log: Saved/Orchestrator/gemini_runs/$($Log.Name)"
    # error text from the API carries quotes / backslashes / newlines that break the argument - keep it plain
    $Text = ($Text -replace '[\\"`]', ' ' -replace '\s+', ' ')
    if ($Text.Length -gt 1500) { $Text = $Text.Substring(0, 1500) + " ..." }
    & python $Chat post --as gemini --to claude --topic "gemini-cli" --text $Text | Out-Null
    Write-Host "[gemini-night] $Text"
    if ($Fatal) { break }
}
Write-Host "[gemini-night] queue done"
