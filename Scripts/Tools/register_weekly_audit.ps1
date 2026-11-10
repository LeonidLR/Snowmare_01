# Creates / updates the Windows Task Scheduler task "CodexTactics Weekly Audit": Sunday 02:00, current user (runs only while
# logged on, no stored password), starts the audit of this checkout (run it from the main checkout). The audit itself waits up
# to 3 h for a closed editor, else skips the analyzer with a note. Does NOT run anything; -Unregister removes the task.
# Usage: powershell -ExecutionPolicy Bypass -File Scripts\Tools\register_weekly_audit.ps1 [-Time 02:00] [-Unregister]
param(
    [string]$Time = "02:00",
    [switch]$Unregister
)

$ErrorActionPreference = "Stop"
$TaskName = "CodexTactics Weekly Audit"
if ($Unregister) {
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
    Write-Host "Removed '$TaskName'."
    exit 0
}
$Root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Script = Join-Path $PSScriptRoot "weekly_audit.ps1"
$Arg = "-NoProfile -ExecutionPolicy Bypass -File `"$Script`" -Root `"$Root`""
$Action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument $Arg -WorkingDirectory $Root
$Trigger = New-ScheduledTaskTrigger -Weekly -DaysOfWeek Sunday -At $Time
$Settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -ExecutionTimeLimit (New-TimeSpan -Hours 8) -MultipleInstances IgnoreNew
$Principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive
Register-ScheduledTask -TaskName $TaskName -Action $Action -Trigger $Trigger -Settings $Settings -Principal $Principal -Force | Out-Null
Write-Host "Registered '$TaskName': Sunday $Time, $Script (root $Root)."
