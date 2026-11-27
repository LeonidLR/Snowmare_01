# Shared helpers for the Lead's headless agent wrappers (agy_task.ps1, codex_task.ps1), 2026-11-27.
# The AgentChat task board shows a task "in progress" only with chat / commit activity in the last 2 h; headless agents post rarely,
# so the wrapper posts START, a heartbeat status every $HeartbeatMinutes while the agent runs, and an END line with the exit code.
# Dot-source: . (Join-Path $PSScriptRoot "agent_run_common.ps1")

function Send-AgentChat([string]$Repo, [string]$As, [string]$Topic, [string]$Text, [switch]$Status) {
    $chat = Join-Path $Repo "Scripts\Tools\agent_chat.py"
    $env:PYTHONIOENCODING = "utf-8"
    $a = @($chat, "post", "--as", $As, "--to", "claude", "--topic", $Topic, "--text", ($Text -replace '"', "'"))
    if ($Status) { $a += "--status" }
    & python @a 2>$null | Out-Null
}

# Starts a background job that posts a heartbeat status line every $Minutes while $ProcessId is alive. Returns the job.
function Start-AgentHeartbeat([string]$Repo, [string]$As, [string]$Topic, [string]$TaskId, [int]$ProcessId, [int]$Minutes = 30) {
    Start-Job -ArgumentList $Repo, $As, $Topic, $TaskId, $ProcessId, $Minutes -ScriptBlock {
        param($Repo, $As, $Topic, $TaskId, $ProcessId, $Minutes)
        $chat = Join-Path $Repo "Scripts\Tools\agent_chat.py"
        $env:PYTHONIOENCODING = "utf-8"
        $started = Get-Date
        while ($true) {
            Start-Sleep -Seconds ($Minutes * 60)
            if (-not (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue)) { break }
            $mins = [int]((Get-Date) - $started).TotalMinutes
            & python $chat post --as $As --to claude --topic $Topic --status --text "$TaskId RUNNING | headless run alive $mins min (wrapper heartbeat)" 2>$null | Out-Null
        }
    }
}
