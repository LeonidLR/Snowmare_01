@echo off
cd /d "%~dp0..\.."
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\Trainer\status.ps1 -RunId pmc-v2 -Resume
powershell -NoProfile -Command "Start-ScheduledTask -TaskName 'CodexTactics Night Trainer'"
echo Trainer started (runs until 08:00, pauses while the editor is open).
pause

