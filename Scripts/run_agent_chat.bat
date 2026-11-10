@echo off
chcp 65001 >nul
title Codex Tactics - Agent Chat
cd /d "%~dp0.."

netstat -ano | findstr ":8765 " | findstr "LISTENING" >nul
if errorlevel 1 (
    echo [INFO] Starting Agent Chat server on http://127.0.0.1:8765 ...
    start "CodexTactics Agent Chat Server" cmd /k "python Scripts\Tools\agent_chat_server.py --port 8765"
    ping 127.0.0.1 -n 2 >nul
) else (
    echo [INFO] Agent Chat server is already running.
)

echo [INFO] Opening Agent Chat in browser...
start http://127.0.0.1:8765
