"""Switch the lead between the main PC and codex-worker (designer 2026-11-19: "переключаться между машинами ... туда и обратно").

Run by the Claude lead on the machine that HANDS OVER:
    python Scripts/Tools/lead_switch.py --to worker      (main PC -> codex-worker)
    python Scripts/Tools/lead_switch.py --to main        (codex-worker -> main PC)
    python Scripts/Tools/lead_switch.py --status         (who leads now, from origin)
What it does (docs/port/LEAD_SWITCH.md):
  1. every lane in docs/port/LANES.json that has a "worktree" on THIS machine: tracked changes there are committed as
     "wip(switch): ..." (never `git add -A`: untracked designer packs stay out) and its branch is pushed - nothing is left
     only on the machine that goes away;
  2. writes docs/port/LEAD_ACTIVE.json (who leads, since when, the hand-over branch) and docs/port/HANDOVER_LIVE.md (every
     lane with its state, branch head and age, plus what the receiving lead must do first), commits both on the CURRENT
     branch of this checkout and pushes it (main PC: main; worker: its worker/lead-* branch);
  3. prints the one line the designer sends to the receiving Claude session.
It never touches the designer's uncommitted files: it only stages the two files it writes (and, per lane worktree, files
that are already tracked).
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import socket
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
LANES = REPO / "docs" / "port" / "LANES.json"
ACTIVE = REPO / "docs" / "port" / "LEAD_ACTIVE.json"
LIVE = REPO / "docs" / "port" / "HANDOVER_LIVE.md"
# The worker's checkouts live under C:\Work (CodexTactics, CodexTactics-slot1); the main PC's under Documents\Unreal Projects.
MACHINE = "worker" if str(REPO).lower().replace("/", "\\").startswith("c:\\work") else "main"


def git(*args: str, cwd: Path = REPO, check: bool = False) -> str:
    r = subprocess.run(["git", "-C", str(cwd), *args], capture_output=True, text=True)
    if check and r.returncode != 0:
        raise SystemExit(f"git {' '.join(args)} failed in {cwd}: {r.stderr.strip()}")
    return r.stdout.strip()


def push_lane(lane: dict) -> str:
    wt = Path(lane.get("worktree", ""))
    if not lane.get("worktree") or not wt.exists() or lane.get("machine", "main") != MACHINE:
        return "not on this machine"
    branch = git("rev-parse", "--abbrev-ref", "HEAD", cwd=wt)
    if lane.get("branch") and branch != lane["branch"]:
        return f"worktree is on {branch}, lane says {lane['branch']} - left alone"
    if git("status", "--porcelain", "--untracked-files=no", cwd=wt):
        git("commit", "-qam", f"wip(switch): {lane['id']} handed over to the {other()} lead", cwd=wt)
    env = dict(os.environ, CODEX_AGENT="claude")
    r = subprocess.run(["git", "-C", str(wt), "push", "-q", "origin", branch], capture_output=True, text=True, env=env)
    return f"pushed {branch} {git('rev-parse', '--short', 'HEAD', cwd=wt)}" if r.returncode == 0 else f"PUSH FAILED: {r.stderr.strip()[:200]}"


def other() -> str:
    return "worker" if MACHINE == "main" else "main"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--to", choices=["worker", "main"])
    ap.add_argument("--status", action="store_true")
    ap.add_argument("--note", default="", help="one line for the receiving lead")
    a = ap.parse_args()
    if a.status or not a.to:
        git("fetch", "-q", "origin")
        print(git("show", "origin/main:docs/port/LEAD_ACTIVE.json") or "no LEAD_ACTIVE.json on origin/main")
        return 0
    if a.to == MACHINE:
        raise SystemExit(f"this machine is already '{MACHINE}'; run the switch on the machine that hands over")
    git("fetch", "-q", "origin")
    branch0 = git("rev-parse", "--abbrev-ref", "HEAD")
    if git("rev-list", "--count", f"HEAD..origin/{branch0}") not in ("", "0"):
        git("merge", "-q", "--ff-only", f"origin/{branch0}", check=True)   # fast-forward only; never touches uncommitted files
    lanes = json.loads(LANES.read_text(encoding="utf-8")).get("lanes", [])
    now = dt.datetime.now().astimezone()
    rows = []
    for lane in lanes:
        res = push_lane(lane) if lane.get("state", "active") not in ("done",) else "done"
        head = git("log", "-1", "--format=%h %cd %s", "--date=format:%m-%d %H:%M", f"origin/{lane['branch']}") if lane.get("branch") else ""
        rows.append(f"| {lane['id']} | {lane.get('state', 'active')}{(' - ' + lane['reason']) if lane.get('reason') else ''} | "
                    f"{lane.get('owner', '')} | `{lane.get('branch', '')}` {head} | {res} |")
    branch = git("rev-parse", "--abbrev-ref", "HEAD")
    ACTIVE.write_text(json.dumps({"lead": a.to, "since": now.isoformat(timespec="seconds"), "from": MACHINE,
                                  "handover_branch": branch, "note": a.note}, indent=1) + "\n", encoding="utf-8")
    first = ("1. `git fetch`; read this file, docs/port/LANES.json, docs/port/WORKER_LEAD_2026-11-20.md (worker) or the newest "
             "docs/port/CLAUDE_SESSION_PROMPT_*.md (main).\n"
             "2. Main PC receiving: back up the designer's uncommitted files (Saved/Backups/designer-wip-*), then review + merge the "
             f"hand-over branch `{branch}` if it is not main; build main; restart the lanes listed below from their branches.\n"
             "3. Worker receiving: work in C:\\Work\\CodexTactics-slot1 on a worker/lead-* branch; continue the lanes from their branches.\n"
             "4. Update docs/port/LANES.json owners / states for the lanes you take, so the watchdog follows them.")
    LIVE.write_text(f"# Live hand-over: {MACHINE} -> {a.to} ({now.strftime('%Y-%m-%d %H:%M')})\n\n"
                    f"Note: {a.note or '-'}\n\n## First steps for the receiving lead\n{first}\n\n## Lanes\n"
                    "| lane | state | owner | branch / head | switch |\n|---|---|---|---|---|\n" + "\n".join(rows) + "\n",
                    encoding="utf-8")
    git("add", str(ACTIVE), str(LIVE), check=True)
    git("commit", "-q", "-m", f"docs(port): lead switch {MACHINE} -> {a.to}\n\nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>", check=True)
    env = dict(os.environ, CODEX_AGENT="claude")
    r = subprocess.run(["git", "-C", str(REPO), "push", "-q", "origin", branch], capture_output=True, text=True, env=env)
    print("\n".join(rows))
    print(f"\nhand-over committed on {branch}: " + ("pushed" if r.returncode == 0 else f"PUSH FAILED {r.stderr.strip()[:200]}"))
    print(f"\nSend this to the {a.to} Claude session: «Прими управление: прочитай docs/port/HANDOVER_LIVE.md (ветка {branch}) и продолжай.»")
    return 0 if r.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
