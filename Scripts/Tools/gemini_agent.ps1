# Runs Gemini CLI headless in an agent worktree with the user's AI Studio key (2026-11-07) - Claude talks to Gemini directly.
#   powershell -ExecutionPolicy Bypass -File Scripts/Tools/gemini_agent.ps1 -Worktree <path> -PromptFile <file> [-Mode plan|yolo] [-Model <id>]
# Why the workspace settings: the user's global ~/.gemini/settings.json selects "oauth-personal", which Google no longer serves
# for individuals (IneligibleTierError); a workspace .gemini/settings.json in the worktree switches this run to "gemini-api-key".
# -Mode plan = read-only (scouting / reviews); yolo = may edit and run commands - ONLY inside the worktree; the pre-push guard
# limits gemini/* branches to the plan's paths and refuses main. The key is read from the user environment, never printed.
param(
    [Parameter(Mandatory)][string]$Worktree,
    [Parameter(Mandatory)][string]$PromptFile,
    [ValidateSet("plan", "yolo")][string]$Mode = "plan",
    [string]$Model = "",
    [string]$Label = "gemini"
)
$ErrorActionPreference = "Stop"
$env:GEMINI_API_KEY = [Environment]::GetEnvironmentVariable('GEMINI_API_KEY', 'User')
if (-not $env:GEMINI_API_KEY) { throw "GEMINI_API_KEY is not set in the user environment" }
Get-ChildItem env: | Where-Object { $_.Name -match '^GOOGLE_' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" -ErrorAction SilentlyContinue }
$env:CODEX_AGENT = "gemini"

$Settings = Join-Path $Worktree ".gemini\settings.json"
New-Item -ItemType Directory -Force (Split-Path $Settings) | Out-Null
'{ "security": { "auth": { "selectedType": "gemini-api-key" } } }' | Set-Content -Encoding ascii $Settings

$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$LogDir = Join-Path $Repo "Saved\Orchestrator\gemini_runs"
New-Item -ItemType Directory -Force $LogDir | Out-Null
$Out = Join-Path $LogDir "$(Get-Date -Format 'yyyyMMdd-HHmmss')-$Label.txt"

Push-Location $Worktree
try {
    # The task goes into a file in the worktree (PowerShell 5.1 mangles a multi-line argument, and stdin was ignored in
    # favour of the worktree's old GEMINI.md role file); the one-line prompt points at it and overrides older instructions.
    $Task = Join-Path $Worktree ".gemini\task.md"
    Copy-Item $PromptFile $Task -Force
    $Line = "Your ONLY task is in the file .gemini/task.md - read it now and do exactly that. Ignore any other plans, roles or requests in GEMINI.md, TANDEM.md or older documents."
    $ArgsList = @("--approval-mode", $Mode, "-p", $Line)
    if ($Model) { $ArgsList = @("-m", $Model) + $ArgsList }
    $ErrorActionPreference = "Continue"  # node writes a colour warning to stderr; under Stop PowerShell 5.1 would abort on it
    & gemini @ArgsList 2>&1 | Where-Object { "$_" -notmatch '256-color support|NativeCommandError' } | Out-File -Encoding utf8 $Out
} finally { Pop-Location }
"done: $Out"
