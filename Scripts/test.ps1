# Runs Automation tests headless (no rendering). Report goes to Saved\TestReport.
# Usage: powershell -ExecutionPolicy Bypass -File Scripts\test.ps1 [-Filter CodexTactics]
param(
    [string]$Filter = "CodexTactics"
)

$ErrorActionPreference = "Stop"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$ProjectDir = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"
$ReportDir = Join-Path $ProjectDir "Saved\TestReport"
$LogFile = Join-Path $ProjectDir "Saved\Logs\AutomationTests.log"

& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $Project `
    "-ExecCmds=Automation RunTests $Filter" `
    "-TestExit=Automation Test Queue Empty" `
    "-ReportExportPath=$ReportDir" `
    "-abslog=$LogFile" `
    -unattended -nullrhi -nosplash -nosound -nopause | Out-Null

$code = $LASTEXITCODE
$results = Select-String -Path $LogFile -Pattern "Test Completed\. Result=\{(\w+)\}.*Path=\{([^}]+)\}"
if (-not $results) {
    Write-Host "No tests matched '$Filter' (engine exit code $code). Log: $LogFile"
    exit 1
}
$failed = 0
foreach ($r in $results) {
    $status = $r.Matches[0].Groups[1].Value
    $path = $r.Matches[0].Groups[2].Value
    if ($status -ne "Success") { $failed++ }
    Write-Host ("{0,-8} {1}" -f $status, $path)
}
Write-Host ("{0} tests, {1} failed. Report: {2}" -f $results.Count, $failed, $ReportDir)
if ($failed -gt 0 -or $code -ne 0) { exit 1 }
exit 0
