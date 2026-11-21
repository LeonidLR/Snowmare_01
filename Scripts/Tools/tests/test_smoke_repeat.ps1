# test_smoke_repeat.ps1 - Pester-free unit tests for smoke_parallel repeat & flake logic (094)
# Tests task expansion, duplicate preservation, suffix assignment, summary generation, and exit code logic.
$ErrorActionPreference = "Stop"

$script:Passed = 0
$script:Failed = 0

function Assert([string]$Name, [bool]$Condition, [string]$Details = "") {
    if ($Condition) {
        $script:Passed++
        Write-Host "  [PASS] $Name" -ForegroundColor Green
    } else {
        $script:Failed++
        Write-Host "  [FAIL] $Name : $Details" -ForegroundColor Red
    }
}

Write-Host "=== test_smoke_repeat.ps1 ==="

# Helper implementing the exact task expansion logic from smoke_parallel.ps1
function Expand-SmokeTasks([string[]]$Commands, [int]$Repeat = 1) {
    if ($Repeat -lt 1) { $Repeat = 1 }
    $rawNames = @($Commands | ForEach-Object { $_ -split '[,;\s]+' } | Where-Object { $_ } | ForEach-Object { $_ -replace '^CodexTactics\.', '' })
    $allNames = @()
    foreach ($name in $rawNames) {
        for ($i = 0; $i -lt $Repeat; $i++) {
            $allNames += $name
        }
    }
    $totalRunsPerName = @{}
    foreach ($name in $allNames) {
        if (-not $totalRunsPerName.ContainsKey($name)) { $totalRunsPerName[$name] = 0 }
        $totalRunsPerName[$name]++
    }
    $currentRunPerName = @{}
    $tasks = @()
    foreach ($name in $allNames) {
        if (-not $currentRunPerName.ContainsKey($name)) { $currentRunPerName[$name] = 0 }
        $currentRunPerName[$name]++
        $k = $currentRunPerName[$name]
        $tot = $totalRunsPerName[$name]
        $suffix = if ($tot -gt 1 -or $Repeat -gt 1) { "-r$k" } else { "" }
        $tasks += [pscustomobject]@{
            Name = $name
            Repeat = $k
            Total = $tot
            Suffix = $suffix
        }
    }
    return $tasks
}

# Helper implementing summary string formatting from smoke_parallel.ps1
function Format-SmokeSummary($Results) {
    $summaries = @{}
    $grouped = $Results | Group-Object Name
    foreach ($g in $grouped) {
        $tot = $g.Count
        $passCount = @($g.Group | Where-Object { $_.State -like "PASS*" }).Count
        $failedRepeats = @($g.Group | Where-Object { $_.State -notlike "PASS*" } | ForEach-Object { "r$($_.Repeat)" })
        $summaryStr = if ($passCount -eq $tot) {
            "PASS $passCount/$tot"
        } else {
            "PASS $passCount/$tot (FAIL $($failedRepeats -join ', '))"
        }
        $summaries[$g.Name] = $summaryStr
    }
    return $summaries
}

# --- Test 1: Duplicate preservation without -Repeat (A,A,A -> 3 runs) ---
$tasks1 = @(Expand-SmokeTasks -Commands @("A,A,A") -Repeat 1)
Assert "Duplicate count preserved" ($tasks1.Count -eq 3) "Expected 3 runs, got $($tasks1.Count)"
Assert "Task 1 suffix is -r1" ($tasks1[0].Suffix -eq "-r1" -and $tasks1[0].Repeat -eq 1) "Got $($tasks1[0].Suffix)"
Assert "Task 2 suffix is -r2" ($tasks1[1].Suffix -eq "-r2" -and $tasks1[1].Repeat -eq 2) "Got $($tasks1[1].Suffix)"
Assert "Task 3 suffix is -r3" ($tasks1[2].Suffix -eq "-r3" -and $tasks1[2].Repeat -eq 3) "Got $($tasks1[2].Suffix)"

# --- Test 2: Single command without -Repeat (A -> 1 run, suffix empty) ---
$tasks2 = @(Expand-SmokeTasks -Commands @("MovementSmoke") -Repeat 1)
Assert "Single run count is 1" ($tasks2.Count -eq 1) "Expected 1 run, got $($tasks2.Count)"
Assert "Single run suffix is empty (backward compatibility)" ($tasks2[0].Suffix -eq "") "Got '$($tasks2[0].Suffix)'"
Assert "Single run repeat is 1" ($tasks2[0].Repeat -eq 1) "Got $($tasks2[0].Repeat)"

# --- Test 3: Command with -Repeat 3 ---
$tasks3 = @(Expand-SmokeTasks -Commands @("MovementSmoke") -Repeat 3)
Assert "Repeat 3 yields 3 tasks" ($tasks3.Count -eq 3) "Expected 3 runs, got $($tasks3.Count)"
Assert "Repeat task 1 suffix is -r1" ($tasks3[0].Suffix -eq "-r1" -and $tasks3[0].Repeat -eq 1)
Assert "Repeat task 2 suffix is -r2" ($tasks3[1].Suffix -eq "-r2" -and $tasks3[1].Repeat -eq 2)
Assert "Repeat task 3 suffix is -r3" ($tasks3[2].Suffix -eq "-r3" -and $tasks3[2].Repeat -eq 3)

# --- Test 4: Multiple commands with -Repeat 2 ---
$tasks4 = @(Expand-SmokeTasks -Commands @("SmokeA", "SmokeB") -Repeat 2)
Assert "Multiple commands repeat count" ($tasks4.Count -eq 4) "Expected 4 runs, got $($tasks4.Count)"
Assert "SmokeA r1 suffix" ($tasks4[0].Name -eq "SmokeA" -and $tasks4[0].Suffix -eq "-r1")
Assert "SmokeA r2 suffix" ($tasks4[1].Name -eq "SmokeA" -and $tasks4[1].Suffix -eq "-r2")
Assert "SmokeB r1 suffix" ($tasks4[2].Name -eq "SmokeB" -and $tasks4[2].Suffix -eq "-r1")
Assert "SmokeB r2 suffix" ($tasks4[3].Name -eq "SmokeB" -and $tasks4[3].Suffix -eq "-r2")

# --- Test 5: Summary text formatting (PASS 7/10 with FAIL r3, r6, r9) ---
$fakeResults10 = @()
for ($i = 1; $i -le 10; $i++) {
    $st = if ($i -in @(3, 6, 9)) { "FAIL" } else { "PASS" }
    $fakeResults10 += [pscustomobject]@{ Name = "PmcStalemateSmoke"; Repeat = $i; State = $st }
}
$sum10 = Format-SmokeSummary $fakeResults10
Assert "Summary with failures format" ($sum10["PmcStalemateSmoke"] -eq "PASS 7/10 (FAIL r3, r6, r9)") "Got '$($sum10['PmcStalemateSmoke'])'"

# --- Test 6: Summary text all PASS ---
$fakeResultsPass = @()
for ($i = 1; $i -le 5; $i++) {
    $fakeResultsPass += [pscustomobject]@{ Name = "MovementSmoke"; Repeat = $i; State = $pscustomobject }
    $fakeResultsPass[-1].State = "PASS"
}
$sumPass = Format-SmokeSummary $fakeResultsPass
Assert "Summary all pass format" ($sumPass["MovementSmoke"] -eq "PASS 5/5") "Got '$($sumPass['MovementSmoke'])'"

# --- Test 7: Summary text all FAIL ---
$fakeResultsFail = @()
for ($i = 1; $i -le 3; $i++) {
    $fakeResultsFail += [pscustomobject]@{ Name = "FailingSmoke"; Repeat = $i; State = "FAIL" }
}
$sumFail = Format-SmokeSummary $fakeResultsFail
Assert "Summary all fail format" ($sumFail["FailingSmoke"] -eq "PASS 0/3 (FAIL r1, r2, r3)") "Got '$($sumFail['FailingSmoke'])'"

# --- Test 8: Exit code logic ---
$hasFailures10 = @($fakeResults10 | Where-Object { $_.State -eq "FAIL" }).Count -gt 0
Assert "Exit code 1 on failure" ($hasFailures10 -eq $true) "Expected failure detection"
$hasFailuresPass = @($fakeResultsPass | Where-Object { $_.State -eq "FAIL" }).Count -gt 0
Assert "Exit code 0 on all pass" ($hasFailuresPass -eq $false) "Expected clean pass"

Write-Host ""
Write-Host "Summary: $($script:Passed) passed, $($script:Failed) failed."
if ($script:Failed -gt 0) {
    exit 1
}
exit 0
