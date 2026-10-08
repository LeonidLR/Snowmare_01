# Builds the CodexTactics editor target (Development, Win64) via UnrealBuildTool.
# Full UBT output goes to Saved\Logs\Build.log; the console gets the result line,
# project-code warnings/errors (engine-header deprecation noise excluded), and the log path.
# Takes the shared build lock (Scripts\agent_lock.ps1) and refuses while this project's Unreal Editor is open.
# Usage: powershell -ExecutionPolicy Bypass -File Scripts\build.ps1 [-Target CodexTacticsEditor] [-Config Development]
param(
    [string]$Target = "CodexTacticsEditor",
    [string]$Config = "Development"
)

$ErrorActionPreference = "Stop"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$ProjectDir = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"
$LogFile = Join-Path $ProjectDir "Saved\Logs\Build.log"
New-Item -ItemType Directory -Force (Split-Path $LogFile) | Out-Null

# Only an editor of THIS checkout locks our DLLs (another project's editor, or the user's editor on the main folder while we
# build in the agents' worktree ../CodexTactics-agents, may stay open): match the full .uproject path.
$ProjectFull = [IO.Path]::GetFullPath($Project)
if (Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor.exe'" | Where-Object {
        $_.CommandLine -and ($_.CommandLine.Replace('/', '') -like "*$ProjectFull*") }) {
    Write-Host "Unreal Editor (CodexTactics) is running: close it first (it locks the DLLs)." -ForegroundColor Yellow
    exit 2
}
. (Join-Path $PSScriptRoot "agent_lock.ps1")
$Owned = Enter-AgentLock "build $Target"
try {
    & "$EngineRoot\Engine\Build\BatchFiles\Build.bat" $Target Win64 $Config "-Project=$Project" -WaitMutex -NoHotReloadFromIDE *> $LogFile
    $code = $LASTEXITCODE
} finally {
    if ($Owned) { Exit-AgentLock }
}

$lines = Get-Content $LogFile
$issues = $lines | Where-Object { $_ -match "(error|warning)" -and $_ -notmatch "Epic Games\\UE_5\.8\\Engine\\" -and $_ -notmatch "not a preferred version|has not been heavily tested" }
$issues | Select-Object -First 40 | ForEach-Object { Write-Host $_ }
if (@($issues).Count -gt 40) { Write-Host ("... {0} more issue lines" -f (@($issues).Count - 40)) }
$lines | Where-Object { $_ -match "^Result:|Total execution time" } | ForEach-Object { Write-Host $_ }
Write-Host "Exit code $code. Full log: $LogFile ($((Get-Item $LogFile).Length) bytes)"
exit $code
