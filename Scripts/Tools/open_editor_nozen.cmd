@echo off
rem Lead 2026-11-25: editor hung at 0-5 %% ("Wait for ZenServer?" hidden behind the splash; Zen readiness event never arrives).
rem Starts Zen on its own (if not running), then the editor that connects to it.
setlocal
set "MAP=%~1"
set "EXTRA="
if "%~1"=="" goto after_args
shift
:collect_args
if "%~1"=="" goto after_args
set "EXTRA=%EXTRA% %1"
shift
goto collect_args
:after_args
set "ZEN=%LOCALAPPDATA%\UnrealEngine\Common\Zen"
"%ZEN%\Install\zen.exe" status | find "8558" >nul || start "" /min "%ZEN%\Install\zenserver.exe" --port 8558 --data-dir "%ZEN%\Data" --http asio --quiet
:wait
curl -s -m 2 http://127.0.0.1:8558/health/ready >nul 2>&1 || (timeout /t 2 >nul & goto wait)
set "UPROJ=%~dp0..\..\CodexTactics.uproject"
start "" "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%UPROJ%" %MAP% -NoZenAutoLaunch%EXTRA%
