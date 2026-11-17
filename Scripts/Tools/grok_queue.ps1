# Night auto-next for Grok (2026-11-17): starts the next unreported G<NNN> task through grok_task.ps1 when Grok is idle.
#   powershell -ExecutionPolicy Bypass -File Scripts/Tools/grok_queue.ps1 [-IdleMinutes 30] [-Model grok-4.7] [-DryRun]
# Meant to be called repeatedly (Claude's night loop / scheduler); each call starts at most ONE task and runs it to the end of that
# headless call (grok_task.ps1 is synchronous), so two never run at once.
# A task is a candidate when docs/port/grok/inbox/G<NNN>-*.md exists, there is no docs/port/grok/outbox/G<NNN>-*.report.md (main
# checkout or Grok worktree) and the agent chat has no "G<NNN> ... FINISH" line. Candidates are taken in numeric order and only the
# FIRST one counts: if its session grok-G<NNN> was already started (marker in grok_runs), the task is in progress (Claude drives it
# or it waits for a fix) - the queue does nothing and never resumes it or jumps over it.
# Idle = no grok.exe process with a -p headless call AND the last FINISH / limit line posted by Grok in the chat is older than
# -IdleMinutes (a fresh FINISH means Claude is about to review it; a limit line means wait for the reset).
param(
    [int]$IdleMinutes = 30,
    [string]$Model = "grok-4.7",
    [switch]$DryRun,
    [string]$ChatFile = ""   # test hook: chat text instead of agent_chat.py output
)
$ErrorActionPreference = "Stop"
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Tree = Join-Path (Split-Path $Repo -Parent) "CodexTactics-grok"
$Inbox = Join-Path $Repo "docs\port\grok\inbox"
$Outboxes = @((Join-Path $Repo "docs\port\grok\outbox"), (Join-Path $Tree "docs\port\grok\outbox"))
$Log = Join-Path $Repo "Saved\Orchestrator\grok_runs"
New-Item -ItemType Directory -Force $Log | Out-Null
$QueueLog = Join-Path $Log "queue.log"
$Lock = Join-Path $Log "queue.lock"

function Write-Q([string]$msg) {
    $line = "{0}  {1}{2}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $(if ($DryRun) { "[dry] " } else { "" }), $msg
    Add-Content -Path $QueueLog -Value $line -Encoding utf8
    $line
}

# Two queues at once: a lock file with the owner PID (stale when that process is gone).
if (Test-Path $Lock) {
    $OwnerPid = [int](Get-Content $Lock -ErrorAction SilentlyContinue | Select-Object -First 1)
    if ($OwnerPid -and (Get-Process -Id $OwnerPid -ErrorAction SilentlyContinue)) { Write-Q "queue already running (pid $OwnerPid) - exit"; return }
}

# Chat: FINISH lines per task and Grok's last FINISH / limit time.
if ($ChatFile) { $Chat = Get-Content $ChatFile -Raw -Encoding utf8 }
else {
    $env:PYTHONIOENCODING = "utf-8"
    $Chat = (& python (Join-Path $PSScriptRoot "agent_chat.py") read --last 40 | Out-String)
}
$Finished = @{}
$LastGrokStop = [datetime]::MinValue
# A message = "[#N yyyy-MM-dd HH:mm:ss] [topic] from -> to:" + text lines up to the next separator.
foreach ($blk in ($Chat -split '(?m)^-{10,}\s*$')) {
    $m = [regex]::Match($blk, '\[#\d+ (\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)\]\s*(?:\[([^\]]*)\]\s*)?(\S+) -> (\S+?):')
    if (-not $m.Success) { continue }
    $when = [datetime]::ParseExact($m.Groups[1].Value, "yyyy-MM-dd HH:mm:ss", $null)
    $topic = $m.Groups[2].Value; $from = $m.Groups[3].Value
    foreach ($f in [regex]::Matches($blk, '\bG(\d{3})\b[^\r\n]*\bFINISH\b')) { $Finished[$f.Groups[1].Value] = $true }
    if ($blk -match '\bG\d{3}\b' -and $blk -match '\bFINISH\b' -and $topic -match '^grok-G(\d{3})') { $Finished[$Matches[1]] = $true }
    if ($from -eq "grok" -and ($blk -match '\bFINISH\b' -or $topic -match 'limit' -or $blk -match '(?i)limit 10\s*%')) {
        if ($when -gt $LastGrokStop) { $LastGrokStop = $when }
    }
}

# Next candidate (numeric order; the first one decides).
$Next = $null
foreach ($t in (Get-ChildItem $Inbox -Filter "G*.md" | Sort-Object Name)) {
    if ($t.Name -notmatch '^G(\d{3})-') { continue }
    $n = $Matches[1]
    $hasReport = $false
    foreach ($o in $Outboxes) { if (Test-Path $o) { if (Get-ChildItem $o -Filter "G$n-*.report.md" -ErrorAction SilentlyContinue) { $hasReport = $true } } }
    if ($hasReport) { continue }
    if ($Finished.ContainsKey($n)) { Write-Q "G$n has a FINISH in chat - skipped"; continue }
    $Next = @{ Num = $n; File = $t }
    break
}
if (-not $Next) { Write-Q "no pending Grok task - nothing to do"; return }
$Num = $Next.Num
$Session = "grok-G$Num"

# Already started (session marker = a headless call was made for this name) -> in progress, never resume or skip it here.
$Hash = [Security.Cryptography.MD5]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($Session))
$Marker = Join-Path $Log ("session-" + ([guid]::new($Hash)).ToString() + ".txt")
if (Test-Path $Marker) { Write-Q "G$Num already started (session $Session) and not finished - in progress, waiting"; return }

# Busy: a grok.exe headless (-p) call is running.
$Busy = @(Get-CimInstance Win32_Process -Filter "Name='grok.exe'" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -match '\s-p\s' })
if ($Busy.Count -gt 0) { Write-Q "grok.exe headless call running (pid $($Busy[0].ProcessId)) - busy, G$Num waits"; return }

# Cool-down after Grok's last FINISH / limit line.
$AgeMin = ((Get-Date) - $LastGrokStop).TotalMinutes
if ($LastGrokStop -gt [datetime]::MinValue -and $AgeMin -lt $IdleMinutes) {
    Write-Q ("last Grok FINISH/limit line {0:N0} min ago (< {1}) - G{2} waits" -f $AgeMin, $IdleMinutes, $Num)
    return
}

$TaskPath = $Next.File.FullName
$Prompt = @"
You are Grok, a worker on CodexTactics (Unreal Engine 5.8.2, C++). Your task is G$Num.
Task file (read it first, from the main checkout, read only): $TaskPath
Your worktree: $Tree. Create the task branch from the newest main: git switch -c grok/G$Num-<slug> main.
First read docs/port/grok/README.md and follow its Hard rules, Workflow and Limit rule exactly. In short:
1. FIRST commit on the task branch = the short PLAN docs/port/grok/outbox/G$Num-<slug>.plan.md, post its hash in the chat, wait up to 20 min for Claude (no answer - continue with your plan).
2. Implement in small commits (explicit paths, only the task's Allowed paths); run the checks the task names; never loosen a check.
3. Write the report docs/port/grok/outbox/G$Num-<slug>.report.md (Russian, Claim | Evidence table with pasted output).
4. Post ONE FINISH line in the chat: G$Num FINISH | status | last commit | done | NOT done | needs Claude | gate | report.
5. 10 % rule: at 10 % of your token / quota limit LEFT stop, commit (WIP ok), update the report, post the grok-limit line, wait for the reset.
Run the model router before the task and each sub-part (README, Model router). End this call with a short status: what you did, last commit, what is next.
"@
$PromptFile = Join-Path $Log "queue-prompt-G$Num.md"

if ($DryRun) {
    Write-Q "WOULD start G$Num ($($Next.File.Name)) session $Session model $Model, prompt file $PromptFile (idle: no grok -p process, last stop line $(if ($LastGrokStop -eq [datetime]::MinValue) { 'none' } else { $LastGrokStop }))"
    return
}

Set-Content -Path $PromptFile -Value $Prompt -Encoding utf8
Set-Content -Path $Lock -Value $PID
try {
    Write-Q "START G$Num ($($Next.File.Name)) session $Session model $Model"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "grok_task.ps1") -PromptFile $PromptFile -Session $Session -Model $Model -Label "G$Num" 2>&1 |
        ForEach-Object { $_.ToString() } | Select-Object -First 12 | ForEach-Object { Write-Q "  $_" }
    Write-Q "END G$Num call returned"
} finally {
    Remove-Item $Lock -Force -ErrorAction SilentlyContinue
}
