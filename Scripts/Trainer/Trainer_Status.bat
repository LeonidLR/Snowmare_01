@echo off
cd /d "%~dp0..\.."
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\Trainer\status.ps1 -RunId pmc-v2
pause

