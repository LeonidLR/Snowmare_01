# Registers the Windows Task Scheduler task that runs the night trainer (docs/tools/night-trainer.md).
#   Nightly (default: every day 22:00 -> 08:00):
#     powershell -ExecutionPolicy Bypass -File Scripts\Trainer\install_night_trainer.ps1 -Mode Nightly -StartTime 22:00 -EndTime 08:00
#   Weekend (Fri 20:00 -> Mon 07:00):
#     powershell -ExecutionPolicy Bypass -File Scripts\Trainer\install_night_trainer.ps1 -Mode Weekend
#   Options: -RunId pmc-main (the same id every night = one run that resumes) -Parallel 2 -YieldToEditor -Mock
#            -TaskName "CodexTactics Night Trainer" -WhatIf (print what would be registered, register nothing)
# The task runs as the current user, only while logged on (no stored password), starts late if the PC was off
# (StartWhenAvailable), never runs twice at once, and is cut off 3 h after the window (clips + report need time).
# Remove it: Scripts\Trainer\uninstall_night_trainer.ps1. Watch it: Scripts\Trainer\status.ps1.
param(
    [ValidateSet("Nightly", "Weekend")][string]$Mode = "Nightly",
    [string]$StartTime = "22:00",
    [string]$EndTime = "08:00",
    [string]$WeekendStartDay = "Friday",
    [string]$WeekendStart = "20:00",
    [string]$WeekendEndDay = "Monday",
    [string]$WeekendEnd = "07:00",
    [string]$RunId = "pmc-main",
    [int]$Parallel = 2,
    [switch]$YieldToEditor,
    [switch]$Mock,
    [string]$TaskName = "CodexTactics Night Trainer",
    [switch]$WhatIf
)
$ErrorActionPreference = "Stop"
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Runner = Join-Path $PSScriptRoot "run_night_trainer.ps1"

function Get-WindowHours([string]$From, [string]$To, [int]$ExtraDays) {
    $a = [datetime]::ParseExact($From, "HH:mm", $null); $b = [datetime]::ParseExact($To, "HH:mm", $null)
    $h = ($b - $a).TotalHours + 24 * $ExtraDays
    if ($h -le 0) { $h += 24 }
    return $h
}

$RunArgs = "-NoProfile -ExecutionPolicy Bypass -File `"$Runner`" -RunId $RunId -Parallel $Parallel"
if ($YieldToEditor) { $RunArgs += " -YieldToEditor" }
if ($Mock) { $RunArgs += " -Mock" }
if ($Mode -eq "Nightly") {
    $RunArgs += " -UntilTime $EndTime"
    $Trigger = New-ScheduledTaskTrigger -Daily -At $StartTime
    $WindowH = Get-WindowHours $StartTime $EndTime 0
    $When = "daily $StartTime -> $EndTime"
} else {
    $RunArgs += " -UntilDay $WeekendEndDay -UntilTime $WeekendEnd"
    $Trigger = New-ScheduledTaskTrigger -Weekly -DaysOfWeek $WeekendStartDay -At $WeekendStart
    $Span = ([int][System.DayOfWeek]$WeekendEndDay - [int][System.DayOfWeek]$WeekendStartDay + 7) % 7
    $WindowH = Get-WindowHours $WeekendStart $WeekendEnd $Span
    $When = "$WeekendStartDay $WeekendStart -> $WeekendEndDay $WeekendEnd"
}
$Action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument $RunArgs -WorkingDirectory $Repo
$Settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Hours ([Math]::Ceiling($WindowH) + 3))
$Principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Limited

Write-Host "Task      : $TaskName ($When, window $([Math]::Round($WindowH, 1)) h)"
Write-Host "Action    : powershell.exe $RunArgs"
Write-Host "Work dir  : $Repo"
if ($WhatIf) { Write-Host "WhatIf: nothing registered."; exit 0 }
Register-ScheduledTask -TaskName $TaskName -Action $Action -Trigger $Trigger -Settings $Settings -Principal $Principal `
    -Description "Evolves the PMC commander weights in the headless arena (Scripts/Trainer, docs/tools/night-trainer.md). Never edits Content/." -Force | Out-Null
Write-Host "Registered. Run it now: Start-ScheduledTask -TaskName '$TaskName'   Remove: Scripts\Trainer\uninstall_night_trainer.ps1"
