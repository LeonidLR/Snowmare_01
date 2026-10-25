@echo off
chcp 65001 >nul
title Codex Tactics - Training Ground
rem Training Ground (Tools/TrainingGround, the former Wave Editor): departments Waves / Movement / Level design.
rem Edits Content/Data/LevelJson/*.json, which the game reads when a level starts.
cd /d "%~dp0..\Tools\TrainingGround"
if not exist node_modules (
  echo [INFO] First run: installing the npm packages...
  call npm install
)
echo [INFO] Starting Training Ground at http://localhost:5173
npm run dev
