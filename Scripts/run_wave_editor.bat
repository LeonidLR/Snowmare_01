@echo off
chcp 65001 >nul
title Codex Tactics - Wave & Level Editor (Unreal)
rem Wave Editor (Tools/WaveEditor): edits Content/Data/LevelJson/*.json, which the game reads when a level starts.
cd /d "%~dp0..\Tools\WaveEditor"
if not exist node_modules (
  echo [INFO] First run: installing the editor's npm packages...
  call npm install
)
echo [INFO] Starting the editor at http://localhost:5173
npm run dev
