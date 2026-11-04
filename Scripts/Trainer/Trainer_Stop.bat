@echo off
cd /d "%~dp0..\.."
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\Trainer\status.ps1 -RunId pmc-v2 -Stop
echo Trainer will stop after the current fights (progress is kept).
pause

