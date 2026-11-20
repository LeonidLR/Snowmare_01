"""Lane watchdog (designer 2026-11-19: "processes must never just stop without a trigger").

Every lane of work in flight (a session, a subagent, Grok, the worker) is registered in docs/port/LANES.json with its branch, its
chat topic, a state and how long it may stay quiet. This script runs on a schedule (Windows Task Scheduler, every 10 min,
independent of any Claude session) and checks each lane:
  - state "active": the newest of (last commit of origin/<branch>, last chat post in <topic>) must be younger than max_quiet_min.
  - state "blocked" / "waiting": needs a non-empty "reason"; quiet longer than max_wait_min (default 240) is a stall too
    (somebody must unblock it).
  - state "done" / "paused-by-designer": ignored.
A stall is announced ONCE per lane (until it moves again): a chat post to everybody (topic "lanes", the designer's task board
shows it, it wakes `agent_chat.py wait`), a line in Saved/Lanes/alerts.log (the lead's Monitor tails it), and
Saved/Lanes/status.json is rewritten every run (what the board / the lead reads).

Usage:
  python Scripts/Tools/lane_watchdog.py              (one check; what the scheduled task runs)
  python Scripts/Tools/lane_watchdog.py --dry-run    (print, write nothing, post nothing)
  python Scripts/Tools/lane_watchdog.py --list       (the lanes and their age)
Lane registry rules are in docs/port/LANES.md.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# The scheduled task runs under pythonw (no console): every console child (git, ssh, python) would open its own window every 10 min
# (designer 2026-11-20: "окна PowerShell постоянно выскакивают"). CREATE_NO_WINDOW keeps them hidden.
_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def _run(*args, **kwargs):
    kwargs.setdefault("creationflags", _NO_WINDOW)
    return subprocess.run(*args, **kwargs)
REGISTRY = REPO / "docs" / "port" / "LANES.json"
OUT_DIR = REPO / "Saved" / "Lanes"
CHAT = REPO / "Saved" / "AgentChat" / "chat.jsonl"
QUIET_STATES = {"done", "paused-by-designer"}
WAIT_STATES = {"blocked", "waiting"}


def now_utc() -> dt.datetime:
    return dt.datetime.now(dt.timezone.utc)


def parse_ts(text: str) -> dt.datetime | None:
    try:
        t = dt.datetime.fromisoformat(text.replace("Z", "+00:00"))
        return t if t.tzinfo else t.replace(tzinfo=dt.timezone.utc)
    except (ValueError, AttributeError):
        return None


def last_chat_by_topic(chat_file: Path) -> dict[str, dt.datetime]:
    out: dict[str, dt.datetime] = {}
    if not chat_file.exists():
        return out
    with chat_file.open(encoding="utf-8") as f:
        for line in f:
            try:
                m = json.loads(line)
            except ValueError:
                continue
            topic, ts = m.get("topic") or "", parse_ts(m.get("ts", ""))
            if topic and ts and (topic not in out or ts > out[topic]):
                out[topic] = ts
    return out


def last_commit(branch: str, fetched: set[str]) -> dt.datetime | None:
    if not branch:
        return None
    if branch not in fetched:
        _run(["git", "-C", str(REPO), "fetch", "-q", "origin", branch], capture_output=True, timeout=120)
        fetched.add(branch)
    r = _run(["git", "-C", str(REPO), "log", "-1", "--format=%cI", f"origin/{branch}"], capture_output=True, text=True)
    return parse_ts(r.stdout.strip()) if r.returncode == 0 and r.stdout.strip() else None


def in_window(window: str, local_now: dt.datetime) -> bool:
    """'16:55-07:45' (local clock, may wrap midnight); empty = always."""
    if not window:
        return True
    a, b = (dt.datetime.strptime(x.strip(), "%H:%M").time() for x in window.split("-"))
    t = local_now.time()
    return (t >= a or t < b) if a > b else (a <= t < b)


def ssh_mtime(spec: dict) -> dt.datetime | None:
    """Last write time (UTC) of a file on another machine: {"host": "codex-worker", "path": "C:/..."}; None when unreachable."""
    cmd = f"powershell -NoProfile -Command \"(Get-Item '{spec['path']}').LastWriteTimeUtc.ToString('o')\""
    try:
        r = _run(["ssh", "-o", "ConnectTimeout=8", "-o", "BatchMode=yes", spec["host"], cmd], capture_output=True, text=True, timeout=40)
    except (subprocess.TimeoutExpired, OSError):
        return None
    return parse_ts(r.stdout.strip()) if r.returncode == 0 else None


def evaluate(lane: dict, commit_ts: dt.datetime | None, chat_ts: dt.datetime | None, now: dt.datetime,
             heartbeat_ts: dt.datetime | None = None, local_now: dt.datetime | None = None) -> dict:
    """Pure: the lane's verdict. Returns {id, state, age_min, verdict ('ok'|'stalled'|'ignored'), why}."""
    state = (lane.get("state") or "active").strip()
    if lane.get("window") and not in_window(lane["window"], local_now or now.astimezone()):
        return {"id": lane.get("id", "?"), "title": lane.get("title", ""), "owner": lane.get("owner", ""), "state": state,
                "last_activity": None, "age_min": None, "verdict": "ignored", "why": f"outside its window {lane['window']}"}
    seen = [t for t in (commit_ts, chat_ts, heartbeat_ts) if t]
    last = max(seen) if seen else None
    age = (now - last).total_seconds() / 60.0 if last else None
    res = {"id": lane.get("id", "?"), "title": lane.get("title", ""), "owner": lane.get("owner", ""), "state": state,
           "last_activity": last.isoformat() if last else None, "age_min": None if age is None else round(age, 1)}
    if state in QUIET_STATES:
        return {**res, "verdict": "ignored", "why": state}
    if state in WAIT_STATES:
        if not (lane.get("reason") or "").strip():
            return {**res, "verdict": "stalled", "why": f"state {state} without a reason"}
        limit = float(lane.get("max_wait_min", 240))
        if age is None or age > limit:
            return {**res, "verdict": "stalled", "why": f"{state} ({lane['reason']}) and quiet {res['age_min']} min > {limit:g}"}
        return {**res, "verdict": "ok", "why": f"{state}: {lane['reason']}"}
    limit = float(lane.get("max_quiet_min", 45))
    if age is None:
        return {**res, "verdict": "stalled", "why": "no commit on its branch and no post in its topic yet"}
    if age > limit:
        return {**res, "verdict": "stalled", "why": f"active but quiet {res['age_min']} min > {limit:g} (no commit, no status post)"}
    return {**res, "verdict": "ok", "why": f"active, last sign {res['age_min']} min ago"}


def post(text: str) -> None:
    _run([sys.executable, str(REPO / "Scripts" / "Tools" / "agent_chat.py"), "post", "--as", "claude", "--to", "all",
                    "--topic", "lanes", "--text", text], capture_output=True, timeout=60)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    registry = json.loads(REGISTRY.read_text(encoding="utf-8"))
    lanes = registry.get("lanes", [])
    chat = last_chat_by_topic(CHAT)
    fetched: set[str] = set()
    now = now_utc()
    results = [evaluate(l, last_commit(l.get("branch", ""), fetched), chat.get(l.get("topic", "")), now,
                        ssh_mtime(l["ssh_heartbeat"]) if l.get("ssh_heartbeat") else None) for l in lanes]
    for r in results:
        if a.list or a.dry_run or r["verdict"] == "stalled":
            print(f"{r['verdict']:8} {r['id']:<22} {r['state']:<18} age {r['age_min']} min - {r['why']}")
    if a.dry_run or a.list:
        return 0
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    announced_file = OUT_DIR / "announced.json"
    try:
        announced = json.loads(announced_file.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        announced = {}
    for r in results:
        key = f"{r['id']}|{r['last_activity']}|{r['state']}"
        if r["verdict"] == "stalled" and announced.get(r["id"]) != key:
            msg = f"STALL {r['id']} ({r['title']}, owner {r['owner']}): {r['why']}. Lead: restart it or set its state + reason in docs/port/LANES.json."
            post(msg)
            with (OUT_DIR / "alerts.log").open("a", encoding="utf-8") as f:
                f.write(f"{now.isoformat()} {msg}\n")
            announced[r["id"]] = key
        elif r["verdict"] != "stalled":
            announced.pop(r["id"], None)
    announced_file.write_text(json.dumps(announced, indent=1), encoding="utf-8")
    (OUT_DIR / "status.json").write_text(json.dumps({"checked": now.isoformat(), "lanes": results}, indent=1, ensure_ascii=False),
                                         encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
