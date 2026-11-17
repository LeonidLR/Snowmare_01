# Runs a PowerShell script in a HIDDEN console and waits for it (2026-11-17, the designer: "dozens of Terminal windows").
# Why: on Windows 11 the default terminal is Windows Terminal; a console program started by a process that has no console of its own
# (an agent's shell, python subprocess, grok.exe tools, the Claude CLI) gets a NEW console = a new Terminal window. Started under one
# hidden console here, every child (powershell, cmd, UnrealEditor-Cmd, git, python) inherits it and opens no window.
#   powershell -ExecutionPolicy Bypass -File Scripts/Tools/run_hidden.ps1 -Script <path.ps1> [-Args @('-A','x')] [-Log <file>]
#   powershell -ExecutionPolicy Bypass -File Scripts/Tools/run_hidden.ps1 -Command "<a command line>" [-Log <file>]
# Output goes to -Log (default Saved/Logs/run_hidden-<time>.log) and is printed when the run ends; the exit code is passed through.
param(
    [string]$Script = "",
    [string[]]$Args = @(),
    [string]$Command = "",
    [string]$Log = "",
    [string]$WorkingDirectory = (Get-Location).Path
)
$ErrorActionPreference = "Stop"
if (-not $Script -and -not $Command) { throw "give -Script or -Command" }
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $Log) {
    $Dir = Join-Path $Repo "Saved\Logs"
    New-Item -ItemType Directory -Force $Dir | Out-Null
    $Log = Join-Path $Dir ("run_hidden-{0}.log" -f (Get-Date -Format "yyyyMMdd-HHmmss-fff"))
}
$Inner = if ($Script) {
    $Quoted = ($Args | ForEach-Object { "'" + ($_ -replace "'", "''") + "'" }) -join " "
    "& '$($Script -replace "'", "''")' $Quoted"
} else { $Command }
# The inner run writes everything (all streams) to the log and returns its exit code.
$Wrapped = "`$ErrorActionPreference='Continue'; & { $Inner } *>&1 | Out-File -FilePath '$($Log -replace "'", "''")' -Encoding utf8; exit `$LASTEXITCODE"
# -EncodedCommand: no quoting problems (Windows PowerShell 5.1 splits native arguments at embedded double quotes).
$Encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($Wrapped))
$Proc = Start-Process powershell -WindowStyle Hidden -Wait -PassThru -WorkingDirectory $WorkingDirectory `
    -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-EncodedCommand", $Encoded)
if (Test-Path $Log) { Get-Content $Log -Encoding utf8 }
exit $Proc.ExitCode
