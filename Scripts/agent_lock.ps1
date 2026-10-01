# Build lock shared by the agents (Claude, Gemini) and the user: only one build / test / smoke / editor-script session
# at a time, because they all use the same Binaries\ (a build fails while a game or editor process holds the DLLs).
# The lock is Saved\agent.lock (JSON: agent, purpose, pid, started). A lock whose process has exited is stale and is
# taken over. Child scripts started by the holder (verify_all -> build / test / smoke) see CODEX_LOCK_HELD=1 and skip it.
#
# Dot-source it from a script:
#   . (Join-Path $PSScriptRoot "agent_lock.ps1")
#   $Owned = Enter-AgentLock "build"        # waits while someone else holds it; $false when the parent holds it
#   try { ... } finally { if ($Owned) { Exit-AgentLock } }
#
# Or hold it by hand around other work (an editor Python commandlet, a profiling session):
#   powershell -ExecutionPolicy Bypass -File Scripts\agent_lock.ps1 -Acquire -Purpose "profiling"   (holds it for that
#   PowerShell session: run it with -NoExit, or use -Acquire / -Release from the same shell)
#   powershell -ExecutionPolicy Bypass -File Scripts\agent_lock.ps1 -Status
#   powershell -ExecutionPolicy Bypass -File Scripts\agent_lock.ps1 -Release
# Who you are: $env:CODEX_AGENT = "claude" | "gemini" | "user" (default "unknown").
# No param() block: a dot-sourced param block would leak its variables ($Status, …) into the calling script.

$script:AgentLockFile = Join-Path (Split-Path $PSScriptRoot -Parent) "Saved\agent.lock"

function Get-AgentLock {
    if (-not (Test-Path $script:AgentLockFile)) { return $null }
    try { return Get-Content $script:AgentLockFile -Raw | ConvertFrom-Json } catch { return $null }
}

function Test-AgentLockAlive($Lock) {
    if (-not $Lock) { return $false }
    if ($Lock.manual) { return $true } # held by hand until -Release
    return [bool](Get-Process -Id $Lock.pid -ErrorAction SilentlyContinue)
}

function Enter-AgentLock([string]$LockPurpose, [int]$Minutes = 60, [switch]$Manual) {
    if ($env:CODEX_LOCK_HELD -eq "1") { return $false } # the parent script holds it
    $Agent = $(if ($env:CODEX_AGENT) { $env:CODEX_AGENT } else { "unknown" })
    $Deadline = (Get-Date).AddMinutes($Minutes)
    while ($true) {
        $Lock = Get-AgentLock
        if (-not (Test-AgentLockAlive $Lock) -or ($Lock.pid -eq $PID)) { break }
        if ((Get-Date) -gt $Deadline) {
            Write-Host "[agent-lock] still held by $($Lock.agent) ($($Lock.purpose), since $($Lock.started)); giving up." -ForegroundColor Red
            exit 3
        }
        Write-Host "[agent-lock] busy: $($Lock.agent) is running $($Lock.purpose) since $($Lock.started); waiting..." -ForegroundColor Yellow
        Start-Sleep -Seconds 20
    }
    New-Item -ItemType Directory -Force (Split-Path $script:AgentLockFile) | Out-Null
    [pscustomobject]@{ agent = $Agent; purpose = $LockPurpose; pid = $PID; manual = [bool]$Manual; started = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss") } |
        ConvertTo-Json | Set-Content -Encoding UTF8 $script:AgentLockFile
    $env:CODEX_LOCK_HELD = "1"
    return $true
}

function Exit-AgentLock {
    $Lock = Get-AgentLock
    if ($Lock -and ($Lock.pid -eq $PID -or $Lock.manual)) { Remove-Item $script:AgentLockFile -ErrorAction SilentlyContinue }
    $env:CODEX_LOCK_HELD = $null
}

# Command-line use (not when dot-sourced): -Status | -Release | -Acquire [-Purpose "..."] [-WaitMinutes 60].
if ($MyInvocation.InvocationName -ne ".") {
    $CliArgs = @($args)
    $Purpose = "manual"
    $WaitMinutes = 60
    for ($i = 0; $i -lt $CliArgs.Count; $i++) {
        if ($CliArgs[$i] -eq "-Purpose" -and $i + 1 -lt $CliArgs.Count) { $Purpose = $CliArgs[$i + 1] }
        if ($CliArgs[$i] -eq "-WaitMinutes" -and $i + 1 -lt $CliArgs.Count) { $WaitMinutes = [int]$CliArgs[$i + 1] }
    }
    if ($CliArgs -contains "-Status") {
        $Lock = Get-AgentLock
        if (Test-AgentLockAlive $Lock) { Write-Host "held by $($Lock.agent): $($Lock.purpose) since $($Lock.started)" } else { Write-Host "free" }
    } elseif ($CliArgs -contains "-Acquire") {
        $null = Enter-AgentLock $Purpose $WaitMinutes -Manual
        Write-Host "[agent-lock] held ($Purpose) until: Scripts\agent_lock.ps1 -Release"
    } elseif ($CliArgs -contains "-Release") {
        Exit-AgentLock
        Write-Host "[agent-lock] released"
    }
}
