@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion
title Codex Tactics - Playtest Bot (Unreal)
rem Playtest bot runs (UPlaytestBotSubsystem, Scripts\bot_run.ps1); the Wave Editor starts it with: runs profile speed [loadout].
rem Speed is accepted for the editor but not needed: the runs are fixed-step and go as fast as the CPU allows.
cd /d "%~dp0.."

if not "%1"=="" goto RUN_WITH_ARGS

:MENU
cls
echo ===================================================
echo   CODEX TACTICS: PLAYTEST BOT (Unreal)
echo ===================================================
echo.
echo   1. Короткая проверка [1 забег, NORMAL]
echo   2. Быстрый тест [5 забегов, NORMAL]
echo   3. Стандартный прогон [20 забегов, NORMAL]
echo   4. Тест 3 профилей [5 CASUAL + 5 NORMAL + 5 VETERAN]
echo   5. Сравнение 2 профилей [10 VETERAN + 10 CASUAL]
echo   6. Ночной стресс-тест [100 забегов, VETERAN]
echo   7. Создать и сохранить собственный пресет
echo   8. Задать параметры вручную
echo.
set /p CHOICE="Ваш выбор [1-8, по умолчанию 1]: "
if "%CHOICE%"=="" set CHOICE=1
set LOADOUT=COLLECT
if "%CHOICE%"=="1" ( set RUNS=1& set PROFILE=NORMAL& goto START_SIM )
if "%CHOICE%"=="2" ( set RUNS=5& set PROFILE=NORMAL& goto START_SIM )
if "%CHOICE%"=="3" ( set RUNS=20& set PROFILE=NORMAL& goto START_SIM )
if "%CHOICE%"=="4" goto RUN_COMPARE_3
if "%CHOICE%"=="5" goto RUN_COMPARE
if "%CHOICE%"=="6" ( set RUNS=100& set PROFILE=VETERAN& goto START_SIM )
if "%CHOICE%"=="7" goto CREATE_CUSTOM_PRESET
if "%CHOICE%"=="8" goto MANUAL_CONFIG
set RUNS=1& set PROFILE=NORMAL& goto START_SIM

:RUN_WITH_ARGS
set RUNS=%1
set PROFILE=%2
if "%PROFILE%"=="" set PROFILE=VETERAN
set LOADOUT=%4
if "%LOADOUT%"=="" set LOADOUT=COLLECT
goto START_SIM

:CREATE_CUSTOM_PRESET
set /p P_NAME="Название пресета: "
if "%P_NAME%"=="" set P_NAME=Пользовательский пресет
set /p RUNS="Количество забегов [1]: "
if "%RUNS%"=="" set RUNS=1
set /p PROFILE="Профиль бота [NORMAL / VETERAN / CASUAL]: "
if "%PROFILE%"=="" set PROFILE=NORMAL
set LOADOUT=COLLECT
powershell -NoProfile -ExecutionPolicy Bypass -Command "$path='Content\Data\Bot\bot_presets.json'; $presets = if (Test-Path $path) { @(Get-Content $path -Raw | ConvertFrom-Json) } else { @() }; $presets += [PSCustomObject]@{ id = ('preset_' + (Get-Date -Format 'yyyyMMdd_HHmmss')); name = '%P_NAME%'; description = ('Пользовательский: %RUNS% забегов, %PROFILE%'); runs = [int]%RUNS%; profile = '%PROFILE%'; speed = 1.0 }; $presets | ConvertTo-Json -Depth 5 | Set-Content $path -Encoding UTF8"
echo [УСПЕХ] Пресет "%P_NAME%" сохранён в Content\Data\Bot\bot_presets.json
goto START_SIM

:MANUAL_CONFIG
set /p RUNS="Количество забегов [10]: "
if "%RUNS%"=="" set RUNS=10
set /p PROFILE="Профиль [VETERAN / NORMAL / CASUAL]: "
if "%PROFILE%"=="" set PROFILE=NORMAL
set /p LOADOUT="Снаряжение [COLLECT / UNIQUE / PRESET]: "
if "%LOADOUT%"=="" set LOADOUT=COLLECT
goto START_SIM

:RUN_COMPARE_3
for %%P in (CASUAL NORMAL VETERAN) do powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\bot_run.ps1 -Runs 5 -Profile %%P
goto FINISH

:RUN_COMPARE
for %%P in (VETERAN CASUAL) do powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\bot_run.ps1 -Runs 10 -Profile %%P
goto FINISH

:START_SIM
echo.
echo [КОНФИГУРАЦИЯ] Забегов: %RUNS%, Профиль: %PROFILE%, Снаряжение: %LOADOUT%
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\bot_run.ps1 -Runs %RUNS% -Profile %PROFILE% -Loadout %LOADOUT%

:FINISH
echo.
echo ===================================================
echo   Симуляции завершены. Телеметрия: Saved\Telemetry\raw_runs\runs.jsonl
echo   Графики: Scripts\run_wave_editor.bat
echo ===================================================
if "%1"=="" pause
