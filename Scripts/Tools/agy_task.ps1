# Runs one Gemini task headless through the Antigravity CLI `agy` (designer's Antigravity subscription, not the Gemini API key) in an
# agent lane (2026-11-27). The Lead uses it when no Antigravity window is working (nights, stalls). The CLI is signed in once by the designer.
#   powershell -ExecutionPolicy Bypass -File Scripts/Tools/agy_task.ps1 -Lane C:/Work/Lanes/CodexTactics-135b -PromptFile <file.md> [-Model id] [-Label 135b]
# Network: the university network blocks Google direct, so every run goes through the local phone proxy (iphone_proxy.py, port 8899).
# The user variable AG_LS_PROXY points at a dead port (53129) and is overridden here for this process only.
# Rules for the agent (lane only, no main / push, chat report) live in the prompt file; --dangerously-skip-permissions = no approvals.
param(
    [Parameter(Mandatory)][string]$Lane,
    [Parameter(Mandatory)][string]$PromptFile,
    [string]$Model = "gemini-4-hydrogen-high",
    [string]$Label = "agy",
    [int]$ProxyPort = 8899,
    [int]$TimeoutMinutes = 0
)
$ErrorActionPreference = "Stop"
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not (Test-Path $Lane)) { throw "Lane $Lane is missing" }
$PromptPath = (Resolve-Path $PromptFile).Path
$Agy = Join-Path $env:LOCALAPPDATA "agy\bin\agy.exe"
if (-not (Test-Path $Agy)) { $Agy = (Get-Command agy -ErrorAction Stop).Source }

if (-not (Get-NetTCPConnection -State Listen -LocalPort $ProxyPort -ErrorAction SilentlyContinue)) {
    throw "PROXY-DOWN: nothing listens on 127.0.0.1:$ProxyPort - start C:\Users\Zephyrus15Duo\Tools\iphone-proxy\iphone_proxy.py (Startup shortcut 'iphone-proxy')"
}
$Proxy = "http://127.0.0.1:$ProxyPort"
$env:AG_LS_PROXY = $Proxy; $env:HTTPS_PROXY = $Proxy; $env:HTTP_PROXY = $Proxy
$env:NO_PROXY = "localhost,127.0.0.1"
$env:CODEX_AGENT = "gemini"  # the pre-push guard keeps gemini/* off main

$RunDir = Join-Path $Repo "Saved\Orchestrator\agy_runs"
New-Item -ItemType Directory -Force $RunDir | Out-Null
$Log = Join-Path $RunDir ("{0}-{1}.log" -f (Get-Date -Format "yyyyMMdd-HHmmss"), $Label)
# A long prompt as an argument breaks on quotes (Windows PowerShell 5.1 native argument passing) and agy -p does not read stdin,
# so the task text stays in its file and the argument only points at it (no double quotes inside).
$Pointer = "Your complete task is in the file $($PromptPath -replace '\\', '/'). Read that whole file first with your file tools, then do exactly what it says, following every rule in it."
$AgyArgs = @("--dangerously-skip-permissions", "--model", $Model, "-p=$Pointer")
if ($TimeoutMinutes -gt 0) { $AgyArgs += @("--print-timeout", "$($TimeoutMinutes)m") }

Write-Host "[agy_task] $Label in $Lane, model $Model, log $Log"
Push-Location $Lane
try {
    $ErrorActionPreference = "Continue"  # agy writes progress to stderr; PowerShell 5.1 would turn it into a terminating error
    & $Agy @AgyArgs 2>&1 | ForEach-Object { "$_" } | Out-File -Encoding utf8 $Log
    $Exit = $LASTEXITCODE
} finally { Pop-Location }
$Fatal = Select-String -Path $Log -Pattern 'sign in|Please sign in|quota|RESOURCE_EXHAUSTED|proxyconnect|token exchange failed' -Quiet
Write-Host "[agy_task] $Label exit $Exit$(if ($Fatal) { ' - AUTH/QUOTA/PROXY problem, see log' })"
exit $Exit
