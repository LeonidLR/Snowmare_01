# Lead merge gate. Run from the main checkout after review. DryRun only plans the merge.
# Example: powershell -ExecutionPolicy Bypass -File Scripts/Tools/merge_gate.ps1 -Branches 'codex/054-traversal-smoke-goals,codex/057-guards' -DryRun
param(
    [Parameter(Position=0)][string[]]$Branches,
    [switch]$DryRun,
    [switch]$FullTests
)

function ConvertFrom-GateGitStatus([string]$Text) {
    $records = $Text.Split([char]0)
    for ($index = 0; $index -lt $records.Count; $index++) {
        $record = $records[$index]
        if (-not $record) { continue }
        if ($record.Length -lt 4 -or $record[2] -ne ' ') { throw "Invalid git status record" }
        $status = $record.Substring(0, 2)
        [pscustomobject]@{ Status = $status; Path = $record.Substring(3) }
        if ($status -match '[RC]') { $index++ } # porcelain -z adds the original path after a rename or copy
    }
}

function Test-GateEditorCommandLine([string]$CommandLine, [string]$ProjectPath) {
    if (-not $CommandLine) { return $false }
    $path = [regex]::Escape($ProjectPath.Replace('/', '\'))
    $line = $CommandLine.Replace('/', '\')
    return $line -match "(?:^|[\s\x22])$path(?=[\s\x22]|$)" -and
        $line -notmatch '(?:^|[\s\x22])-(?:game(?=[\s\x22]|$)|run=)'
}

function Get-GateAutomationFailures([string[]]$Log, [string[]]$DirtyPaths) {
    $knownInputs = @{
        'CodexTactics.LevelGen.MissionGoals.CommittedFiles' = 'Content/Data/LevelGen/objectives/L_Gen_Depot_1_X6.objectives.json'
        'CodexTactics.LevelGen.Props.Power.GeneratedMaps' = 'Content/Data/LevelGen/props/L_Gen_Depot_1_X6.props.json'
    }
    $known = @()
    $unexpected = @()
    $pattern = 'Test Completed\. Result=\{(?<result>[^}]+)\}.*Path=\{(?<path>[^}]+)\}'
    $completed = 0
    $queueCount = -1
    $cleanExit = $false
    $fatal = $false
    foreach ($line in $Log) {
        if ($line -match 'LogAutomationCommandLine:.*Automation Test Queue Empty (?<count>\d+) tests performed') {
            $queueCount = [int]$Matches['count']
        }
        if ($line -match 'LogExit:.*TestExit: Automation Test Queue Empty') { $cleanExit = $true }
        if ($line -match 'Fatal error:|=== Critical error: ===') { $fatal = $true }
        if ($line -notmatch $pattern) { continue }
        $completed++
        if ($Matches.result -eq 'Success') { continue }
        $name = $Matches.path
        if ($Matches.result -in @('Fail','Failed') -and $knownInputs.ContainsKey($name) -and $DirtyPaths -contains $knownInputs[$name]) {
            $known += $name
        } else {
            $unexpected += $name
        }
    }
    $complete = $completed -gt 0 -and $queueCount -eq $completed -and $cleanExit -and -not $fatal
    return [pscustomobject]@{ Known = $known; Unexpected = $unexpected; Complete = $complete }
}

function Get-GateFileSha256([string]$Path) {
    $file = [IO.File]::OpenRead($Path)
    try {
        $sha = [Security.Cryptography.SHA256]::Create()
        try {
            return [BitConverter]::ToString($sha.ComputeHash($file)).Replace('-', '')
        } finally {
            $sha.Dispose()
        }
    } finally {
        $file.Dispose()
    }
}

function Backup-GateDesignerFiles([string]$RepoDir, [string]$BackupDir, [string[]]$Paths) {
    $repoPrefix = [IO.Path]::GetFullPath($RepoDir).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $backupPrefix = [IO.Path]::GetFullPath($BackupDir).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $entries = @(
        foreach ($path in $Paths) {
            $source = [IO.Path]::GetFullPath((Join-Path $RepoDir $path))
            $backup = [IO.Path]::GetFullPath((Join-Path $BackupDir $path))
            if (-not $source.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase) -or
                -not $backup.StartsWith($backupPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Backup path escapes its directory: $path"
            }
            $expectedHash = Get-GateFileSha256 $source
            $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($backup))
            [IO.File]::Copy($source, $backup)
            if ((Get-GateFileSha256 $backup) -ne $expectedHash -or (Get-GateFileSha256 $source) -ne $expectedHash) {
                throw "SHA256 verification failed or the source changed during backup: $path"
            }
            [pscustomobject]@{ Path = $path; SHA256 = $expectedHash }
        }
    )
    ConvertTo-Json -InputObject $entries | Set-Content -LiteralPath (Join-Path $BackupDir 'SHA256.json') -Encoding UTF8
}

function Get-GateDiffPaths([string]$From, [string]$To, [switch]$Preview) {
    $range = if ($Preview) { "$From...$To" } else { "$From..$To" }
    $raw = git diff --no-renames --name-only -z $range --
    if ($LASTEXITCODE -ne 0) { throw "Could not read changed paths for $range" }
    return ($raw -join [Environment]::NewLine).Split([char]0) | Where-Object { $_ }
}

function Test-GateNeedsBuild([string[]]$Paths) {
    return [bool]($Paths -match '^Source/|\.uproject$')
}

function Show-GateSummary($Stages) {
    $Stages.GetEnumerator() | ForEach-Object {
        [pscustomobject]@{ Step = $_.Key; Result = $_.Value }
    } | Format-Table -AutoSize | Out-Host
}

function Invoke-MergeGate([string[]]$BranchNames, [switch]$Preview, [switch]$RunFull) {
    $ErrorActionPreference = 'Stop'
    $toolsDir = $PSScriptRoot
    $scriptsDir = Split-Path $toolsDir -Parent
    $repoDir = Split-Path $scriptsDir -Parent
    $stages = [ordered]@{ Editor = 'pending'; Backup = 'pending'; Merge = 'pending'; Build = 'pending'; Tests = 'pending'; 'Worker smokes' = 'pending' }
    $stage = 'Editor'
    $ownsLock = $false
    $code = 1
    $previousEncoding = [Console]::OutputEncoding
    Push-Location -LiteralPath $repoDir
    try {
        [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
        $names = @($BranchNames -split '[,\s]+' | Where-Object { $_ })
        if (-not $names.Count) { throw 'Specify at least one branch with -Branches.' }
        $project = Join-Path $repoDir 'CodexTactics.uproject'
        $editor = Get-CimInstance Win32_Process -Filter "Name = 'UnrealEditor.exe'" | Where-Object {
            Test-GateEditorCommandLine $_.CommandLine $project
        } | Select-Object -First 1
        if ($editor) { throw "The designer's Unreal Editor is open for this checkout (PID $($editor.ProcessId))." }
        $stages.Editor = 'clear'

        if (-not $Preview) {
            . (Join-Path $scriptsDir 'agent_lock.ps1')
            $ownsLock = Enter-AgentLock 'Lead merge gate'
            if ((git branch --show-current) -ne 'main') { throw 'Run the merge gate on main. Use -DryRun to preview from another branch.' }
        }
        $head = (git rev-parse --verify HEAD)
        if ($LASTEXITCODE -ne 0) { throw 'Could not resolve HEAD.' }
        $tips = @(
            foreach ($name in $names) {
                $tip = git rev-parse --verify --end-of-options "$name^{commit}"
                if ($LASTEXITCODE -ne 0) { throw "Cannot resolve branch $name." }
                [pscustomobject]@{ Name = $name; Commit = $tip }
            }
        )
        $status = git --no-optional-locks status --porcelain=v1 -z --untracked-files=no
        if ($LASTEXITCODE -ne 0) { throw 'Could not read designer changes.' }
        $changes = @(ConvertFrom-GateGitStatus ($status -join [Environment]::NewLine))
        $dirty = @($changes | Where-Object Status -eq ' M' | ForEach-Object Path)
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
        $stage = 'Backup'
        if ($Preview -or -not $dirty.Count) {
            $stages.Backup = if ($Preview) { "would verify SHA256 for $($dirty.Count) file(s)" } else { 'no modified tracked files' }
        } else {
            $backupDir = Join-Path $repoDir "Saved/Backups/designer-wip-$stamp"
            Backup-GateDesignerFiles $repoDir $backupDir $dirty
            $stages.Backup = "$($dirty.Count) file(s) verified at $backupDir"
        }

        $stage = 'Merge'
        if (@($changes | Where-Object { $_.Status[0] -ne ' ' -or $_.Status[1] -eq 'U' }).Count) {
            throw 'The index contains staged or unresolved changes. The merge has not started.'
        }
        $mergeHead = git rev-parse --git-path MERGE_HEAD
        if ($LASTEXITCODE -ne 0 -or (Test-Path -LiteralPath $mergeHead)) { throw 'An earlier merge is still in progress.' }
        if ($Preview) {
            $paths = @(
                foreach ($tip in $tips) { Get-GateDiffPaths $head $tip.Commit -Preview }
            )
            $paths = @($paths | Sort-Object -Unique)
            $stages.Merge = "would merge $($names -join ', ') (diffs from each merge base)"
        } else {
            foreach ($tip in $tips) {
                git merge --no-ff --no-edit -m "Merge $($tip.Name)" -- $tip.Commit | Out-Host
                if ($LASTEXITCODE -ne 0) { throw "Merge stopped at $($tip.Name). Resolve the reported error or conflicts before proceeding." }
            }
            $paths = @(Get-GateDiffPaths $head HEAD)
            $stages.Merge = "merged $($names -join ', ')"
        }
        . (Join-Path $scriptsDir 'verify_lib.ps1')
        $plan = Resolve-Impact $paths (Get-TestMap)
        $testLabel = if ($RunFull) { 'full automation suite' } else { "Smart (base $head, local smokes off)" }
        $smokeNames = @($plan.Smokes | ForEach-Object { "CodexTactics.$_" })
        if ($Preview) {
            $stages.Build = if (Test-GateNeedsBuild $paths) { 'would build' } else { 'skip: no Source/ or .uproject changes' }
            $stages.Tests = "would run $testLabel"
            $dirty | ForEach-Object { Write-Host "  backup: $_" }
            $paths | ForEach-Object { Write-Host "  changed: $_" }
            $stages['Worker smokes'] = if ($smokeNames.Count) { "would run $($smokeNames -join ', ')" } else { 'none mapped' }
            $code = 0
        } else {
            $stage = 'Build'
            if (Test-GateNeedsBuild $paths) {
                powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $scriptsDir 'build.ps1') | Out-Host
                if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)." }
                $stages.Build = 'PASS'
            } else {
                $stages.Build = 'skip: no Source/ or .uproject changes'
            }

            $stage = 'Tests'
            $testArgs = if ($RunFull) { @('-Filter', 'CodexTactics') } else { @('-Smart', '-SkipSmokes', '-Base', $head) }
            $started = [DateTime]::UtcNow
            $testLog = Join-Path $repoDir 'Saved/Logs/AutomationTests.log'
            powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $scriptsDir 'test.ps1') @testArgs | Out-Host
            $testExit = $LASTEXITCODE
            $stages.Tests = "PASS ($testLabel)"
            if ($testExit -ne 0) {
                $latest = Get-Item -LiteralPath $testLog -ErrorAction SilentlyContinue
                if (-not $latest -or $latest.LastWriteTimeUtc -lt $started) { throw "Tests failed without a fresh automation log (exit $testExit)." }
                $evidence = Get-GateAutomationFailures (Get-Content -LiteralPath $testLog) $dirty
                if ($evidence.Known.Count) { Write-Host "Known X6 WIP failures: $($evidence.Known -join ', ')" }
                if (-not $evidence.Complete -or -not $evidence.Known.Count -or $evidence.Unexpected.Count) {
                    throw "Unexpected or incomplete automation result (exit $testExit). $($evidence.Unexpected -join ', ')"
                }
                $stages.Tests = "PASS with $($evidence.Known.Count) known X6 WIP failure(s) (see log)"
            }

            $stage = 'Worker smokes'
            if ($smokeNames.Count) {
                $resultDir = Join-Path $repoDir "Saved/Logs/Worker/merge-gate-$stamp"
                powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $toolsDir 'worker_sync.ps1') `
                    -RemoteRoot C:/Work/CodexTactics-slot1 -ShipHead -ShareMachine -Jobs 1 -Commands ($smokeNames -join ',') -ResultDir $resultDir | Out-Host
                if ($LASTEXITCODE -ne 0) { throw "Worker smoke command failed (exit $LASTEXITCODE)." }
                $results = (Get-Content -LiteralPath (Join-Path $resultDir 'SmokeParallel-worker.json') -Raw | ConvertFrom-Json).results
                $summary = @($results | ForEach-Object { "$($_.Name)=$($_.State)" })
                $expected = @($plan.Smokes | Sort-Object)
                $actual = @($results | ForEach-Object { $_.Name } | Sort-Object)
                if ($actual.Count -ne $expected.Count -or (Compare-Object $expected $actual) -or
                    @($results | Where-Object State -notin @('PASS', 'PASS*', 'KNOWN-FAIL')).Count) {
                    throw "Worker returned missing, unexpected or unsuccessful smoke results: $($summary -join ', ')"
                }
                $stages['Worker smokes'] = $summary -join ', '
            } else {
                $stages['Worker smokes'] = 'none mapped'
            }
            $code = 0
        }
    } catch {
        $stages[$stage] = "FAIL: $($_.Exception.Message)"
    } finally {
        if ($ownsLock) { Exit-AgentLock }
        Pop-Location
        [Console]::OutputEncoding = $previousEncoding
        Show-GateSummary $stages
    }
    if ($code -eq 0 -and -not $Preview) { Write-Host 'ready to push' }
    return $code
}

if ($MyInvocation.InvocationName -ne '.') {
    exit (Invoke-MergeGate $Branches -Preview:$DryRun -RunFull:$FullTests)
}
