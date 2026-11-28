@echo off
rem Stops the hidden Training Ground dev server (the process listening on port 5173) started by run_training_ground_hidden.vbs.
powershell -NoProfile -Command "Get-NetTCPConnection -State Listen -LocalPort 5173 -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.OwningProcess -Force }; 'Training Ground stopped.'"
timeout /t 2 >nul
