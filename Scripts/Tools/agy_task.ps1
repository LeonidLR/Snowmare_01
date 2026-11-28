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

# University network: Google only through the phone proxy. Home network: the proxy has no upstream (no hotspot) -> go direct.
# Pick the route that actually reaches Google now (2026-11-27, designer at home: "Bad Gateway" through the proxy).
function Test-GoogleRoute([string]$ProxyUrl) {
    try {
        $req = [System.Net.WebRequest]::Create("https://www.googleapis.com/discovery/v1/apis")
        $req.Timeout = 8000
        $req.Proxy = if ($ProxyUrl) { New-Object System.Net.WebProxy($ProxyUrl) } else { $null }
        $resp = $req.GetResponse(); $resp.Close(); return $true
    } catch [System.Net.WebException] {
        # a 4xx from Google = reachable; a 502 / 504 is the proxy itself failing upstream
        $r = $_.Exception.Response
        return [bool]($r -and [int]$r.StatusCode -lt 500)
    } catch { return $false }
}
$Proxy = "http://127.0.0.1:$ProxyPort"
$ProxyUp = [bool](Get-NetTCPConnection -State Listen -LocalPort $ProxyPort -ErrorAction SilentlyContinue)
if ($ProxyUp -and (Test-GoogleRoute $Proxy)) {
    $env:AG_LS_PROXY = $Proxy; $env:HTTPS_PROXY = $Proxy; $env:HTTP_PROXY = $Proxy
    Write-Host "[agy_task] network: phone proxy $Proxy"
} elseif (Test-GoogleRoute "") {
    $env:AG_LS_PROXY = ""; $env:HTTPS_PROXY = ""; $env:HTTP_PROXY = ""
    Write-Host "[agy_task] network: direct (proxy $ProxyPort has no upstream)"
} else {
    throw "NO-NETWORK: Google is reachable neither through $Proxy nor directly (university: start iphone_proxy.py; check Antigravity Tools DNS rules)"
}
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

. (Join-Path $PSScriptRoot "agent_run_common.ps1")
$Topic = "gemini-$Label"
Write-Host "[agy_task] $Label in $Lane, model $Model, log $Log"
Send-AgentChat $Repo "gemini" $Topic "$Label START | headless agy run ($Model) in $(Split-Path $Lane -Leaf), started by the Lead"
$ErrFile = "$Log.stderr"
$Proc = Start-Process -FilePath $Agy -ArgumentList ($AgyArgs | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) `
    -WorkingDirectory $Lane -NoNewWindow -PassThru -RedirectStandardOutput $Log -RedirectStandardError $ErrFile
$null = $Proc.Handle  # cache the handle, else ExitCode stays empty for Start-Process -PassThru
$Beat = Start-AgentHeartbeat $Repo "gemini" $Topic $Label $Proc.Id
$Proc.WaitForExit()
$Exit = $Proc.ExitCode
Stop-Job $Beat -ErrorAction SilentlyContinue; Remove-Job $Beat -Force -ErrorAction SilentlyContinue
if (Test-Path $ErrFile) { Get-Content $ErrFile | Add-Content -Encoding utf8 $Log; Remove-Item $ErrFile -ErrorAction SilentlyContinue }
$Fatal = Select-String -Path $Log -Pattern 'sign in|Please sign in|quota|RESOURCE_EXHAUSTED|proxyconnect|token exchange failed' -Quiet
Write-Host "[agy_task] $Label exit $Exit$(if ($Fatal) { ' - AUTH/QUOTA/PROXY problem, see log' })"
Send-AgentChat $Repo "gemini" $Topic "$Label ENDED | headless agy run exit $Exit$(if ($Fatal) { ' - AUTH/QUOTA/PROXY problem' }) | log Saved/Orchestrator/agy_runs/$(Split-Path $Log -Leaf)" -Status
exit $Exit
