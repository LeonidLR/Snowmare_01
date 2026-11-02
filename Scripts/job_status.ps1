# Machine-readable job status for the scripts the Training Ground server starts (level generator pipeline, 2026-11-02:
# levelgen_build_map.ps1, bot_run.ps1 combat lab batches, levelgen_record_run.ps1, levelgen_route_smoke.ps1). Dot-source it:
#   . (Join-Path $PSScriptRoot "job_status.ps1")
#   $Job = New-JobStatus -File <status.json> -Job build_map -Log <log>      # state "running"
#   Set-JobStatus $Job @{ map = "/Game/..." }                              # merges into "result", rewrites the file
#   Wait-JobLock $Job "purpose" -Minutes 60                                # "waiting_lock" while the shared agent lock is held; after
#                                                                          # Minutes: state "refused" (reason lock_held), exit 3
#   Complete-JobStatus $Job "done" 0 "message"                             # or "failed" / "refused" with a non-zero code
# The file (UTF-8, no BOM), rewritten on every change - a server polls it:
#   { "schema": "codex.job_status/1", "job": "<kind>", "state": "running" | "waiting_lock" | "done" | "failed" | "refused",
#     "reason": "" | "editor_open" | "lock_held" | "timeout" | "<script specific>", "message": "...", "exit_code": null | <int>,
#     "pid": <this script's pid>, "started_utc": "...", "updated_utc": "...", "finished_utc": null | "...", "log": "<abs path>",
#     "lock_holder": null | { agent, purpose, pid, started }, "result": { <job specific> } }
# A process whose status says running / waiting_lock but whose pid is gone has died: the server treats that as failed.

$script:JobUtf8 = New-Object System.Text.UTF8Encoding($false)

function Write-JobStatusFile($Job) {
    $Job.status.updated_utc = (Get-Date).ToUniversalTime().ToString("o")
    New-Item -ItemType Directory -Force -Path (Split-Path $Job.file) | Out-Null
    $Tmp = "$($Job.file).tmp"
    [IO.File]::WriteAllText($Tmp, ($Job.status | ConvertTo-Json -Depth 8), $script:JobUtf8)
    Move-Item -Force $Tmp $Job.file
}

function New-JobStatus([string]$File, [string]$Job, [string]$Log = "") {
    $Status = [ordered]@{
        schema = "codex.job_status/1"; job = $Job; state = "running"; reason = ""; message = ""; exit_code = $null; pid = $PID
        started_utc = (Get-Date).ToUniversalTime().ToString("o"); updated_utc = ""; finished_utc = $null; log = $Log; lock_holder = $null
        result = [ordered]@{}
    }
    $Obj = [pscustomobject]@{ file = [IO.Path]::GetFullPath($File); status = $Status }
    Write-JobStatusFile $Obj
    return $Obj
}

function Set-JobStatus($Job, [hashtable]$Result = @{}, [string]$State = "", [string]$Message = "") {
    if (-not $Job) { return }
    foreach ($Key in $Result.Keys) { $Job.status.result[$Key] = $Result[$Key] }
    if ($State) { $Job.status.state = $State }
    if ($Message) { $Job.status.message = $Message }
    Write-JobStatusFile $Job
}

function Complete-JobStatus($Job, [string]$State, [int]$ExitCode, [string]$Message = "", [string]$Reason = "") {
    if (-not $Job) { return }
    $Job.status.state = $State
    $Job.status.exit_code = $ExitCode
    $Job.status.finished_utc = (Get-Date).ToUniversalTime().ToString("o")
    if ($Message) { $Job.status.message = $Message }
    if ($Reason) { $Job.status.reason = $Reason }
    Write-JobStatusFile $Job
}

# Waits for the shared agent lock (Scripts\agent_lock.ps1, dot-sourced by the caller) with a clear status instead of a silent hang, then
# takes it. Returns what Enter-AgentLock returns ($true = taken here, $false = the parent holds it). Refused after $Minutes: exit 3.
function Wait-JobLock($Job, [string]$Purpose, [int]$Minutes = 60) {
    if ($env:CODEX_LOCK_HELD -eq "1") { return $false }
    $Deadline = (Get-Date).AddMinutes($Minutes)
    while ($true) {
        $Lock = Get-AgentLock
        if (-not (Test-AgentLockAlive $Lock) -or ($Lock.pid -eq $PID)) { break }
        if ($Job) {
            $Job.status.lock_holder = [ordered]@{ agent = $Lock.agent; purpose = $Lock.purpose; pid = $Lock.pid; started = $Lock.started }
            Set-JobStatus $Job @{} "waiting_lock" ("waiting for the build lock: {0} runs '{1}' since {2}" -f $Lock.agent, $Lock.purpose, $Lock.started)
        }
        if ((Get-Date) -gt $Deadline) {
            Complete-JobStatus $Job "refused" 3 ("the build lock is still held by {0} ('{1}', since {2}); gave up after {3} min" -f $Lock.agent, $Lock.purpose, $Lock.started, $Minutes) "lock_held"
            Write-Host "[job] the build lock is held by $($Lock.agent) ($($Lock.purpose)); giving up." -ForegroundColor Red
            exit 3
        }
        Start-Sleep -Seconds 10
    }
    if ($Job) {
        $Job.status.lock_holder = $null
        Set-JobStatus $Job @{} "running" ""
    }
    return (Enter-AgentLock $Purpose 1)
}

# This checkout's Unreal Editor is open (not a -game / -run= process): it holds the map packages / the DLLs.
function Test-ProjectEditorOpen([string]$Project) {
    $Full = [IO.Path]::GetFullPath($Project)
    return [bool](Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%.exe'" | Where-Object {
            $_.CommandLine -and ($_.CommandLine.Replace('/', '') -like "*$Full*") -and ($_.CommandLine -notmatch '-run=|-game') })
}
