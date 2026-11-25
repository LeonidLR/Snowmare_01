# Lead merge gate. Run from the main checkout after review. DryRun only plans the merge.
# Resume a red gate on its recorded merged HEAD with -Resume. Exit 2 means only confirmed pre-existing smokes remain.
# Example: powershell -ExecutionPolicy Bypass -File Scripts/Tools/merge_gate.ps1 -Branches 'codex/054-traversal-smoke-goals,codex/057-guards' -DryRun
param(
    [Parameter(Position=0)][string[]]$Branches,
    [switch]$DryRun,
    [switch]$FullTests,
    [switch]$Resume,
    [switch]$AcceptPreExisting,
    [switch]$AllowNoWorkerSmokes,
    [int]$Jobs = 0
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

function Backup-GateDesignerFiles([string]$RepoDir, [string]$BackupDir, [string[]]$Paths, [string[]]$Untracked = @(), [string[]]$Deleted = @()) {
    $null = New-Item -ItemType Directory -Path $BackupDir -Force
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
    $manifest = [pscustomobject]@{ Tracked = $entries; Deleted = $Deleted; Untracked = $Untracked }
    Write-GateState (Join-Path $BackupDir 'Manifest.json') $manifest
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

function Assert-GateNoIgnoredOverlap([string[]]$Changed) {
    $checked = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $Changed) {
        $path = $file
        while ($path) {
            $exists = if ($path -eq $file) { Test-Path -LiteralPath $path } else { Test-Path -LiteralPath $path -PathType Leaf }
            if ($exists -and $checked.Add($path)) {
                $ignored = @(git ls-files --others --ignored --exclude-standard -- ":(literal)$path")
                if ($LASTEXITCODE -ne 0) { throw "Could not check ignored paths at $path." }
                if ($ignored.Count) { throw "Merge overlaps an ignored local path at $path." }
            }
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
            Assert-GateNoIgnoredOverlap $stepPaths
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
    $rows = @((Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json).results)
    $summary = @($rows | ForEach-Object { "$($_.Name)=$($_.State)" }) -join ', '
    Write-Host "Worker smoke results (exit $ExitCode): $summary"
    $names = @($rows | ForEach-Object Name | Sort-Object)
    if ($names.Count -ne $Expected.Count -or (Compare-Object ($Expected | Sort-Object) $names) -or
        @($rows | Where-Object State -notin @('PASS','PASS*','KNOWN-FAIL','FAIL')).Count) {
        throw "Worker returned incomplete or unexpected results (exit $ExitCode): $summary"
    }
    return $rows
}


function Get-GateJobs([int]$Requested, [datetime]$Now = (Get-Date)) {
    if ($Requested -lt 0) { throw '-Jobs must be nonnegative.' }
    if ($Requested) { return $Requested }
    if ($Now.TimeOfDay -ge [TimeSpan]::FromHours(8) -and $Now.TimeOfDay -lt [TimeSpan]::FromHours(16.5)) { return 2 }
    return 1
}

function Start-GateWorker([string]$ScriptPath, [string]$Directory, [string[]]$Names, [int]$Jobs,
    [string]$ShipRef, [switch]$Build, [switch]$SkipSmoke) {
    $options = @('-RemoteRoot', 'C:/Work/CodexTactics-slot1', '-ShareMachine', '-Jobs', $Jobs, '-ShipRef', $ShipRef, '-ResultDir', $Directory)
    if ($Build) { $options += '-Build' }
    if ($SkipSmoke) { $options += '-SkipSmoke' }
    else { $options += @('-Commands', (($Names | ForEach-Object { "CodexTactics.$_" }) -join ','), '-AllLogs') }

    $settings = @{ Script = $ScriptPath; Options = $options }
    return Start-Job -ArgumentList (,$settings) -ScriptBlock {
        param($Settings)
        $workerArgs = $Settings.Options
        & powershell -NoProfile -ExecutionPolicy Bypass -File $Settings.Script @workerArgs
        [int]$LASTEXITCODE
    }
}

function Receive-GateWorker($Job) {
    try {
        $output = @(Receive-Job -Job $Job -Wait)
        if (-not $output.Count -or $output[-1] -isnot [int]) { throw 'Worker job did not report an exit code.' }
        $output | Select-Object -SkipLast 1 | Out-Host
        return [int]@($output)[-1]
    } finally {
        Remove-Job -Job $Job
    }
}

function Assert-GateWorkerVerdicts([object[]]$Rows, [string]$Directory, [int]$ExitCode) {
    $failed = @($Rows | Where-Object State -eq 'FAIL')
    $known = @($Rows | Where-Object State -eq 'KNOWN-FAIL')
    if ($ExitCode -notin @(0,1) -or ($ExitCode -eq 1 -and -not ($failed.Count -or $known.Count))) {
        throw "Worker smoke infrastructure failure (exit $ExitCode): $Directory"
    }
    foreach ($row in @($failed) + @($known)) {
        $log = if ($row.Log) { Join-Path $Directory ([IO.Path]::GetFileName($row.Log)) }
        # Worker FAIL can also mean that Unreal did not start. That must stay red even on the baseline.
        if (-not $log -or -not (Test-Path -LiteralPath $log) -or
            -not (Select-String -LiteralPath $log -Pattern 'Smoke RESULT: FAIL' -SimpleMatch -Quiet)) {
            throw "$($row.Name)=$($row.State) has no completed smoke failure in its log. Worker runtime or result collection failed: $Directory"
        }
    }
}

function Test-GateNeedsBuild([string[]]$Paths) {
    return [bool]($Paths -match '^Source/|\.uproject$')
}


function Test-GateDesignerFiles([string]$RepoDir, [string]$BackupDir) {
    $manifestFile = Join-Path $BackupDir 'Manifest.json'
    $manifest = Get-Content -LiteralPath $manifestFile -Raw -Encoding UTF8 | ConvertFrom-Json
    $intact = 0
    $changed = @(
        foreach ($entry in $manifest.Tracked) {
            $original = Join-Path $RepoDir $entry.Path
            $backup = Join-Path $BackupDir $entry.Path
            if ((Test-Path -LiteralPath $original -PathType Leaf) -and (Test-Path -LiteralPath $backup -PathType Leaf) -and
                (Get-GateFileSha256 $original) -eq $entry.SHA256 -and (Get-GateFileSha256 $backup) -eq $entry.SHA256) {
                $intact++
            } else {
                $entry.Path
            }
        }
        foreach ($path in $manifest.Deleted) {
            if (Test-Path -LiteralPath (Join-Path $RepoDir $path)) { $path }
        }
    )
    $label = "designer files intact $intact/$(@($manifest.Tracked).Count)"
    if ($changed.Count) { throw "$label. Changed or missing: $($changed -join ', '). Backup: $BackupDir" }
    return $label
}

function Write-GateProgress($Timing, [string]$Stage) {
    try {
        if (-not $Timing -or $Timing.Preview -or -not $Timing.RepoDir) { return }
        $logDir = Join-Path $Timing.RepoDir 'Saved/Logs'
        $progressPath = Join-Path $logDir 'merge-gate-progress.json'
        if (-not $Stage) {
            if (Test-Path -LiteralPath $progressPath) { Remove-Item -LiteralPath $progressPath -Force -ErrorAction SilentlyContinue }
            return
        }
        $order = @('Editor', 'Backup', 'Merge', 'Build', 'Tests', 'Worker smokes', 'Designer files')
        $idx = [Array]::IndexOf($order, $Stage)
        $now = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
        if (-not (Test-Path -LiteralPath $logDir)) { $null = New-Item -ItemType Directory -Path $logDir -Force }
        $payload = [ordered]@{
            stage = $Stage
            stage_index = if ($idx -ge 0) { $idx + 1 } else { 1 }
            stage_count = $order.Count
            stage_started = $now
            gate_started = if ($Timing.GateStarted) { [int64]$Timing.GateStarted } else { $now }
            updated = $now
        }
        $payload | ConvertTo-Json -Compress | Set-Content -LiteralPath $progressPath -Encoding UTF8
    } catch { }
}

function Write-GateHistory($RepoDir, [int]$ExitCode, $Seconds) {
    try {
        if (-not $RepoDir) { return }
        $logDir = Join-Path $RepoDir 'Saved/Logs'
        if (-not (Test-Path -LiteralPath $logDir)) { $null = New-Item -ItemType Directory -Path $logDir -Force }
        $histPath = Join-Path $logDir 'merge-gate-history.jsonl'
        $rec = [ordered]@{
            at = (Get-Date -Format 's')
            code = $ExitCode
            stages = $Seconds
        }
        Add-Content -LiteralPath $histPath -Encoding UTF8 ($rec | ConvertTo-Json -Compress)
    } catch { }
}

function Set-GateStage($Timing, [string]$Next) {
    if ($Timing.Stage) { $Timing.Seconds[$Timing.Stage] += $Timing.Clock.Elapsed.TotalSeconds }
    $Timing.Stage = $Next
    $Timing.Clock.Restart()
    Write-GateProgress $Timing $Next
}

function Show-GateSummary($Stages, $Seconds) {
    $Stages.GetEnumerator() | ForEach-Object { Write-Host ('{0}: {1} ({2:N1}s)' -f $_.Key, $_.Value, [double]$Seconds[$_.Key]) }
}

function Assert-GateWorkerSmokePlan([string[]]$Paths, $Plan, $Map, [switch]$AllowNoWorkerSmokes) {
    $reg = @($Map.smokeRegistry)
    foreach ($s in @($Plan.Smokes)) {
        if ($reg -notcontains $s) {
            throw "Mapped worker smoke '$s' is not in Scripts/test_map.json smokeRegistry (archived map?)."
        }
    }
    $cppChanged = @($Paths | Where-Object { $_ -match '^Source/(?!CodexTacticsTests/)' })
    if ($cppChanged.Count -gt 0 -and @($Plan.Smokes).Count -eq 0 -and -not $AllowNoWorkerSmokes) {
        throw ("C++ Source/ changed ({0}) but 0 worker smokes were mapped. Update Scripts/test_map.json or pass -AllowNoWorkerSmokes." -f ($cppChanged[0..([Math]::Min(2, $cppChanged.Count - 1))] -join ', '))
    }
}

function Show-GateSmokeReasons($Plan) {
    foreach ($name in $Plan.Smokes) {
        foreach ($row in @($Plan.Rows | Where-Object { $_.SmokeNames -contains $name })) {
            $rules = if ($row.Rules) { $row.Rules } elseif ($Plan.Unmapped -contains $row.File) { 'fallback' } else { 'registered smoke' }
            Write-Host "smoke selector: $name <- $($row.File) [$rules]"
        }
    }
}

function Invoke-MergeGate([string[]]$BranchNames, [switch]$Preview, [switch]$RunFull, [switch]$ResumeRun, [switch]$AllowPreExisting, [switch]$AllowNoWorkerSmokes, [int]$RequestedJobs = 0) {
    $ErrorActionPreference = 'Stop'
    $toolsDir = $PSScriptRoot
    $scriptsDir = Split-Path $toolsDir -Parent
    $repoDir = Split-Path $scriptsDir -Parent
    $stages = [ordered]@{ Editor = 'pending'; Backup = 'pending'; Merge = 'pending'; Build = 'pending'; Tests = 'pending'; 'Worker smokes' = 'pending'; 'Designer files' = 'pending' }
    $timing = @{ Stage = ''; Seconds = @{}; Clock = [Diagnostics.Stopwatch]::new(); RepoDir = $repoDir; GateStarted = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds(); Preview = [bool]$Preview }
    Set-GateStage $timing 'Editor'
    $ownsLock = $false
    $code = 1
    $state = $null
    $head = $null
    $testLog = Join-Path $repoDir 'Saved/Logs/AutomationTests.log'
    $buildLog = Join-Path $repoDir 'Saved/Logs/Build.log'
    $resultDir = $null
    $baselineDir = $null
    $workerJob = $null
    $workerWall = $null
    $backupComplete = $false
    $designerChecked = $false
    $statePath = Join-Path $repoDir 'Saved/Logs/merge-gate-state.json'
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    $previousEncoding = [Console]::OutputEncoding
    Push-Location -LiteralPath $repoDir
    try {
        [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
        $names = @($BranchNames -split '[,\s]+' | Where-Object { $_ })
        if ($Preview -and $ResumeRun) { throw '-DryRun and -Resume cannot be combined.' }
        if (-not $ResumeRun -and -not $names.Count) { throw 'Specify at least one branch with -Branches.' }
        $workerWidth = Get-GateJobs $RequestedJobs
        $project = Join-Path $repoDir 'CodexTactics.uproject'
        Assert-GateEditorClosed $project
        $stages.Editor = 'clear'

        if (-not $Preview) {
            . (Join-Path $scriptsDir 'agent_lock.ps1')
            $ownsLock = Enter-AgentLock 'Lead merge gate'
            if ((git branch --show-current) -ne 'main') { throw 'Run the merge gate on main. Use -DryRun to preview from another branch.' }
        }
        Set-GateStage $timing 'Merge'
        $currentHead = git rev-parse --verify HEAD
        if ($LASTEXITCODE -ne 0) { throw 'Could not resolve HEAD.' }
        $mergeHead = git rev-parse --git-path MERGE_HEAD
        if ($LASTEXITCODE -ne 0 -or (Test-Path -LiteralPath $mergeHead)) { throw 'An earlier merge is still in progress.' }
        $status = git --no-optional-locks status --porcelain=v1 -z --untracked-files=all
        if ($LASTEXITCODE -ne 0) { throw 'Could not read designer changes.' }
        $changes = @(ConvertFrom-GateGitStatus ($status -join [Environment]::NewLine))
        $tracked = @($changes | Where-Object Status -ne '??')
        $dirty = @($tracked | Where-Object Status -in @(' M',' T') | ForEach-Object Path)
        $deleted = @($tracked | Where-Object Status -eq ' D' | ForEach-Object Path)
        $untracked = @($changes | Where-Object Status -eq '??' | ForEach-Object Path)
        if (@($tracked | Where-Object { $_.Status[0] -ne ' ' -or $_.Status[1] -eq 'U' }).Count) {
            throw 'The index contains staged or unresolved changes. The merge has not started.'
        }
        $saved = if (-not $Preview -and (Test-Path -LiteralPath $statePath)) {
            Get-Content -LiteralPath $statePath -Raw -Encoding UTF8 | ConvertFrom-Json
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
            $backupComplete = $true
            $RunFull = $RunFull -or $state.FullTests
            $state.FullTests = [bool]$RunFull
            Write-GateState $statePath $state
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
            Set-GateStage $timing 'Backup'
            $backupDir = Join-Path $repoDir "Saved/Backups/designer-wip-$stamp"
            if ($Preview) {
                $stages.Backup = "would verify SHA256 for $($dirty.Count) file(s)"
                $stages.Merge = "would merge $($names -join ', ') (all branches preflighted)"
            } else {
                Backup-GateDesignerFiles $repoDir $backupDir $dirty $untracked $deleted
                $backupComplete = $true
                $stages.Backup = "$($dirty.Count) tracked file(s) verified, $($untracked.Count) untracked listed at $backupDir"
                Set-GateStage $timing 'Merge'
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
        $testMap = Get-TestMap
        $plan = Resolve-Impact $paths $testMap
        Show-GateSmokeReasons $plan
        Assert-GateWorkerSmokePlan $paths $plan $testMap -AllowNoWorkerSmokes:$AllowNoWorkerSmokes
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
            Set-GateStage $timing 'Build'
            Assert-GateEditorClosed $project
            if (Test-GateNeedsBuild $paths) {
                # Designer 2026-11-25: compile on the worker (build_remote copies the DLLs back); local UBT only if the worker is unreachable (exit 2).
                powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $scriptsDir 'build.ps1') -Remote worker | Out-Host
                if ($LASTEXITCODE -eq 2) {
                    Write-Host '[merge_gate] remote build unavailable (exit 2) - building locally'
                    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $scriptsDir 'build.ps1') | Out-Host
                }
                if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)." }
                $stages.Build = 'PASS'
            } else {
                $stages.Build = 'skip: no Source/ or .uproject changes'
            }


            $workerScript = Join-Path $toolsDir 'worker_sync.ps1'
            if ($plan.Smokes.Count) {
                $resultDir = Join-Path $repoDir "Saved/Logs/Worker/merge-gate-$stamp"
                $smokeStarted = [DateTime]::UtcNow
                $workerJob = Start-GateWorker $workerScript $resultDir $plan.Smokes $workerWidth $state.MergedHead
            }

            Set-GateStage $timing 'Tests'
            $testFailed = $false
            try {
                $testArgs = if ($RunFull) { @('-Filter', 'CodexTactics') } else { @('-Smart', '-SkipSmokes', '-Base', $head) }
                $started = [DateTime]::UtcNow
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
            } catch {
                $testFailed = $true
                $stages.Tests = "FAIL: $($_.Exception.Message)"
            }

            Set-GateStage $timing 'Worker smokes'
            $preExistingNeedsApproval = $false
            if ($workerJob) {
                try {
                    $workerExit = Receive-GateWorker $workerJob
                } finally {
                    $workerJob = $null
                }
                $rows = @(Read-GateWorkerRows $resultDir $smokeStarted $plan.Smokes $workerExit)
                Assert-GateWorkerVerdicts $rows $resultDir $workerExit
                $summary = @($rows | ForEach-Object { "$($_.Name)=$($_.State)" }) -join ', '
                $stages['Worker smokes'] = $summary
                $failed = @($rows | Where-Object State -eq 'FAIL' | ForEach-Object Name)
                if ($failed.Count) {
                    $baselineDir = "$resultDir-baseline"
                    $baselineStarted = [DateTime]::UtcNow
                    try {
                        $baselineJob = Start-GateWorker $workerScript $baselineDir $failed $workerWidth $head -Build
                        $baselineExit = Receive-GateWorker $baselineJob
                        $baseline = @(Read-GateWorkerRows $baselineDir $baselineStarted $failed $baselineExit)
                        Assert-GateWorkerVerdicts $baseline $baselineDir $baselineExit
                        $regressions = @($baseline | Where-Object State -in @('PASS','PASS*') | ForEach-Object Name)
                        $preExisting = @($baseline | Where-Object State -in @('FAIL','KNOWN-FAIL') | ForEach-Object Name)
                        if ($preExisting.Count) { Write-Host "PRE-EXISTING at $($head): $($preExisting -join ', ')" }
                    } finally {
                        $restoreJob = Start-GateWorker $workerScript "$resultDir-restore" @() $workerWidth $state.MergedHead -SkipSmoke
                        if ((Receive-GateWorker $restoreJob) -ne 0) { throw 'Could not restore candidate source and binaries on the worker after the baseline.' }
                    }
                    if ($regressions.Count) { throw "New smoke failures: $($regressions -join ', '). Results: $summary" }
                    $preExistingNeedsApproval = -not $AllowPreExisting
                    $decision = if ($preExistingNeedsApproval) { 'requires -AcceptPreExisting' } else { 'accepted by -AcceptPreExisting' }
                    $stages['Worker smokes'] = "PRE-EXISTING ($decision): $($preExisting -join ', ')"
                }
                $workerWall = ([DateTime]::UtcNow - $smokeStarted).TotalSeconds
            } else {
                $stages['Worker smokes'] = 'none mapped'
            }

            Set-GateStage $timing 'Designer files'
            $designerChecked = $true
            $stages['Designer files'] = Test-GateDesignerFiles $repoDir $state.BackupDir
            if ((git rev-parse --verify HEAD) -ne $state.MergedHead) { throw 'HEAD changed during verification.' }
            if ($testFailed) { $code = 1 }
            elseif ($preExistingNeedsApproval) { $code = 2 }
            else {
                $state.Status = 'passed'
                Write-GateState $statePath $state
                $code = 0
            }
        }
    } catch {
        $stages[$timing.Stage] = "FAIL: $($_.Exception.Message)"
    } finally {
        if ($workerJob) {
            try { $null = Receive-GateWorker $workerJob }
            catch { Write-Host "Worker cleanup: $($_.Exception.Message)" }
        }
        if ($backupComplete -and -not $designerChecked -and $state) {
            Set-GateStage $timing 'Designer files'
            try {
                $stages['Designer files'] = Test-GateDesignerFiles $repoDir $state.BackupDir
            } catch {
                $stages['Designer files'] = "FAIL: $($_.Exception.Message)"
                $code = 1
            }
        }
        Set-GateStage $timing ''
        if (-not $Preview) { Write-GateHistory $repoDir $code $timing.Seconds }
        if ($head) { Write-Host "Pre-merge SHA: $head" }
        if (-not $Preview) {
            Write-Host "Build log: $buildLog"
            Write-Host "Automation log: $testLog"
            if ($resultDir) { Write-Host "Worker result directory: $resultDir" }
            if ($baselineDir) { Write-Host "Baseline result directory: $baselineDir" }
            if ($workerWall) { Write-Host ('Worker elapsed: {0:N1}s (includes overlap with local tests)' -f $workerWall) }
        }
        if ($code -ne 0 -and -not $Preview -and $state) {
            if ($state.Status -eq 'verifying') { Write-Host 'After fixing the failure, run merge_gate.ps1 -Resume.' }
            Write-Host "To discard the merge while keeping local changes: git reset --keep $head"
        }
        if ($ownsLock) { Exit-AgentLock }
        Pop-Location
        [Console]::OutputEncoding = $previousEncoding
        Show-GateSummary $stages $timing.Seconds
    }
    if ($code -eq 0 -and -not $Preview) { Write-Host 'ready to push' }
    return $code
}

if ($MyInvocation.InvocationName -ne '.') {
    exit (Invoke-MergeGate $Branches -Preview:$DryRun -RunFull:$FullTests -ResumeRun:$Resume -AllowPreExisting:$AcceptPreExisting -AllowNoWorkerSmokes:$AllowNoWorkerSmokes -RequestedJobs $Jobs)
}
