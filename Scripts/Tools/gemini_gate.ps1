<#
.SYNOPSIS
    Pre-"done" validation gate for Gemini tasks (036).
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Scripts/Tools/gemini_gate.ps1 -Task 036
    powershell -ExecutionPolicy Bypass -File Scripts/Tools/gemini_gate.ps1 -Task 034 -SkipBuild -SonnetReview
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$Task,
    [string]$Worktree = "",
    [switch]$SkipBuild,
    [switch]$SonnetReview,
    [switch]$OpusReview,
    [string]$Base = ""
)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

if ([string]::IsNullOrWhiteSpace($Worktree)) {
    $Worktree = (Get-Location).Path
}
$resolvedWt = Resolve-Path -Path $Worktree -ErrorAction SilentlyContinue
if ($resolvedWt) {
    $Worktree = $resolvedWt.Path
}
$gitTop = (git -C $Worktree rev-parse --show-toplevel 2>$null)
if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($gitTop)) {
    $Worktree = ($gitTop.Trim() -replace '/', '\')
}

$HelperPy = Join-Path $PSScriptRoot "gemini_gate.py"
if (-not (Test-Path $HelperPy)) {
    Write-Host "ERROR: gemini_gate.py not found at $HelperPy" -ForegroundColor Red
    exit 1
}

function Invoke-Captured {
    param(
        [string]$WorkingDir,
        [scriptblock]$CommandBlock
    )
    Push-Location $WorkingDir
    try {
        $global:LASTEXITCODE = 0
        $out = @(& $CommandBlock 2>&1 | ForEach-Object {
            if ($_ -is [System.Management.Automation.ErrorRecord]) {
                $_.Exception.Message
            } else {
                "$_"
            }
        })
        $code = $LASTEXITCODE
        return @{ Code = $code; Lines = $out }
    }
    finally {
        Pop-Location
    }
}

$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("gemini_gate_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null

try {
    $preJson = Join-Path $tmpDir "pre.json"
    $preArgs = @($HelperPy, "--phase", "pre", "--task", $Task, "--worktree", $Worktree, "--json-out", $preJson)
    if (-not [string]::IsNullOrWhiteSpace($Base)) {
        $preArgs += @("--base", $Base)
    }
    & python @preArgs
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $preJson)) {
        Write-Host "ERROR: Pre-phase check failed." -ForegroundColor Red
        exit 1
    }
    $pre = Get-Content -Path $preJson -Raw -Encoding UTF8 | ConvertFrom-Json

    $outcomes = [System.Collections.ArrayList]::new()
    foreach ($o in $pre.pre_outcomes) {
        [void]$outcomes.Add(@{
            name    = $o.name
            passed  = $o.passed
            summary = $o.summary
            details = @($o.details)
        })
    }

    # --- Check 3: Build + tests ---
    $buildPassed = $null
    $buildParts = [System.Collections.ArrayList]::new()
    $buildDetails = [System.Collections.ArrayList]::new()
    $anyRan = $false

    if ($pre.has_cpp) {
        $anyRan = $true
        $cppOk = $true
        if (-not $SkipBuild) {
            $buildScript = Join-Path $Worktree "Scripts\build.ps1"
            $res = Invoke-Captured $Worktree { powershell -ExecutionPolicy Bypass -File $buildScript }
            $tail = @($res.Lines | Select-Object -Last 8)
            [void]$buildDetails.Add("build.ps1 (exit $($res.Code)):")
            foreach ($l in $tail) { [void]$buildDetails.Add("  $l") }
            if ($res.Code -ne 0) {
                $cppOk = $false
                [void]$buildParts.Add("build.ps1 FAIL ($($res.Code))")
            } else {
                [void]$buildParts.Add("build.ps1 PASS")
            }
        } else {
            [void]$buildParts.Add("build.ps1 SKIP (-SkipBuild)")
        }

        $testScript = Join-Path $Worktree "Scripts\test.ps1"
        $baseArg = $pre.base
        $resTest = Invoke-Captured $Worktree { powershell -ExecutionPolicy Bypass -File $testScript -Smart -Base $baseArg }
        $tailTest = @($resTest.Lines | Select-Object -Last 10)
        [void]$buildDetails.Add("test.ps1 -Smart -Base $baseArg (exit $($resTest.Code)):")
        foreach ($l in $tailTest) { [void]$buildDetails.Add("  $l") }
        if ($resTest.Code -ne 0) {
            $cppOk = $false
            [void]$buildParts.Add("test.ps1 -Smart FAIL ($($resTest.Code))")
        } else {
            [void]$buildParts.Add("test.ps1 -Smart PASS")
        }
        $buildPassed = $cppOk
    }

    if ($pre.has_tg) {
        $anyRan = $true
        $tgDir = Join-Path $Worktree "Tools\TrainingGround"
        $tgOk = $true
        $resNpm = Invoke-Captured $tgDir { npm test }
        foreach ($l in @($resNpm.Lines | Select-Object -Last 8)) { [void]$buildDetails.Add("npm test: $l") }
        if ($resNpm.Code -ne 0) { $tgOk = $false; [void]$buildParts.Add("npm test FAIL") } else { [void]$buildParts.Add("npm test PASS") }

        $resTsc = Invoke-Captured $tgDir { npx tsc --noEmit }
        foreach ($l in @($resTsc.Lines | Select-Object -Last 8)) { [void]$buildDetails.Add("tsc: $l") }
        if ($resTsc.Code -ne 0) { $tgOk = $false; [void]$buildParts.Add("tsc FAIL") } else { [void]$buildParts.Add("tsc PASS") }

        if ($null -eq $buildPassed) { $buildPassed = $tgOk } else { $buildPassed = $buildPassed -and $tgOk }
    }

    if ($pre.has_py_tools) {
        $anyRan = $true
        $resPy = Invoke-Captured $Worktree { python -m unittest discover -s "Scripts/Tools/tests" -p "test_*.py" -v }
        foreach ($l in @($resPy.Lines | Select-Object -Last 12)) { [void]$buildDetails.Add("unittest: $l") }
        $pyOk = ($resPy.Code -eq 0)
        if ($pyOk) { [void]$buildParts.Add("Python unittest PASS") } else { [void]$buildParts.Add("Python unittest FAIL ($($resPy.Code))") }
        if ($null -eq $buildPassed) { $buildPassed = $pyOk } else { $buildPassed = $buildPassed -and $pyOk }
    }

    if (-not $anyRan) {
        $buildSummary = "Skipped (docs/non-code diff only)"
    } else {
        $buildSummary = $buildParts -join "; "
    }
    [void]$outcomes.Add(@{
        name    = "Build + tests"
        passed  = $buildPassed
        summary = $buildSummary
        details = @($buildDetails)
    })

    # --- Check 4: Jev audit ---
    $triagePy = Join-Path $Worktree "Scripts\Tools\typesafe_triage.py"
    if (Test-Path $triagePy) {
        $resJev = Invoke-Captured $Worktree { python $triagePy --audit-diff --agent gemini }
        $jevOk = ($resJev.Code -eq 0)
        $jevTail = @($resJev.Lines | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Last 6)
        $jevLast = if ($jevTail.Count -gt 0) { $jevTail[-1] } else { "Completed" }
        [void]$outcomes.Add(@{
            name    = "Jev audit"
            passed  = $jevOk
            summary = $(if ($jevOk) { "PASS: $jevLast" } else { "FAIL (exit $($resJev.Code)): $jevLast" })
            details = $jevTail
        })
    } else {
        [void]$outcomes.Add(@{
            name    = "Jev audit"
            passed  = $false
            summary = "typesafe_triage.py not found"
            details = @()
        })
    }

    # --- Write combined intermediate results and run Post phase (Checks 5, 6, 7 + report) ---
    $postInput = @{
        task          = $pre.task
        repo          = $pre.repo
        branch        = $pre.branch
        base          = $pre.base
        changed_files = @($pre.changed_files)
        outcomes      = @($outcomes)
    }
    $resultsJson = Join-Path $tmpDir "results.json"
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($resultsJson, ($postInput | ConvertTo-Json -Depth 6), $utf8NoBom)

    $postArgs = @($HelperPy, "--phase", "post", "--task", $Task, "--worktree", $Worktree, "--results-json", $resultsJson)
    if ($SonnetReview) {
        $postArgs += "--sonnet-review"
    }
    if ($OpusReview) {
        $postArgs += "--opus-review"
    }
    & python @postArgs
    $finalCode = $LASTEXITCODE
    exit $finalCode
}
finally {
    Remove-Item -Path $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
}
