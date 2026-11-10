# Weekly code audit (zero Claude tokens): UBT static analysis of the project modules + git churn + Jev risk + coverage map
# -> docs/port/audit/AUDIT_<date>.md (+ audit_<date>.json, static_baseline.json) and docs/port/project-health.html.
# Guards: refuses the analyzer while this checkout's Unreal Editor is open (waits up to -WaitEditorHours, then skips the
# analyzer with a note and still writes the churn / coverage report); the build takes the shared lock (Scripts\agent_lock.ps1).
# After the analysis a normal build runs so the binaries are usable again (-NoRestore to skip).
# Usage: powershell -ExecutionPolicy Bypass -File Scripts\Tools\weekly_audit.ps1 [-Root <checkout of main>] [-Date yyyy-MM-dd]
#        [-SkipAnalyzer] [-AnalyzerLog <existing log>] [-WaitEditorHours 3] [-Since "7 days ago"] [-NoRestore] [-NoJev]
# Registered weekly by Scripts\Tools\register_weekly_audit.ps1.
param(
    [string]$Root = "",
    [string]$Date = (Get-Date -Format "yyyy-MM-dd"),
    [switch]$SkipAnalyzer,
    [string]$AnalyzerLog = "",
    [int]$WaitEditorHours = 3,
    [string]$Since = "7 days ago",
    [switch]$NoRestore,
    [switch]$NoJev
)

$ErrorActionPreference = "Stop"
if (-not $env:CODEX_AGENT) { $env:CODEX_AGENT = "claude" }
$ToolDir = $PSScriptRoot
if (-not $Root) { $Root = Split-Path (Split-Path $ToolDir -Parent) -Parent }
$Root = [IO.Path]::GetFullPath($Root)
$Uproject = Join-Path $Root "CodexTactics.uproject"
$BuildScript = Join-Path (Split-Path $ToolDir -Parent) "build.ps1"

function Test-EditorOpen {
    [bool](Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor.exe'" | Where-Object {
            $_.CommandLine -and ($_.CommandLine.Replace('/', '') -like "*$Uproject*") })
}

$analyzerTime = ""
$note = ""
$flags = "-StaticAnalyzer=Default -StaticAnalyzerProjectOnly (MSVC /analyze, Development Win64, CodexTacticsEditor)"
if ($SkipAnalyzer) {
    $note = "skipped by -SkipAnalyzer"
} elseif ($AnalyzerLog) {
    $note = "log supplied"
} else {
    $deadline = (Get-Date).AddHours($WaitEditorHours)
    while ((Test-EditorOpen) -and ((Get-Date) -lt $deadline)) { Start-Sleep -Seconds 300 }
    if (Test-EditorOpen) {
        $note = "editor was open for $WaitEditorHours h, analyzer skipped"
        Write-Host "[audit] $note" -ForegroundColor Yellow
    } else {
        $sw = [Diagnostics.Stopwatch]::StartNew()
        & powershell -NoProfile -ExecutionPolicy Bypass -File $BuildScript -StaticAnalyzer Default -ProjectDirOverride $Root
        $code = $LASTEXITCODE
        $sw.Stop()
        $analyzerTime = "{0}m{1:00}s" -f [int][math]::Floor($sw.Elapsed.TotalMinutes), $sw.Elapsed.Seconds
        $AnalyzerLog = Join-Path $Root "Saved\Logs\StaticAnalysis.log"
        if ($code -ne 0) { $note = "analysis build exit code $code (warnings parsed anyway)" }
        Write-Host "[audit] analysis $analyzerTime, exit $code"
    }
}

$pyArgs = @((Join-Path (Join-Path $ToolDir "audit") "run_audit.py"), "--root", $Root, "--date", $Date, "--since", $Since,
            "--analyzer-time", $analyzerTime, "--analyzer-flags", $flags)
if ($AnalyzerLog -and (Test-Path $AnalyzerLog)) { $pyArgs += @("--analyzer-log", $AnalyzerLog) }
if ($NoJev) { $pyArgs += "--no-jev" }
& python @pyArgs
if ($note) { Add-Content -Path (Join-Path $Root "docs\port\audit\AUDIT_$Date.md") -Value "`nNote: $note" -Encoding utf8 }
& python (Join-Path $ToolDir "build_health_dashboard.py") --root $Root

if ($analyzerTime -and -not $NoRestore) {
    Write-Host "[audit] restoring the normal build"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $BuildScript -ProjectDirOverride $Root
}
