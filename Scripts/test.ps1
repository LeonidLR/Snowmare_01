# Automation tests (and, with -Changed, the impacted headless smokes).
# Usage:
#   test.ps1                                  all CodexTactics.* tests
#   test.ps1 -Filter CodexTactics.Grid        one filter ("A+B" for several)
#   test.ps1 -Changed [-Quick] [-Base HEAD]   impact map (Scripts/test_map.json) over `git diff --name-only <Base>` + untracked files:
#                                             ONE unit-test run with the combined filter, then the mapped smokes in parallel
#                                             (-Quick = only the 1-2 key smokes per rule, for the edit loop; -Jobs N, -Serial)
#   test.ps1 -Changed -DryRun                 print the plan (files -> rules, filter, smokes) and run nothing
#   test.ps1 -Changed -SkipSmokes | -SkipUnit only one half
#   test.ps1 -Smart                           = -Changed (the map decides); add -Jev to ALSO union Jev's include-based unit filter
#   test.ps1 -CheckMap                        validate Scripts/test_map.json against Debug/*SmokeCommand.cpp (every smoke mapped)
param(
    [string]$Filter = "CodexTactics",
    [switch]$Smart,
    [switch]$Jev,
    [switch]$Changed,
    [switch]$Quick,
    [switch]$DryRun,
    [switch]$SkipSmokes,
    [switch]$SkipUnit,
    [switch]$Serial,
    [switch]$CheckMap,
    [int]$Jobs = 0,
    [string]$Base = "HEAD"
)

$ErrorActionPreference = "Stop"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$ProjectDir = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"
$ReportDir = Join-Path $ProjectDir "Saved\TestReport"
$LogFile = Join-Path $ProjectDir "Saved\Logs\AutomationTests.log"
. (Join-Path $PSScriptRoot "verify_lib.ps1")

if ($CheckMap) {
    $problems = @(Test-MapCoverage (Get-TestMap))
    if ($problems.Count) { $problems | ForEach-Object { Write-Host "MAP: $_" -ForegroundColor Red }; exit 1 }
    Write-Host "Impact map OK: every smoke command is registered and mapped."
    exit 0
}

if ($Smart) { $Changed = $true }
$Plan = $null
if ($Changed) {
    $Map = Get-TestMap
    $files = @(Get-ChangedFiles $Base)
    $Plan = Resolve-Impact $files $Map
    $smokeList = @($(if ($Quick) { $Plan.QuickSmokes } else { $Plan.Smokes }))
    $Filters = @($Plan.Tests)
    if ($Jev) {
        Write-Host "[Jev] Optional hint: include-based unit filter via TypeSafe System One..."
        $hint = python (Join-Path $PSScriptRoot "Tools\typesafe_triage.py") --smart-test | Select-String -Pattern "FILTER=(.+)"
        if ($hint) { $Filters = @(Compress-Filters ($Filters + ($hint.Matches[0].Groups[1].Value.Trim() -split '\+'))) }
    }
    Write-Host ("Changed files: {0} (base {1})" -f $files.Count, $Base)
    $skipped = @($Plan.Rows | Where-Object { $_.Rules -eq "no-verification" -or (-not $_.Rules -and -not $_.Tests -and -not $_.Smokes) })
    $Plan.Rows | Where-Object { $_ -notin $skipped } | ForEach-Object { Write-Host ("  {0,-70} {1}" -f $_.File, $(if ($_.Rules) { $_.Rules } elseif ($_.Smokes) { "(its own smoke)" } else { "(fallback)" })) }
    if ($skipped.Count) { Write-Host ("  (+{0} docs / scripts / tooling files need no verification)" -f $skipped.Count) }
    if ($Plan.Unmapped.Count) { Write-Host ("  unmapped Source files -> fallback: {0}" -f ($Plan.Unmapped -join ", ")) -ForegroundColor Yellow }
    if ($Filters.Count) { $Filter = $Filters -join "+" } else { $Filter = "" }
    Write-Host ("Unit filter : {0}" -f $(if ($Filter) { $Filter } else { "(none)" }))
    Write-Host ("Smokes ({0}): {1}" -f $(if ($Quick) { "quick" } else { "all mapped" }), $(if ($smokeList.Count) { $smokeList -join ", " } else { "(none)" }))
    if ($DryRun) { exit 0 }
    if (-not $Filter -and -not $smokeList.Count) { Write-Host "Nothing to verify for these changes."; exit 0 }
}

. (Join-Path $PSScriptRoot "agent_lock.ps1")
$Owned = Enter-AgentLock $(if ($Changed) { "test -Changed" } else { "tests $Filter" })
$UnitFailed = 0
$UnitCount = 0
$exitCode = 0
$unitWatch = [System.Diagnostics.Stopwatch]::StartNew()
try {
    if ((-not $Changed -or ($Filter -and -not $SkipUnit))) {
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
            $exitCode = 1
        } else {
            foreach ($r in $results) {
                $status = $r.Matches[0].Groups[1].Value
                $path = $r.Matches[0].Groups[2].Value
                if ($status -ne "Success") { $UnitFailed++ }
                if ($status -ne "Success" -or -not $Changed) { Write-Host ("{0,-8} {1}" -f $status, $path) }
            }
            $UnitCount = @($results).Count
            Write-Host ("{0} tests, {1} failed. Report: {2}" -f $UnitCount, $UnitFailed, $ReportDir)
            if ($UnitFailed -gt 0 -or $code -ne 0) { $exitCode = 1 }
        }
    }
    $unitSeconds = $unitWatch.Elapsed.TotalSeconds
    $smokeCode = 0
    if ($Changed -and -not $SkipSmokes -and $smokeList.Count) {
        # The lock is ours for the whole run: smoke_parallel sees CODEX_LOCK_HELD=1 and does not take it again.
        $smokeArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "smoke_parallel.ps1"), "-Commands", ($smokeList -join ","))
        if ($Jobs -gt 0) { $smokeArgs += @("-Jobs", $Jobs) }
        if ($Serial) { $smokeArgs += "-Serial" }
        & powershell @smokeArgs
        $smokeCode = $LASTEXITCODE
        if ($smokeCode -ne 0) { $exitCode = 1 }
    }
} finally {
    if ($Owned) { Exit-AgentLock }
}
if ($Changed) {
    Write-Host ""
    Write-Host ("== test -Changed: unit {0}/{1} failed ({2:N0} s); smokes {3}{4} ==" -f $UnitFailed, $UnitCount, $unitSeconds, $(if ($SkipSmokes -or -not $smokeList.Count) { "skipped" } else { "$($smokeList.Count) run" }), $(if ($smokeCode) { ", FAILED" } else { "" })) -ForegroundColor $(if ($exitCode) { "Red" } else { "Green" })
}
exit $exitCode
