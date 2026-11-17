# Runs one Grok (xAI) headless step in Grok's own worktree (2026-11-17, Grok onboarding). Claude drives Grok this way instead of the
# designer pasting prompts: each call = one prompt, the session is resumed by -Session so Grok keeps its context between calls.
#   powershell -ExecutionPolicy Bypass -File Scripts/Tools/grok_task.ps1 -PromptFile <file> [-Session G001] [-Model grok-4.7] [-MaxTurns 200]
# Models: grok-4.7 (main, router levels 3-4), grok-4.7-build-fast (cheaper, levels 1-2).
# Safety: the run is confined to CodexTactics-grok (--cwd); git push / merge / rebase of main and the protected paths are denied; the
# pre-push guard also refuses main without CODEX_AGENT=claude. Output JSON goes to Saved/Orchestrator/grok_runs/.
param(
    [Parameter(Mandatory)][string]$PromptFile,
    [string]$Session = "",
    [string]$Model = "grok-4.7",
    [int]$MaxTurns = 200,
    [string]$Label = "grok"
)
$ErrorActionPreference = "Stop"
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Tree = Join-Path (Split-Path $Repo -Parent) "CodexTactics-grok"
$Grok = Join-Path $env:USERPROFILE ".grok\bin\grok.exe"
if (-not (Test-Path $Grok)) { throw "grok.exe not found at $Grok" }
if (-not (Test-Path $Tree)) { throw "Grok worktree not found: $Tree" }
# Billing guard (designer 2026-11-17): Grok must run on the monthly subscription (browser / OIDC login), never on paid API tokens.
# An XAI_API_KEY takes precedence over the login, so refuse to start while one is set (process or user env).
if ($env:XAI_API_KEY -or [Environment]::GetEnvironmentVariable('XAI_API_KEY', 'User') -or [Environment]::GetEnvironmentVariable('XAI_API_KEY', 'Machine')) {
    throw "XAI_API_KEY is set - Grok would bill API tokens instead of the subscription. Remove it first."
}
$env:CODEX_AGENT = "grok"
$env:PYTHONIOENCODING = "utf-8"
$Log = Join-Path $Repo "Saved\Orchestrator\grok_runs"
New-Item -ItemType Directory -Force $Log | Out-Null
$Out = Join-Path $Log ("{0}-{1}.json" -f (Get-Date -Format "yyyyMMdd-HHmmss"), $Label)
$Prompt = Get-Content $PromptFile -Raw -Encoding utf8
# Windows PowerShell 5.1 splits a native argument at embedded double quotes (the prompt arrived cut / as extra args): use single quotes.
$Prompt = $Prompt.Replace('"', "'")
$ArgsList = @("-p", $Prompt, "--cwd", $Tree, "-m", $Model, "--max-turns", "$MaxTurns", "--output-format", "json", "--always-approve",
    "--deny", "Bash(git push*)", "--deny", "Bash(git merge*)", "--deny", "Bash(git rebase main*)", "--deny", "Bash(git reset --hard*)",
    "--deny", "Bash(*Remove-Item*-Recurse*)", "--deny", "Bash(rm -rf*)")
if ($Session) {
    # grok -s needs a UUID: a readable name (e.g. grok-G001) maps to a stable UUID, so the same name resumes the same session.
    $Guid = [guid]::Empty
    if (-not [guid]::TryParse($Session, [ref]$Guid)) {
        $Hash = [Security.Cryptography.MD5]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($Session))
        $Guid = [guid]::new($Hash)
    }
    # -s creates a session; a later call must resume it with -r ("Session ... already in use" otherwise).
    $Marker = Join-Path $Log ("session-" + $Guid.ToString() + ".txt")
    if (Test-Path $Marker) { $ArgsList += @("-r", $Guid.ToString()) }
    else { $ArgsList += @("-s", $Guid.ToString()); Set-Content -Path $Marker -Value $Session }
}
# Hidden console (2026-11-17): grok.exe started from an agent shell got no console, so every tool call it made (powershell, the UE
# smokes) opened a new Windows Terminal window. Started hidden, its children inherit the hidden console. Arguments are quoted for
# Start-Process (one string); the prompt has no double quotes (replaced above).
$Quote = { param($a) if ($a -match '[\s]') { '"' + $a + '"' } else { $a } }
$ArgLine = ($ArgsList | ForEach-Object { & $Quote $_ }) -join " "
$ErrFile = "$Out.stderr.txt"
$P = Start-Process -FilePath $Grok -ArgumentList $ArgLine -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $Out -RedirectStandardError $ErrFile
$Raw = Get-Content $Out -Raw -Encoding utf8
if (-not $Raw) { $Raw = Get-Content $ErrFile -Raw -Encoding utf8 }
try {
    $J = $Raw | ConvertFrom-Json
    "done: $Out  cost $([math]::Round([double]$J.total_cost_usd, 3)) USD  turns $($J.num_turns)  stop $($J.stopReason)"
    $J.text
} catch {
    "done (no JSON): $Out"
    $Raw.Substring([Math]::Max(0, $Raw.Length - 1500))
}
