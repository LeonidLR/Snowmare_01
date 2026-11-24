# Lead merge gate. Run from the main checkout after review. DryRun only plans the merge.
# Example: powershell -ExecutionPolicy Bypass -File Scripts/Tools/merge_gate.ps1 -Branches 'codex/054-traversal-smoke-goals,codex/057-guards' -DryRun
param(
    [Parameter(Position=0)][string[]]$Branches,
    [switch]$DryRun,
    [switch]$FullTests,
    [switch]$Resume
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

function Get-GateDiffPaths([string]$From, [string]$To) {
    $range = "$From..$To"
    $raw = git diff --no-renames --name-only -z $range --
    if ($LASTEXITCODE -ne 0) { throw "Could not read changed paths for $range" }
    return ($raw -join [Environment]::NewLine).Split([char]0) | Where-Object { $_ }
}


function Assert-GateEditorClosed([string]$Project) {
    foreach ($editor in @(Get-CimInstance Win32_Process -Filter "Name = 'UnrealEditor.exe'")) {
        if (-not $editor.CommandLine) { throw "Cannot read CommandLine for Unreal Editor PID $($editor.ProcessId)." }
        if (Test-GateEditorCommandLine $editor.CommandLine $Project) {
            throw "The designer's Unreal Editor is open for this checkout (PID $($editor.ProcessId))."
        }
    }
}

function Assert-GateNoPathOverlap([string[]]$Changed, [string[]]$Protected) {
    $files = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $parents = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($path in $Protected) {
        $file = $path.TrimEnd('/')
        $null = $files.Add($file)
        while ($file.Contains('/')) {
            $file = $file.Substring(0, $file.LastIndexOf('/'))
            if (-not $parents.Add($file)) { break }
        }
    }
    foreach ($file in $Changed) {
        if ($parents.Contains($file)) { throw "Merge would replace a directory containing designer changes: $file" }
        $path = $file
        while ($path) {
            if ($files.Contains($path)) { throw "Merge overlaps a designer path: $path (changed: $file)." }
            $slash = $path.LastIndexOf('/')
            if ($slash -lt 0) { break }
            $path = $path.Substring(0, $slash)
        }
    }
}

function Get-GateMergePlan([string]$Head, [object[]]$Tips, [string[]]$ProtectedPaths) {
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    $objectRoot = Join-Path $tempRoot ('codex-gate-objects-' + [Guid]::NewGuid().ToString('N'))
    $previousObjects = $env:GIT_OBJECT_DIRECTORY
    $previousAlternates = $env:GIT_ALTERNATE_OBJECT_DIRECTORIES
    $currentObjects = git rev-parse --path-format=absolute --git-path objects
    if ($LASTEXITCODE -ne 0) { throw 'Could not resolve the repository object directory.' }

    # Hold synthetic trees and commits outside the checkout's Git object store, even on -DryRun.
    $null = New-Item -ItemType Directory -Path $objectRoot
    try {
        $env:GIT_ALTERNATE_OBJECT_DIRECTORIES = (@($currentObjects, $previousAlternates) | Where-Object { $_ }) -join [IO.Path]::PathSeparator
        $env:GIT_OBJECT_DIRECTORY = $objectRoot
        $synthetic = $Head
        foreach ($tip in $Tips) {
            git merge-base --is-ancestor $tip.Commit $synthetic
            $ancestorExit = $LASTEXITCODE
            if ($ancestorExit -eq 0) { throw "Branch $($tip.Name) is already in HEAD or an earlier planned merge." }
            if ($ancestorExit -ne 1) { throw "Could not check ancestry of $($tip.Name)." }

            $merge = @(git merge-tree --write-tree $synthetic $tip.Commit)
            if ($LASTEXITCODE -ne 0) {
                $details = @($merge | Where-Object { $_ -match 'CONFLICT' }) -join ', '
                throw "Preflight conflict at $($tip.Name). HEAD unchanged. $details"
            }
            $tree = $merge[0]
            if ($tree -notmatch '^[0-9a-f]{40,64}$') { throw "Unexpected merge-tree output for $($tip.Name)." }
            $stepPaths = @(Get-GateDiffPaths $synthetic $tree)
            if (-not $stepPaths.Count) { throw "Branch $($tip.Name) produces an empty merge diff." }
            Assert-GateNoPathOverlap $stepPaths $ProtectedPaths
            $synthetic = git -c user.name=MergeGate -c user.email=mergegate@example.invalid -c commit.gpgsign=false commit-tree $tree -p $synthetic -p $tip.Commit -m 'Merge gate preflight'
            if ($LASTEXITCODE -ne 0) { throw "Could not simulate merge of $($tip.Name)." }
        }
        $paths = @(Get-GateDiffPaths $Head $synthetic)
        if (-not $paths.Count) { throw 'The combined merge diff is empty.' }
        return [pscustomobject]@{ Paths = $paths; Tree = $tree }
    } finally {
        $env:GIT_OBJECT_DIRECTORY = $previousObjects
        $env:GIT_ALTERNATE_OBJECT_DIRECTORIES = $previousAlternates
        $resolved = (Resolve-Path -LiteralPath $objectRoot).ProviderPath
        if ($resolved.StartsWith((Join-Path $tempRoot 'codex-gate-objects-'), [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
}

function Write-GateState([string]$Path, $State) {
    $null = New-Item -ItemType Directory -Path (Split-Path $Path -Parent) -Force
    $temp = "$Path.$PID.tmp"
    $json = ConvertTo-Json -Depth 8 -InputObject $State
    try {
        [IO.File]::WriteAllText($temp, $json, [Text.UTF8Encoding]::new($false))
        if (Test-Path -LiteralPath $Path) {
            $previous = "$temp.previous"
            [IO.File]::Replace($temp, $Path, $previous)
            Remove-Item -LiteralPath $previous
        } else {
            [IO.File]::Move($temp, $Path)
        }
    } finally {
        if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp }
    }
}

function Read-GateWorkerRows([string]$Directory, [datetime]$Started, [string[]]$Expected, [int]$ExitCode) {
    $path = Join-Path $Directory 'SmokeParallel-worker.json'
    $file = Get-Item -LiteralPath $path -ErrorAction SilentlyContinue
    if (-not $file -or $file.LastWriteTimeUtc -lt $Started) {
        throw "Worker did not return a fresh smoke result (exit $ExitCode): $path"
    }
    $rows = @((Get-Content -LiteralPath $path -Raw | ConvertFrom-Json).results)
    $summary = @($rows | ForEach-Object { "$($_.Name)=$($_.State)" }) -join ', '
    Write-Host "Worker smoke results (exit $ExitCode): $summary"
    $names = @($rows | ForEach-Object Name | Sort-Object)
    if ($names.Count -ne $Expected.Count -or (Compare-Object ($Expected | Sort-Object) $names) -or
        @($rows | Where-Object State -notin @('PASS','PASS*','KNOWN-FAIL','FAIL')).Count) {
        throw "Worker returned incomplete or unexpected results (exit $ExitCode): $summary"
    }
    return $rows
}

function Test-GateNeedsBuild([string[]]$Paths) {
    return [bool]($Paths -match '^Source/|\.uproject$')
}

function Show-GateSummary($Stages) {
    $Stages.GetEnumerator() | ForEach-Object { Write-Host "$($_.Key): $($_.Value)" }
}

function Invoke-MergeGate([string[]]$BranchNames, [switch]$Preview, [switch]$RunFull, [switch]$ResumeRun) {
    $ErrorActionPreference = 'Stop'
    $toolsDir = $PSScriptRoot
    $scriptsDir = Split-Path $toolsDir -Parent
    $repoDir = Split-Path $scriptsDir -Parent
    $stages = [ordered]@{ Editor = 'pending'; Backup = 'pending'; Merge = 'pending'; Build = 'pending'; Tests = 'pending'; 'Worker smokes' = 'pending' }
    $stage = 'Editor'
    $ownsLock = $false
    $code = 1
    $state = $null
    $head = $null
    $statePath = Join-Path $repoDir 'Saved/Logs/merge-gate-state.json'
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    $previousEncoding = [Console]::OutputEncoding
    Push-Location -LiteralPath $repoDir
    try {
        [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
        $names = @($BranchNames -split '[,\s]+' | Where-Object { $_ })
        if ($Preview -and $ResumeRun) { throw '-DryRun and -Resume cannot be combined.' }
        if (-not $ResumeRun -and -not $names.Count) { throw 'Specify at least one branch with -Branches.' }
        $project = Join-Path $repoDir 'CodexTactics.uproject'
        Assert-GateEditorClosed $project
        $stages.Editor = 'clear'

        if (-not $Preview) {
            . (Join-Path $scriptsDir 'agent_lock.ps1')
            $ownsLock = Enter-AgentLock 'Lead merge gate'
            if ((git branch --show-current) -ne 'main') { throw 'Run the merge gate on main. Use -DryRun to preview from another branch.' }
        }
        $stage = 'Merge'
        $currentHead = git rev-parse --verify HEAD
        if ($LASTEXITCODE -ne 0) { throw 'Could not resolve HEAD.' }
        $mergeHead = git rev-parse --git-path MERGE_HEAD
        if ($LASTEXITCODE -ne 0 -or (Test-Path -LiteralPath $mergeHead)) { throw 'An earlier merge is still in progress.' }
        $status = git --no-optional-locks status --porcelain=v1 -z --untracked-files=all
        if ($LASTEXITCODE -ne 0) { throw 'Could not read designer changes.' }
        $changes = @(ConvertFrom-GateGitStatus ($status -join [Environment]::NewLine))
        $tracked = @($changes | Where-Object Status -ne '??')
        $dirty = @($tracked | Where-Object Status -eq ' M' | ForEach-Object Path)
        if (@($tracked | Where-Object { $_.Status[0] -ne ' ' -or $_.Status[1] -eq 'U' }).Count) {
            throw 'The index contains staged or unresolved changes. The merge has not started.'
        }
        $saved = if (-not $Preview -and (Test-Path -LiteralPath $statePath)) {
            Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
        }

        if ($ResumeRun) {
            if (-not $saved -or $saved.Status -ne 'verifying' -or $saved.MergedHead -ne $currentHead -or
                $saved.PreMerge -notmatch '^[0-9a-f]{40,64}$') {
                throw "No resumable verification for current HEAD. State: $statePath"
            }
            if ($names.Count -and (($names -join ',') -ne (@($saved.Tips | ForEach-Object Name) -join ','))) {
                throw 'The supplied branches do not match the saved merge.'
            }
            $state = $saved
            $head = $state.PreMerge
            $paths = @(Get-GateDiffPaths $head $currentHead)
            if (-not $paths.Count) { throw 'The saved merge diff is empty.' }
            $stages.Merge = "resumed verification of $currentHead"
            $stages.Backup = "stored designer backup: $($state.BackupDir)"
            $RunFull = $RunFull -or $state.FullTests
        } else {
            if ($saved -and $saved.Status -ne 'passed' -and $saved.PreMerge -ne $currentHead) {
                throw "A previous merge has not passed verification. Use -Resume on its merged HEAD or recover to $($saved.PreMerge)."
            }
            $head = $currentHead
            $tips = @(
                foreach ($name in $names) {
                    $tip = git rev-parse --verify --end-of-options "$name^{commit}"
                    if ($LASTEXITCODE -ne 0) { throw "Cannot resolve branch $name." }
                    [pscustomobject]@{ Name = $name; Commit = $tip }
                }
            )
            $mergePlan = Get-GateMergePlan $head $tips @($changes | ForEach-Object Path)
            $paths = @($mergePlan.Paths)
            $stage = 'Backup'
            $backupDir = Join-Path $repoDir "Saved/Backups/designer-wip-$stamp"
            if ($Preview) {
                $stages.Backup = "would verify SHA256 for $($dirty.Count) file(s)"
                $stages.Merge = "would merge $($names -join ', ') (all branches preflighted)"
            } else {
                if ($dirty.Count) { Backup-GateDesignerFiles $repoDir $backupDir $dirty }
                $stages.Backup = if ($dirty.Count) { "$($dirty.Count) file(s) verified at $backupDir" } else { 'no modified tracked files' }
                $stage = 'Merge'
                $state = [pscustomobject]@{ PreMerge = $head; MergedHead = ''; Status = 'merging'; Tips = $tips
                    FullTests = [bool]$RunFull; BackupDir = $backupDir }
                Write-GateState $statePath $state
                foreach ($tip in $tips) {
                    Assert-GateEditorClosed $project
                    try {
                        git merge --no-ff --no-edit --no-overwrite-ignore -m "Merge $($tip.Name)" -- $tip.Commit | Out-Host
                        if ($LASTEXITCODE -ne 0) { throw "Merge stopped at $($tip.Name) (exit $LASTEXITCODE)." }
                    } catch {
                        if (Test-Path -LiteralPath $mergeHead) {
                            git merge --abort | Out-Host
                            if ($LASTEXITCODE -ne 0) { throw "Merge stopped at $($tip.Name) and git merge --abort failed." }
                        }
                        throw
                    }
                }
                $mergedTree = git rev-parse 'HEAD^{tree}'
                if ($LASTEXITCODE -ne 0 -or $mergedTree -ne $mergePlan.Tree) { throw 'The merged tree differs from preflight.' }
                $state.MergedHead = git rev-parse --verify HEAD
                if ($LASTEXITCODE -ne 0) { throw 'Could not resolve merged HEAD.' }
                $state.Status = 'verifying'
                Write-GateState $statePath $state
                $stages.Merge = "merged $($names -join ', ')"
            }
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
            Assert-GateEditorClosed $project
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
                $smokeStarted = [DateTime]::UtcNow
                powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $toolsDir 'worker_sync.ps1') `
                    -RemoteRoot C:/Work/CodexTactics-slot1 -ShipHead -ShareMachine -Jobs 1 -Commands ($smokeNames -join ',') -ResultDir $resultDir | Out-Host
                $workerExit = $LASTEXITCODE
                $rows = @(Read-GateWorkerRows $resultDir $smokeStarted $plan.Smokes $workerExit)
                $summary = @($rows | ForEach-Object { "$($_.Name)=$($_.State)" }) -join ', '
                $failed = @($rows | Where-Object State -eq 'FAIL')
                $known = @($rows | Where-Object State -eq 'KNOWN-FAIL')
                if ($failed.Count -or ($workerExit -ne 0 -and -not ($workerExit -eq 1 -and $known.Count))) {
                    throw "Worker smoke verification failed (exit $workerExit): $summary"
                }
                $stages['Worker smokes'] = $summary
            } else {
                $stages['Worker smokes'] = 'none mapped'
            }
            $state.Status = 'passed'
            Write-GateState $statePath $state
            $code = 0
        }
    } catch {
        $stages[$stage] = "FAIL: $($_.Exception.Message)"
    } finally {
        if ($code -ne 0 -and -not $Preview -and $state) {
            Write-Host "Pre-merge SHA: $head"
            if ($state.Status -eq 'verifying') { Write-Host 'After fixing the failure, run merge_gate.ps1 -Resume.' }
            Write-Host "To discard the merge while keeping local changes: git reset --keep $head"
        }
        if ($ownsLock) { Exit-AgentLock }
        Pop-Location
        [Console]::OutputEncoding = $previousEncoding
        Show-GateSummary $stages
    }
    if ($code -eq 0 -and -not $Preview) { Write-Host 'ready to push' }
    return $code
}

if ($MyInvocation.InvocationName -ne '.') {
    exit (Invoke-MergeGate $Branches -Preview:$DryRun -RunFull:$FullTests -ResumeRun:$Resume)
}
