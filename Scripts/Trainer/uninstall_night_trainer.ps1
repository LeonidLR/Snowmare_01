# Removes the night trainer's Task Scheduler task (the run folders under Saved\Trainer stay).
#   powershell -ExecutionPolicy Bypass -File Scripts\Trainer\uninstall_night_trainer.ps1 [-TaskName "CodexTactics Night Trainer"]
param([string]$TaskName = "CodexTactics Night Trainer")
$Task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if (-not $Task) { Write-Host "No task '$TaskName'."; exit 0 }
if ($Task.State -eq "Running") { Stop-ScheduledTask -TaskName $TaskName }
Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
Write-Host "Removed '$TaskName'."
