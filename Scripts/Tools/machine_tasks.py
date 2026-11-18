#!/usr/bin/env python3
"""What runs right now on the main PC and on codex-worker (agent chat Machines tab).

Pure part: classify_processes(procs, now) turns [{name, pid, start_time, command_line}] into grouped, human readable items.
Collectors: collect_local() / collect_remote() run one PowerShell snippet (CIM process list + CPU + free RAM).
Command lines never leave this module (only labels do) and are scrubbed of key-like strings anyway.
"""
import base64
import json
import os
import re
import subprocess
import time

REMOTE_HOST = "codex-worker"
HIDDEN = "[скрыто]"

_SECRET_RES = [
    re.compile(r"sk-ant-[A-Za-z0-9_\-]+"),
    re.compile(r"(--api-key)(\s+|=)\S+", re.I),
    re.compile(r"((?:ANTHROPIC|XAI|OPENAI|GEMINI|GOOGLE)_API_KEY)(\s*=\s*|\s+)\S+", re.I),
]

_PS_SCRIPT = r"""
$ErrorActionPreference = 'SilentlyContinue'
$procs = @(Get-CimInstance Win32_Process | Where-Object { $_.CommandLine } | ForEach-Object {
  $st = 0; if ($_.CreationDate) { $st = [int64]([DateTimeOffset]$_.CreationDate).ToUnixTimeSeconds() }
  [pscustomobject]@{ pid = $_.ProcessId; name = $_.Name; start_time = $st; command_line = $_.CommandLine }
})
$os = Get-CimInstance Win32_OperatingSystem
$cpu = (Get-CimInstance Win32_Processor | Measure-Object -Property LoadPercentage -Average).Average
[pscustomobject]@{ cpu = $cpu; free_gb = [math]::Round($os.FreePhysicalMemory / 1MB, 1); total_gb = [math]::Round($os.TotalVisibleMemorySize / 1MB, 1); procs = $procs } | ConvertTo-Json -Depth 4 -Compress
"""

_KIND_ORDER = {"Редактор": 0, "Сборка": 1, "Smoke": 2, "Python": 3, "Commandlet": 4, "Агент": 5, "Скрипт": 6, "Тренер": 7}


def scrub(text):
    """Remove anything that looks like a key / token from a string."""
    text = text or ""
    text = _SECRET_RES[0].sub(HIDDEN, text)
    for rx in _SECRET_RES[1:]:
        text = rx.sub(lambda m: m.group(1) + m.group(2) + HIDDEN, text)
    return text


def _worktree(cmd):
    """Worktree label from a path in the command line (CodexTactics-claude -> claude, plain CodexTactics -> main)."""
    m = re.search(r"CodexTactics-([A-Za-z0-9_.\-]+)", cmd)
    if m:
        return m.group(1)
    if re.search(r"CodexTactics", cmd, re.I):
        return "main"
    return ""


def _script_name(cmd, pattern):
    m = re.search(pattern, cmd, re.I)
    if not m:
        return ""
    return re.split(r"[\\/]", m.group(1).strip('"'))[-1]


def _classify_one(p):
    name = (p.get("name") or "").lower()
    cmd = p.get("command_line") or ""
    low = cmd.lower()
    wt = _worktree(cmd)
    wt_s = f" ({wt})" if wt else ""

    if name in ("unrealbuildtool.exe", "build.bat"):
        return "Сборка", f"Сборка{wt_s}"
    if name in ("unrealeditor-cmd.exe", "unrealeditor-win64-debuggame-cmd.exe"):
        m = re.search(r"-ExecCmds=\"?([^\s\"]+)", cmd, re.I)
        if m:
            smoke = re.split(r"[,;]", m.group(1))[0]
            if smoke.lower().startswith("codextactics."):
                smoke = smoke.split(".", 1)[1]
            return "Smoke", f"Smoke {smoke}{wt_s}"
        if re.search(r"-run=pythonscript", cmd, re.I):
            s = _script_name(cmd, r"-script=(\"[^\"]+\"|\S+)")
            return "Python", f"Python в редакторе: {s or '?'}{wt_s}"
        m = re.search(r"-run=(\w+)", cmd, re.I)
        return "Commandlet", f"Commandlet {m.group(1) if m else ''}{wt_s}".strip()
    if name == "unrealeditor.exe":
        if "codextactics.uproject" in low and "-game" not in low and "-run=" not in low:
            return "Редактор", f"Редактор дизайнера ({wt or 'main'})"
        return None
    if name in ("grok", "grok.exe"):
        return "Агент", "Грок"
    if name in ("claude", "claude.exe"):
        if "--bare" in low or "claude_api_agent" in low:
            return "Агент", "API-агент Claude"
        return None
    if name in ("codex", "codex.exe"):
        return "Агент", "Codex"
    if name in ("powershell.exe", "pwsh.exe", "python.exe", "pythonw.exe", "py.exe", "node.exe", "cmd.exe"):
        for key, label in (
            ("smoke_parallel.ps1", "Параллельные smoke"),
            ("worker_sync.ps1", "Синхронизация воркера"),
            ("grok_task.ps1", "Грок: задача"),
            ("claude_api_agent.ps1", "API-агент Claude"),
            ("qd_window.ps1", "QD-окно тренера"),
        ):
            if key in low:
                return "Скрипт", label + wt_s
        if "nighttrainer" in low or re.search(r"scripts[\\/]+trainer", low):
            return "Тренер", "Ночной тренер"
    return None


def classify_processes(procs, now=None):
    """-> list of {kind, label, pid, started, running_min, count}; same kind+label are merged (count, longest run)."""
    now = now if now is not None else time.time()
    merged = {}
    for p in procs or []:
        r = _classify_one(p)
        if not r:
            continue
        kind, label = r
        st = p.get("start_time") or 0
        e = merged.setdefault((kind, label), {"kind": kind, "label": label, "pid": p.get("pid"), "start": st, "count": 0})
        e["count"] += 1
        if st and (not e["start"] or st < e["start"]):
            e["start"], e["pid"] = st, p.get("pid")
    items = []
    for e in merged.values():
        st = e["start"]
        items.append({
            "kind": e["kind"], "label": scrub(e["label"]), "pid": e["pid"], "count": e["count"],
            "started": time.strftime("%H:%M:%S", time.localtime(st)) if st else "",
            "running_min": int(max(0, now - st) // 60) if st else None,
        })
    items.sort(key=lambda i: (_KIND_ORDER.get(i["kind"], 9), -(i["running_min"] or 0)))
    return items


def _run_ps(prefix, timeout):
    enc = base64.b64encode(_PS_SCRIPT.encode("utf-16-le")).decode("ascii")
    cmd = prefix + ["powershell", "-NoProfile", "-NonInteractive", "-EncodedCommand", enc]
    flags = 0x08000000 if os.name == "nt" else 0  # CREATE_NO_WINDOW
    res = subprocess.run(cmd, capture_output=True, timeout=timeout, creationflags=flags)
    out = res.stdout.decode("utf-8", "replace").strip()
    err = res.stderr.decode("utf-8", "replace").strip()
    start = out.find("{")
    if start < 0:
        raise RuntimeError(scrub(err or out or f"exit {res.returncode}")[:300])
    return json.loads(out[start:])


def _summarize(raw, now=None):
    procs = raw.get("procs") or []
    if isinstance(procs, dict):
        procs = [procs]
    return {
        "status": "онлайн",
        "cpu": raw.get("cpu"),
        "free_gb": raw.get("free_gb"),
        "total_gb": raw.get("total_gb"),
        "items": classify_processes(procs, now),
    }


def collect_local():
    return _summarize(_run_ps([], 25))


def collect_remote(host=REMOTE_HOST):
    return _summarize(_run_ps(["ssh", "-o", "ConnectTimeout=8", "-o", "BatchMode=yes", host], 15))
