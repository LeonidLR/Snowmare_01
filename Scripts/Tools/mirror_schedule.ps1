# Registers / removes the main PC's scheduled task "CodexTactics Mirror": Scripts\Tools\mirror_to_worker.py sync every
# -EveryHours (default 2) and at logon (+10 min), hidden (pythonw, no console window), only while this user is logged on.
# The sync itself refuses a slow link (hotspot) and resumes a cut run. Log: %LOCALAPPDATA%\CodexTactics\mirror\sync.log.
#   powershell -ExecutionPolicy Bypass -File Scripts\Tools\mirror_schedule.ps1            # install / update
#   powershell -ExecutionPolicy Bypass -File Scripts\Tools\mirror_schedule.ps1 -Remove    # uninstall
#   powershell -ExecutionPolicy Bypass -File Scripts\Tools\mirror_schedule.ps1 -RunNow    # install + start one run now
param([double]$EveryHours = 2, [switch]$Remove, [switch]$RunNow)
$ErrorActionPreference = "Stop"
$TaskName = "CodexTactics Mirror"
if ($Remove) {
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host "removed $TaskName"; exit 0
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$script = Join-Path $repo "Scripts\Tools\mirror_to_worker.py"
$py = (Get-Command python -ErrorAction Stop).Source
$pyw = Join-Path (Split-Path $py -Parent) "pythonw.exe"
if (-not (Test-Path $pyw)) { $pyw = $py }
$logDir = Join-Path $env:LOCALAPPDATA "CodexTactics\mirror"
New-Item -ItemType Directory -Force $logDir | Out-Null
# pythonw has no console: the run writes its own log through a cmd-free wrapper (python -c, stdout -> file).
$wrapper = Join-Path $logDir "run_sync.py"
@"
import subprocess, sys, datetime
log = r'$logDir\sync.log'
with open(log, 'a', encoding='utf-8') as f:
    f.write('\n=== ' + datetime.datetime.now().isoformat(timespec='seconds') + '\n'); f.flush()
    r = subprocess.run([r'$py', '-u', r'$script', 'sync'], stdout=f, stderr=subprocess.STDOUT,
                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    f.write('=== exit %d\n' % r.returncode)
"@ | Set-Content -Encoding UTF8 $wrapper
$action = New-ScheduledTaskAction -Execute $pyw -Argument "`"$wrapper`"" -WorkingDirectory $repo
$repeat = New-ScheduledTaskTrigger -Once -At ((Get-Date).AddMinutes(5)) -RepetitionInterval ([TimeSpan]::FromHours($EveryHours))
$logon = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
$logon.Delay = "PT10M"
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable `
    -MultipleInstances IgnoreNew -ExecutionTimeLimit ([TimeSpan]::FromHours(8))
$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Limited
Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger @($repeat, $logon) -Settings $settings -Principal $principal -Force | Out-Null
Write-Host "installed $TaskName: every $EveryHours h + at logon, hidden; log $logDir\sync.log"
if ($RunNow) { Start-ScheduledTask -TaskName $TaskName; Write-Host "started one run now" }
