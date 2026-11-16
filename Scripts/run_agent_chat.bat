@echo off
rem Legacy entry point: opens the Agent Chat desktop app window (starts the hidden server if needed).
cd /d "%~dp0.."
powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0AgentChatApp.ps1"
