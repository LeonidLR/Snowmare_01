# Runs a headless Claude Code agent billed to the user's Anthropic API credit (2026-11-07), in an agent worktree.
#   powershell -ExecutionPolicy Bypass -File Scripts/Tools/claude_api_agent.ps1 -Worktree <path> -PromptFile <file> [-Model claude-sonnet-5-5] [-BudgetUsd 10]
# Why the env cleanup: inside the Claude desktop app the shell inherits ANTHROPIC_BASE_URL / CLAUDE_CODE_* (the app's own
# session plumbing); with them the CLI talks to the app instead of the API and fails ("Credit balance is too low").
# --bare: OAuth / keychain are never read, only ANTHROPIC_API_KEY. The key is read from the user environment, never printed.
# The agent may edit files and run commands inside the worktree (acceptEdits + an allow-list); it never gets main:
# the pre-push guard refuses a main push without CODEX_AGENT=claude, and the prompt must keep it on its own branch.
param(
    [Parameter(Mandatory)][string]$Worktree,
    [Parameter(Mandatory)][string]$PromptFile,
    [string]$Model = "claude-sonnet-5-5",
    [double]$BudgetUsd = 10,
    [string]$Label = "agent"
)
$ErrorActionPreference = "Stop"
Get-ChildItem env: | Where-Object { $_.Name -match '^(CLAUDE|ANTHROPIC_BASE_URL)' } | ForEach-Object {
    Remove-Item -LiteralPath "env:$($_.Name)" -ErrorAction SilentlyContinue }
$env:ANTHROPIC_API_KEY = [Environment]::GetEnvironmentVariable('ANTHROPIC_API_KEY', 'User')
if (-not $env:ANTHROPIC_API_KEY) { throw "ANTHROPIC_API_KEY is not set in the user environment" }
$env:CODEX_AGENT = "claude-api"

$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Log = Join-Path $Repo "Saved\Orchestrator\api_agents"
New-Item -ItemType Directory -Force $Log | Out-Null
$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$Out = Join-Path $Log "$Stamp-$Label.json"

Push-Location $Worktree
try {
    $Prompt = Get-Content $PromptFile -Raw
    $Tools = "Read,Edit,Write,Glob,Grep,Bash(git *),Bash(python *),Bash(powershell *),Bash(npm *),PowerShell"
    claude --bare -p $Prompt --model $Model --max-budget-usd $BudgetUsd --permission-mode acceptEdits `
        --allowedTools $Tools --output-format json 2>&1 | Out-File -Encoding utf8 $Out
} finally { Pop-Location }

$Raw = Get-Content $Out -Raw
$Json = $Raw.Substring([Math]::Max(0, $Raw.IndexOf('{'))) | ConvertFrom-Json -ErrorAction SilentlyContinue
$Usd = if ($Json) { [double]$Json.total_cost_usd } else { 0 }
Add-Content -Encoding UTF8 (Join-Path $Repo "Saved\Orchestrator\api_spend.jsonl") (@{
    when = (Get-Date).ToString("s"); model = $Model; label = "cli:$Label"; usd = [Math]::Round($Usd, 4) } | ConvertTo-Json -Compress)
"done: $Out  cost $([Math]::Round($Usd, 3)) USD  error=$($Json.is_error)"
