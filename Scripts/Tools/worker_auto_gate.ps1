# Headless READY gate. Install this script outside the dedicated scratch checkout.
param(
    [string]$Repo = 'C:/Work/CodexTactics-auto-gate',
    [string]$WorkerRoot = 'C:/Work/CodexTactics',
    [string]$Slot1 = 'C:/Work/CodexTactics-slot1',
    [string]$ChatRoot,
    [string]$Python = 'python',
    [switch]$DryRun,
    [datetime]$PreviewAt = [datetime]::MinValue
)

function Test-AutoGateWindow([datetime]$Now) {
    return $Now.TimeOfDay -ge [TimeSpan]::FromHours(8) -and $Now.TimeOfDay -lt [TimeSpan]::FromHours(16.5)
}

function Get-AutoGateCandidates($Heads, $Messages) {
    $candidates = [ordered]@{}
    foreach ($message in $Messages) {
        if ($message.topic -eq 'gate' -and $message.from -in @('claude','codex','gemini','grok','designer') -and
            $message.text -cmatch '\AREADY[ \t]+([a-zA-Z0-9][a-zA-Z0-9._/-]*)[ \t]+([a-fA-F0-9]{7,64})[ \t]*\z') {
            $branch = $Matches[1]
            $expected = $Matches[2]
            if ($branch -ne 'main' -and $Heads.ContainsKey($branch) -and
                $Heads[$branch].StartsWith($expected, [StringComparison]::OrdinalIgnoreCase)) {
                $sha = $Heads[$branch]
                $candidates[$sha] = [pscustomobject]@{ Branch = $branch; Sha = $sha }
            }
        }
    }
    foreach ($branch in ($Heads.Keys | Sort-Object -CaseSensitive)) {
        $sha = $Heads[$branch]
        if ($branch -clike 'ready/*' -and -not $candidates.Contains($sha)) {
            $candidates[$sha] = [pscustomobject]@{ Branch = $branch; Sha = $sha }
        }
    }
    return $candidates.Values
}

function Invoke-AutoGit([string[]]$Argv) {
    $output = & git @Argv
    if ($LASTEXITCODE -ne 0) { throw "git $($Argv[0]) failed with exit $LASTEXITCODE." }
    return $output
}

function Test-AutoGateAdmission([string[]]$Roots, [datetime]$Now) {
    if (-not (Test-AutoGateWindow $Now)) { return $false }
    foreach ($root in $Roots) {
        $markerDir = Join-Path $root 'Saved/Worker'
        $freeze = Get-Item -LiteralPath (Join-Path $markerDir 'FROZEN') -ErrorAction SilentlyContinue
        if ($freeze -and $freeze.LastWriteTime -ge $Now.Date.AddHours(8.5)) { return $false }
        if (Get-ChildItem -LiteralPath $markerDir -Filter 'SYNC.*' -ErrorAction SilentlyContinue) { return $false }
    }
    return -not @(Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%'").Count
}

function New-AutoGateLock([string]$Root, [string]$Token) {
    $file = Join-Path $Root 'Saved/agent.lock'
    $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file))
    try { $stream = [IO.File]::Open($file, 'CreateNew', 'Write') }
    catch [IO.IOException] { return $null }
    try {
        $json = @{ agent = 'worker-auto-gate'; purpose = $Token; pid = $PID; manual = $false; started = (Get-Date -Format s) } |
            ConvertTo-Json -Compress
        $bytes = [Text.Encoding]::UTF8.GetBytes($json)
        $stream.Write($bytes, 0, $bytes.Length)
    } finally { $stream.Dispose() }
    return $file
}

function Remove-AutoGateLock([string]$File, [string]$Token) {
    if (-not (Test-Path -LiteralPath $File)) { return }
    $entry = Get-Content -LiteralPath $File -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($entry.pid -eq $PID -and $entry.purpose -eq $Token) { Remove-Item -LiteralPath $File }
}

function Invoke-AutoGateStage([string]$Script, [string[]]$Options, [string]$Log, [datetime]$Deadline) {
    if ((Get-Date) -ge $Deadline) { throw 'Daytime gate deadline reached.' }
    $quotedScript = "'" + $Script.Replace("'", "''") + "'"
    $quoted = @($quotedScript) + @($Options | ForEach-Object {
        if ($_ -cmatch '\A-[A-Za-z][A-Za-z0-9]*\z') { $_ } else { "'" + $_.Replace("'", "''") + "'" }
    })
    $command = '& ' + ($quoted -join ' ') + '; exit $LASTEXITCODE'
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    $process = Start-Process powershell.exe -WindowStyle Hidden -PassThru -WorkingDirectory (Get-Location).ProviderPath -ArgumentList @(
        '-NoProfile','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded
    ) -RedirectStandardOutput $Log -RedirectStandardError "$Log.err"
    try {
        $null = $process.Handle  # Retain the handle so ExitCode remains available after the process exits.
        while (-not $process.WaitForExit(1000)) {
            if ((Get-Date) -ge $Deadline) { throw 'Daytime gate deadline reached.' }
        }
        if ($null -eq $process.ExitCode) { throw 'Stage did not return an exit code.' }
        return [int]$process.ExitCode
    } finally {
        if (-not $process.HasExited) {
            & taskkill.exe /PID $process.Id /T /F | Out-Null
            if (-not $process.WaitForExit(10000)) { throw "Could not stop the gate process tree: PID $($process.Id)." }
        }
        $process.Dispose()
    }
}

function Invoke-AutoGate([string]$Scratch, [string]$Primary, [string]$OtherSlot, [string]$SharedChat, [string]$Py,
    [switch]$Preview, [datetime]$At = [datetime]::MinValue) {
    $ErrorActionPreference = 'Stop'
    [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
    if ($At -ne [datetime]::MinValue -and -not $Preview) { throw '-PreviewAt requires -DryRun.' }
    $now = if ($At -eq [datetime]::MinValue) { Get-Date } else { $At }
    if (-not (Test-AutoGateWindow $now)) { Write-Host 'Gate deferred: outside 08:00-16:30.'; return 0 }

    $Scratch = (Resolve-Path -LiteralPath $Scratch).ProviderPath
    $Primary = (Resolve-Path -LiteralPath $Primary).ProviderPath
    $OtherSlot = if ($OtherSlot -and (Test-Path -LiteralPath $OtherSlot)) { (Resolve-Path -LiteralPath $OtherSlot).ProviderPath }
    if ($Scratch -eq $Primary -or $Scratch -eq $OtherSlot -or -not (Test-Path -LiteralPath (Join-Path $Scratch '.git') -PathType Container)) {
        throw 'Use a separate, dedicated gate clone.'
    }
    if (-not $SharedChat -or -not (Test-Path -LiteralPath (Join-Path $SharedChat 'chat.jsonl') -PathType Leaf) -or
        (Split-Path $SharedChat.TrimEnd('/','\') -Leaf) -ne 'AgentChat') { throw 'Point -ChatRoot at the existing shared AgentChat directory.' }

    $roots = @($Scratch, $Primary, $OtherSlot | Where-Object { $_ } | Select-Object -Unique)
    $chat = Join-Path $Primary 'Scripts/Tools/agent_chat.py'
    $token = 'auto-gate-' + [Guid]::NewGuid().ToString('N')
    $locks = @()
    $oldLock = $env:CODEX_LOCK_HELD
    $oldRemote = $env:CODEX_SMOKE_REMOTE
    $oldPriority = $env:CODEX_IGNORE_DESIGNER
    $oldAgent = $env:CODEX_AGENT
    $oldEncoding = $env:PYTHONIOENCODING
    $env:PYTHONIOENCODING = 'utf-8'
    Push-Location -LiteralPath $Scratch
    try {
        $rawChat = & $Py $chat --root $SharedChat read --json
        if ($LASTEXITCODE -ne 0) { throw 'Could not read the shared agent chat.' }
        $messages = @(($rawChat -join [Environment]::NewLine) | ConvertFrom-Json)
        $heads = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
        foreach ($line in @(Invoke-AutoGit @('ls-remote','--heads','origin'))) {
            if ($line -match '\A([0-9a-f]{40,64})\trefs/heads/(.+)\z') { $heads[$Matches[2]] = $Matches[1] }
        }
        if (-not $heads.ContainsKey('main')) { throw 'origin/main was not advertised.' }
        $completed = @($messages | Where-Object { $_.topic -eq 'gate' -and $_.from -eq 'codex' } | ForEach-Object {
            if ($_.text -match '\AGATE \S+ (PASS|FAIL) sha=([a-f0-9]{40,64})\b') { $Matches[2] }
        })
        $stateDir = Join-Path $Scratch 'Saved/Worker/auto-gate'
        $candidate = Get-AutoGateCandidates $heads $messages | Where-Object {
            $_.Sha -notin $completed -and -not (Test-Path -LiteralPath (Join-Path $stateDir "$($_.Sha).sent"))
        } | Select-Object -First 1
        if (-not $candidate) { Write-Host 'No ungated READY commits.'; return 0 }
        if ($Preview) {
            Write-Host "DRY RUN: $($candidate.Branch) $($candidate.Sha) onto origin/main $($heads['main'])."
            return 0
        }

        if (-not (Test-AutoGateAdmission $roots (Get-Date))) { Write-Host 'Gate deferred: worker busy or frozen.'; return 0 }
        foreach ($root in $roots) {
            $lock = New-AutoGateLock $root $token
            if (-not $lock) { Write-Host "Gate deferred: lock held at $root."; return 0 }
            $locks += $lock
        }
        if (-not (Test-AutoGateAdmission $roots (Get-Date))) { Write-Host 'Gate deferred after locking: worker busy or frozen.'; return 0 }
        $deadline = (Get-Date).Date.AddHours(16.5)
        $env:CODEX_LOCK_HELD = '1'; $env:CODEX_SMOKE_REMOTE = 'off'; $env:CODEX_IGNORE_DESIGNER = '1'; $env:CODEX_AGENT = 'worker-auto-gate'

        $dirty = @(Invoke-AutoGit @('status','--porcelain','--untracked-files=no'))
        $mergeHeadPath = Invoke-AutoGit @('rev-parse','--git-path','MERGE_HEAD')
        if ($dirty.Count -or (Test-Path -LiteralPath $mergeHeadPath)) { throw 'Scratch checkout has local changes or a merge in progress.' }

        $resultFile = Join-Path $stateDir "$($candidate.Sha).result"
        if (Test-Path -LiteralPath $resultFile) {
            $message = Get-Content -LiteralPath $resultFile -Raw -Encoding UTF8
        } else {
            $dir = Join-Path $stateDir ($candidate.Sha.Substring(0,12) + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
            $null = [IO.Directory]::CreateDirectory($dir)
            $log = Join-Path $dir 'gate.log'
            $stage = [ordered]@{ build = 'pending'; tests = 'pending'; smokes = 'pending' }
            $state = 'FAIL'
            $base = $null
            $switched = $false
            try {
                $branch = $candidate.Branch
                $null = Invoke-AutoGit @('check-ref-format',"refs/heads/$branch")
                Invoke-AutoGit @('fetch','--no-tags','origin','+refs/heads/main:refs/remotes/origin/main',
                    "+refs/heads/$($branch):refs/auto-gate/candidate") | Out-File -LiteralPath $log -Encoding UTF8 -Append
                $base = Invoke-AutoGit @('rev-parse','refs/remotes/origin/main')
                $tip = Invoke-AutoGit @('rev-parse','refs/auto-gate/candidate')
                if ($tip -ne $candidate.Sha) { throw 'The READY tip moved while fetching. Announce its new SHA.' }
                $helper = Invoke-AutoGit @('show',"$($base):Scripts/Tools/merge_gate.ps1")
                . ([scriptblock]::Create($helper -join [Environment]::NewLine))
                $null = Get-GateMergePlan $base @([pscustomobject]@{ Name=$branch; Commit=$tip }) @()
                if ((Get-Date) -ge $deadline) { throw 'Daytime gate deadline reached before checkout.' }
                $null = Invoke-AutoGit @('switch','--detach','--no-overwrite-ignore',$base)
                $switched = $true
                Invoke-AutoGit @('-c','user.name=Worker Auto Gate','-c','user.email=worker-gate@example.invalid',
                    '-c','commit.gpgsign=false','merge','--no-ff','--no-edit','--no-overwrite-ignore','--',$tip) |
                    Out-File -LiteralPath $log -Encoding UTF8 -Append
                Add-Content -LiteralPath $log -Value "base=$base tip=$tip merged=$(git rev-parse HEAD)"
                $paths = @(Get-GateDiffPaths $base HEAD)
                if (-not $paths.Count) { throw 'Empty merged diff.' }
                . (Join-Path $Scratch 'Scripts/verify_lib.ps1')
                $impact = Resolve-Impact $paths (Get-TestMap)

                $stage.build = 'skip'
                if (Test-GateNeedsBuild $paths) {
                    $stage.build = 'FAIL'
                    if ((Invoke-AutoGateStage (Join-Path $Scratch 'Scripts/build.ps1') @() (Join-Path $dir 'build.log') $deadline) -ne 0) {
                        throw 'Build failed.'
                    }
                    $stage.build = 'PASS'
                }
                $stage.tests = 'FAIL'
                if ((Invoke-AutoGateStage (Join-Path $Scratch 'Scripts/test.ps1') @('-Smart','-SkipSmokes','-Base',$base) (Join-Path $dir 'tests.log') $deadline) -ne 0) {
                    throw 'Smart tests failed.'
                }
                $stage.tests = 'PASS'

                $stage.smokes = 'none'
                if ($impact.Smokes.Count) {
                    $stage.smokes = 'FAIL'
                    $started = [datetime]::UtcNow
                    $names = ($impact.Smokes | ForEach-Object { "CodexTactics.$_" }) -join ','
                    $json = Join-Path $dir 'SmokeParallel-worker.json'
                    $exitCode = Invoke-AutoGateStage (Join-Path $Scratch 'Scripts/smoke_parallel.ps1') @(
                        '-Remote','off','-Commands',$names,'-Jobs','1','-ResultJson',$json
                    ) (Join-Path $dir 'smokes.log') $deadline
                    $rows = @(Read-GateWorkerRows $dir $started $impact.Smokes $exitCode)
                    foreach ($row in $rows) {
                        $leaf = [IO.Path]::GetFileName($row.Log)
                        if ($leaf) {
                            $file = Join-Path (Join-Path $Scratch 'Saved/Logs') $leaf
                            if (Test-Path -LiteralPath $file -PathType Leaf) { Copy-Item -LiteralPath $file -Destination $dir }
                        }
                    }
                    $details = ($rows | ForEach-Object { "$($_.Name)=$($_.State)" }) -join ','
                    $stage.smokes = $details
                    Assert-GateWorkerVerdicts $rows $dir $exitCode
                    if (@($rows | Where-Object State -eq 'FAIL').Count) { throw 'Impacted smoke failed.' }
                }
                $state = 'PASS'
            } catch {
                Add-Content -LiteralPath $log -Value "FAIL: $($_.Exception.Message)"
            } finally {
                if ($switched) {
                    try {
                        if (Test-Path -LiteralPath $mergeHeadPath) { $null = Invoke-AutoGit @('merge','--abort') }
                        $null = Invoke-AutoGit @('switch','--detach','--no-overwrite-ignore',$base)
                    } catch {
                        $state = 'FAIL'
                        Add-Content -LiteralPath $log -Value "Restoring scratch checkout failed: $($_.Exception.Message)"
                    }
                }
            }
            $message = "GATE $($candidate.Branch) $state sha=$($candidate.Sha) build=$($stage.build) tests=$($stage.tests) smokes=$($stage.smokes) log=$log"
            [IO.File]::WriteAllText($resultFile,$message)
        }
        & $Py $chat --root $SharedChat post --as codex --to claude --topic gate --text $message | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Could not deliver gate result. Saved at $resultFile." }
        [IO.File]::WriteAllText((Join-Path $stateDir "$($candidate.Sha).sent"), $message)
        return [int]($message -match '\AGATE \S+ FAIL ')
    } finally {
        foreach ($lock in $locks) { Remove-AutoGateLock $lock $token }
        $env:CODEX_LOCK_HELD = $oldLock
        $env:CODEX_SMOKE_REMOTE = $oldRemote
        $env:CODEX_IGNORE_DESIGNER = $oldPriority
        $env:CODEX_AGENT = $oldAgent
        $env:PYTHONIOENCODING = $oldEncoding
        Pop-Location
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    try { exit (Invoke-AutoGate $Repo $WorkerRoot $Slot1 $ChatRoot $Python -Preview:$DryRun -At $PreviewAt) }
    catch { Write-Error $_; exit 1 }
}
