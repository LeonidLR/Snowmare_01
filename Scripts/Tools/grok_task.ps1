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
    [string]$Label = "grok",
    # Test hooks (Scripts/Tools/tests/test_grok_task.py): a fake grok.exe, a throwaway worktree dir and log dir.
    [string]$GrokPath = "",
    [string]$TreePath = "",
    [string]$LogDir = ""
)
$ErrorActionPreference = "Stop"
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Tree = if ($TreePath) { $TreePath } else { "C:/Work/Lanes/CodexTactics-grok" }
$Grok = if ($GrokPath) { $GrokPath } else { Join-Path $env:USERPROFILE ".grok\bin\grok.exe" }
if (-not (Test-Path $Grok)) { throw "grok.exe not found at $Grok" }
if (-not (Test-Path $Tree)) { throw "Grok worktree not found: $Tree" }
# Billing guard (designer 2026-11-17): Grok must run on the monthly subscription (browser / OIDC login), never on paid API tokens.
# An XAI_API_KEY takes precedence over the login, so refuse to start while one is set (process or user env).
if ($env:XAI_API_KEY -or [Environment]::GetEnvironmentVariable('XAI_API_KEY', 'User') -or [Environment]::GetEnvironmentVariable('XAI_API_KEY', 'Machine')) {
    throw "XAI_API_KEY is set - Grok would bill API tokens instead of the subscription. Remove it first."
}
$env:CODEX_AGENT = "grok"
$env:PYTHONIOENCODING = "utf-8"
$Log = if ($LogDir) { $LogDir } else { Join-Path $Repo "Saved\Orchestrator\grok_runs" }
New-Item -ItemType Directory -Force $Log | Out-Null
$Out = Join-Path $Log ("{0}-{1}.json" -f (Get-Date -Format "yyyyMMdd-HHmmss"), $Label)
$Prompt = Get-Content $PromptFile -Raw -Encoding utf8
$ArgsList = @("-p", $Prompt, "--cwd", $Tree, "-m", $Model, "--max-turns", "$MaxTurns", "--output-format", "json", "--always-approve", "--background-wait-timeout", "5400",
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
# smokes) opened a new Windows Terminal window. Started hidden, its children inherit the hidden console.
# Start-Process joins ArgumentList into ONE command-line string without quoting, so quote each argument per the CommandLineToArgvW
# rules (backslashes before a quote are doubled, quote -> \", whole argument wrapped in quotes): double quotes, newlines, Russian text
# and paths with spaces then arrive as exactly one argument.
function ConvertTo-WinArg([string]$a) {
    if ($a.Length -gt 0 -and $a -notmatch '[\s"]') { return $a }
    $bs = [string][char]92
    $dq = [string][char]34
    $sb = New-Object Text.StringBuilder
    $n = 0   # run of backslashes seen so far
    foreach ($c in $a.ToCharArray()) {
        if ($c -eq [char]92) { $n++; continue }
        if ($c -eq [char]34) { [void]$sb.Append($bs * (2 * $n + 1)).Append($dq) }
        else { [void]$sb.Append($bs * $n).Append($c) }
        $n = 0
    }
    [void]$sb.Append($bs * (2 * $n))   # trailing backslashes precede the closing quote
    return $dq + $sb.ToString() + $dq
}
$ArgLine = ($ArgsList | ForEach-Object { ConvertTo-WinArg $_ }) -join " "
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
