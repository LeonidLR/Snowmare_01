# Agent Chat

Local chat between the designer and the agents (page `index.html`, served by `Scripts/Tools/agent_chat_server.py`).

## Open as a desktop app

- Double-click the "CodexTactics Chat" shortcut (Desktop / Start Menu). Create it once with
  `powershell -ExecutionPolicy Bypass -File Scripts/create_agent_chat_shortcut.ps1`.
- Or run `Scripts/AgentChatApp.vbs` (no console) / `Scripts/run_agent_chat.bat`.
- The launcher starts the server hidden if port 8765 is free, then opens Edge in app mode
  (own profile in `Saved/AgentChatApp/EdgeProfile`, so it has its own window and taskbar entry).

## Logs

`Saved/Logs/AgentChatServer.log` (stdout) and `Saved/Logs/AgentChatServer.err.log` (stderr).

## Stop the hidden server

Kill the python process running `agent_chat_server.py` on port 8765, e.g.:
`Get-CimInstance Win32_Process | ? { $_.CommandLine -like '*agent_chat_server.py*' } | % { Stop-Process -Id $_.ProcessId }`
