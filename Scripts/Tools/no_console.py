"""Import this first in long-running window-less tools (agent_chat_server, chat_dispatcher) - 2026-11-17, the designer's
"hundreds of Terminal windows": a pythonw process has no console, so every subprocess it starts (git, powershell, python) got a NEW
console, and on Windows 11 each new console is a Windows Terminal window. Here every subprocess defaults to CREATE_NO_WINDOW."""
import subprocess
import sys

if sys.platform == "win32" and not getattr(subprocess.Popen, "_codex_no_console", False):
    _OriginalPopen = subprocess.Popen
    _CREATE_NO_WINDOW = 0x08000000

    class _NoConsolePopen(_OriginalPopen):
        _codex_no_console = True

        def __init__(self, *args, **kwargs):
            if not kwargs.get("creationflags"):
                kwargs["creationflags"] = _CREATE_NO_WINDOW
            super().__init__(*args, **kwargs)

    subprocess.Popen = _NoConsolePopen
