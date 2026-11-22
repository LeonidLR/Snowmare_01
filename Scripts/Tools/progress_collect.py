#!/usr/bin/env python3
"""Progress collector (P0) for the "Прогресс разработки" page.

Design: docs/port/progress/PROGRESS_SYSTEM_PROPOSAL_2026-11-22.md (sections 2, 3, 8, 9).

Pure reader of: git refs of all worktrees (plumbing only, GIT_OPTIONAL_LOCKS=0), the agent chat, LANES.json +
Saved/Lanes/status.json, the inbox / outbox scan of task_board.py and docs/port/DAY_PLAN_<date>.md.
Single writer of Saved/Progress/{events.jsonl,state.json,cursors.json}; a lock file makes a second runner exit.
Nobody types a status: it is derived from evidence (git > chat claims > hand-written docs).

  python Scripts/Tools/progress_collect.py [--rebuild] [--once] [--html docs/port/progress/progress.html]

State = fold(plan.json, events) + a live git snapshot; --rebuild twice gives a byte-identical state.json.
P0 derives planned / in_progress / claimed / in_review / rework / merged only. built / verified / accepted are P1.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import html
import json
import os
import re
import subprocess
import sys
import time
from typing import Any

os.environ["GIT_OPTIONAL_LOCKS"] = "0"  # read-only git: never take index locks (the designer's checkout stays untouched)

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(TOOLS_DIR, "..", ".."))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

try:  # existing inbox / outbox scanning is reused, not re-implemented
    import task_board  # type: ignore
except Exception:  # pragma: no cover - collector must still run without it
    task_board = None

SCHEMA = "progress-state/1"
IN_PROGRESS_WINDOW_SEC = 2 * 3600  # same window as task_board.py
RECENT_BRANCH_SEC = 48 * 3600
COMMIT_WINDOW_DAYS = 14
FINISH_DISCREPANCY_DAYS = 7
LOCK_STALE_SEC = 300

STATUS_RU = {
    "planned": "в плане",
    "in_progress": "в работе",
    "claimed": "исполнитель: готово",
    "in_review": "на проверке",
    "rework": "возвращено",
    "merged": "влито (не проверено)",
}
STATUS_RANK = {"planned": 0, "in_progress": 1, "rework": 1, "claimed": 2, "in_review": 3, "merged": 4}
STATUS_COLOR = {"planned": "grey", "in_progress": "blue", "rework": "blue", "claimed": "blue", "in_review": "blue", "merged": "amber"}

_FINISH_RE = re.compile(r"^\s*([A-Za-z]?\d{3}[a-z]?)\s+FINISH\s*\|", re.IGNORECASE)
_REVIEW_RE = re.compile(r"^\s*([A-Za-z]?\d{3})[a-z]?\s+review:\s*(\S.*)$", re.IGNORECASE | re.DOTALL)
_OK_RE = re.compile(r"^\s*OK\s+([A-Za-z]?\d{3})\b", re.IGNORECASE)
_HASH_RE = re.compile(r"\b([0-9a-f]{7,40})\b")
_NUM_RE = re.compile(r"^([A-Za-z]?\d{3})")
_BRANCH_NUM_RE = re.compile(r"^(?:gemini|grok|codex|claude)/([A-Za-z]?\d{3})-", re.IGNORECASE)


# --------------------------------------------------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------------------------------------------------
def git(repo: str, *args: str, input_text: str | None = None) -> tuple[int, str]:
    """Run a read-only git command; returns (returncode, stdout)."""
    try:
        proc = subprocess.run(
            ["git", "-c", "core.quotepath=false", "-C", repo, *args],
            input=input_text.encode("utf-8") if input_text is not None else None,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            env={**os.environ, "GIT_OPTIONAL_LOCKS": "0"},
        )
    except OSError:
        return 127, ""
    return proc.returncode, proc.stdout.decode("utf-8", errors="replace")


def iso_to_ts(value: Any) -> int:
    if not value:
        return 0
    try:
        text = str(value).replace("Z", "+00:00")
        parsed = dt.datetime.fromisoformat(text)
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=dt.timezone.utc)
        return int(parsed.timestamp())
    except ValueError:
        return 0


def ts_to_iso(ts: int | float | None) -> str | None:
    if not ts:
        return None
    return dt.datetime.fromtimestamp(int(ts), tz=dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def read_json(path: str, default: Any = None) -> Any:
    try:
        with open(path, "r", encoding="utf-8-sig") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return default


def dump_json(obj: Any) -> bytes:
    return (json.dumps(obj, sort_keys=True, indent=1, ensure_ascii=False) + "\n").encode("utf-8")


def write_atomic(path: str, data: bytes) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "wb") as fh:
        fh.write(data)
    os.replace(tmp, path)


# --------------------------------------------------------------------------------------------------------------------
# lock: a second runner exits
# --------------------------------------------------------------------------------------------------------------------
class CollectorLock:
    def __init__(self, path: str) -> None:
        self.path = path
        self.held = False

    def acquire(self) -> bool:
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        for _ in range(2):
            try:
                fd = os.open(self.path, os.O_CREAT | os.O_EXCL | os.O_RDWR)
                os.write(fd, str(os.getpid()).encode("utf-8"))
                os.close(fd)
                self.held = True
                return True
            except FileExistsError:
                try:
                    if time.time() - os.path.getmtime(self.path) > LOCK_STALE_SEC:
                        os.remove(self.path)
                        continue
                except OSError:
                    pass
                return False
        return False

    def touch(self) -> None:
        if self.held:
            try:
                os.utime(self.path, None)
            except OSError:
                pass

    def release(self) -> None:
        if self.held:
            try:
                os.remove(self.path)
            except OSError:
                pass
            self.held = False


# --------------------------------------------------------------------------------------------------------------------
# git: refs, commits, merged-ness (ancestor OR the same patch-id in main)
# --------------------------------------------------------------------------------------------------------------------
def list_refs(repo: str) -> list[dict[str, Any]]:
    """All local branches + origin branches, de-duplicated by short name (newest tip wins)."""
    fmt = "%(refname)%09%(objectname)%09%(committerdate:unix)%09%(subject)"
    rc, out = git(repo, "for-each-ref", "--format=" + fmt, "refs/heads", "refs/remotes/origin")
    best: dict[str, dict[str, Any]] = {}
    if rc != 0:
        return []
    for line in out.splitlines():
        parts = line.split("\t", 3)
        if len(parts) < 4:
            continue
        ref, sha, ts_s, subject = parts
        if ref.startswith("refs/heads/"):
            name = ref[len("refs/heads/"):]
        else:
            name = ref[len("refs/remotes/origin/"):]
        if name == "HEAD":
            continue
        try:
            ts = int(ts_s)
        except ValueError:
            ts = 0
        cur = best.get(name)
        if cur is None or ts > cur["ts"]:
            best[name] = {"name": name, "ref": ref, "sha": sha, "ts": ts, "subject": subject}
    return [best[k] for k in sorted(best)]


def pick_main_ref(repo: str) -> str:
    rc, _ = git(repo, "rev-parse", "--verify", "-q", "origin/main")
    return "origin/main" if rc == 0 else "main"


def resolve_commits(repo: str, names: list[str]) -> dict[str, str | None]:
    """Abbreviated / full hash -> full sha (None when no such commit in any ref)."""
    out_map: dict[str, str | None] = {}
    names = [n for n in dict.fromkeys(names) if n]
    if not names:
        return out_map
    rc, out = git(repo, "cat-file", "--batch-check", input_text="\n".join(f"{n}^{{commit}}" for n in names) + "\n")
    lines = out.splitlines()
    for name, line in zip(names, lines):
        parts = line.split()
        out_map[name] = parts[0] if len(parts) >= 3 and parts[1] == "commit" else None
    for name in names:
        out_map.setdefault(name, None)
    return out_map


def commit_info(repo: str, sha: str) -> dict[str, Any]:
    rc, out = git(repo, "show", "-s", "--format=%ct%x09%s", sha)
    parts = out.strip().split("\t", 1)
    try:
        return {"ts": int(parts[0]), "subject": parts[1] if len(parts) > 1 else ""}
    except (ValueError, IndexError):
        return {"ts": 0, "subject": ""}


def _patch_ids_of_log(repo: str, args: list[str]) -> dict[str, str]:
    """patch-id -> commit for `git log -p <args>` (non-merge commits)."""
    rc, patch = git(repo, "log", "-p", "--no-merges", "--format=commit %H", *args)
    if rc != 0 or not patch:
        return {}
    rc, out = git(repo, "patch-id", "--stable", input_text=patch)
    res: dict[str, str] = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 2:
            res.setdefault(parts[0], parts[1])
    return res


def merge_status(repo: str, shas: list[str], main_ref: str) -> dict[str, str | None]:
    """sha -> 'ancestor' | 'patch-id' | None. Cherry-picks count as merged (same patch-id in main)."""
    res: dict[str, str | None] = {}
    pending: list[str] = []
    for sha in dict.fromkeys(shas):
        rc, _ = git(repo, "merge-base", "--is-ancestor", sha, main_ref)
        if rc == 0:
            res[sha] = "ancestor"
        else:
            res[sha] = None
            pending.append(sha)
    if not pending:
        return res
    mine: dict[str, str] = {}  # patch-id -> sha for pending commits
    oldest = None
    for sha in pending:
        rc, out = git(repo, "diff-tree", "-p", "--root", "--no-commit-id", sha)
        if rc == 0 and out.strip():
            rc2, pid_out = git(repo, "patch-id", "--stable", input_text=out)
            parts = pid_out.split()
            if parts:
                mine[parts[0]] = sha
        ts = commit_info(repo, sha)["ts"]
        if ts and (oldest is None or ts < oldest):
            oldest = ts
    if not mine:
        return res
    since = max(int(oldest or 0) - 86400, int(time.time()) - 21 * 86400)
    main_ids = _patch_ids_of_log(repo, [f"--since={since}", main_ref])
    for pid, sha in mine.items():
        if pid in main_ids:
            res[sha] = "patch-id"
    return res


# --------------------------------------------------------------------------------------------------------------------
# chat parsing
# --------------------------------------------------------------------------------------------------------------------
def parse_finish_text(text: str) -> dict[str, Any] | None:
    """'<NNN> FINISH | status | last commit | done | NOT done | needs Claude | gate | report' (positional or 'key: value')."""
    head = _FINISH_RE.match(text or "")
    if not head:
        return None
    fields = [f.strip() for f in text.split("|")]
    raw_num = head.group(1)
    num_m = _NUM_RE.match(raw_num)
    num = (num_m.group(1) if num_m else raw_num).upper()
    status = re.sub(r"^status:\s*", "", fields[1], flags=re.IGNORECASE) if len(fields) > 1 else ""
    commit_hash = None
    for field in fields[1:3]:
        for token in _HASH_RE.findall(field):
            if re.search(r"\d", token):
                commit_hash = token
                break
        if commit_hash:
            break
    out: dict[str, Any] = {"num": num, "raw_num": raw_num, "status": status, "hash": commit_hash}
    for key, label in (("done", "done"), ("not_done", "not done"), ("gate", "gate"), ("report", "report")):
        for field in fields:
            m = re.match(rf"^{label}:\s*(.*)$", field, flags=re.IGNORECASE | re.DOTALL)
            if m:
                out[key] = m.group(1).strip()
                break
    return out


def parse_review_text(text: str) -> dict[str, Any] | None:
    m = _REVIEW_RE.match(text or "")
    if m:
        verdict_text = m.group(2).strip()
        low = verdict_text.lower()
        verdict = "REWORK" if low.startswith("rework") else "OK" if re.match(r"ok\b", low) else "NOTES"
        return {"num": m.group(1).upper(), "verdict": verdict}
    m = _OK_RE.match(text or "")
    if m:
        return {"num": m.group(1).upper(), "verdict": "OK"}
    return None


def chat_events(chat_path: str) -> tuple[list[dict[str, Any]], int, bool]:
    events: list[dict[str, Any]] = []
    last_id = 0
    if not os.path.isfile(chat_path):
        return events, last_id, False
    with open(chat_path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except ValueError:
                continue
            text = str(msg.get("text", ""))
            mid = msg.get("id")
            if isinstance(mid, int):
                last_id = max(last_id, mid)
            ts = iso_to_ts(msg.get("ts"))
            fin = parse_finish_text(text)
            if fin:
                events.append({"id": f"chat:{mid}", "type": "finish_claimed", "ts": ts, "src": "chat",
                               "data": {**fin, "from": msg.get("from"), "topic": msg.get("topic")}})
                continue
            rev = parse_review_text(text)
            if rev:
                events.append({"id": f"chat:{mid}", "type": "review", "ts": ts, "src": "chat",
                               "data": {**rev, "from": msg.get("from"), "topic": msg.get("topic")}})
    return events, last_id, True


# --------------------------------------------------------------------------------------------------------------------
# day plan
# --------------------------------------------------------------------------------------------------------------------
def find_day_plan(repo: str, today: str) -> str | None:
    docs = os.path.join(repo, "docs", "port")
    exact = os.path.join(docs, f"DAY_PLAN_{today}.md")
    if os.path.isfile(exact):
        return exact
    try:
        names = sorted(n for n in os.listdir(docs) if re.match(r"^DAY_PLAN_\d{4}-\d{2}-\d{2}\.md$", n))
    except OSError:
        return None
    names = [n for n in names if n[9:19] <= today] or names
    return os.path.join(docs, names[-1]) if names else None


def parse_day_plan(path: str) -> dict[str, Any]:
    m = re.search(r"DAY_PLAN_(\d{4}-\d{2}-\d{2})\.md$", path)
    date = m.group(1) if m else ""
    rows: list[dict[str, Any]] = []
    header: list[str] = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line.startswith("|"):
                continue
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if not header:
                header = [c.lower() for c in cells]
                continue
            if all(re.fullmatch(r":?-{2,}:?", c) for c in cells):
                continue
            if not re.fullmatch(r"\d+", cells[0]):
                continue
            row = {"n": int(cells[0]), "who": cells[1] if len(cells) > 1 else "", "task": cells[2] if len(cells) > 2 else "",
                   "done_when": cells[3] if len(cells) > 3 else "", "plan_id": None}
            if "id" in header:
                idx = header.index("id")
                if idx < len(cells) and cells[idx]:
                    row["plan_id"] = cells[idx]
            rows.append(row)
    return {"date": date, "rows": rows}


# --------------------------------------------------------------------------------------------------------------------
# collector
# --------------------------------------------------------------------------------------------------------------------
class Collector:
    def __init__(self, repo: str = REPO_ROOT, out_dir: str | None = None, chat_path: str | None = None,
                 plan_path: str | None = None, now: int | None = None) -> None:
        self.repo = repo
        self.out_dir = out_dir or os.path.join(repo, "Saved", "Progress")
        self.plan_path = plan_path or os.path.join(repo, "docs", "port", "progress", "plan.json")
        self.chat_path = chat_path or self._default_chat_path()
        self.now = int(now if now is not None else time.time())

    def _default_chat_path(self) -> str:
        try:
            import agent_chat  # type: ignore
            return os.path.join(agent_chat.get_storage_dir(), "chat.jsonl")
        except Exception:
            return os.path.join(self.repo, "Saved", "AgentChat", "chat.jsonl")

    # -- sources ---------------------------------------------------------------------------------------------------
    def _commit_events(self, refs: list[dict[str, Any]], old_map: dict[str, str], rebuild: bool) -> list[dict[str, Any]]:
        """One `git log` for every moved / new ref: tips are positive, the previously seen tips are excluded."""
        tips: list[str] = []
        olds: list[str] = []
        for ref in refs:
            old = None if rebuild else old_map.get(ref["name"])
            if old == ref["sha"]:
                continue
            tips.append(ref["sha"])
            if old:
                olds.append(old)
        if not tips:
            return []
        args = ["-n", "5000", "--format=%H%x09%ct%x09%s", f"--since={self.now - COMMIT_WINDOW_DAYS * 86400}", *sorted(set(tips))]
        known_olds = sorted(set(olds) - set(tips))
        if known_olds:
            args += ["--not", *known_olds]
        rc, out = git(self.repo, "log", *args)
        events: list[dict[str, Any]] = []
        for line in out.splitlines():
            parts = line.split("	", 2)
            if len(parts) < 3:
                continue
            try:
                ts = int(parts[1])
            except ValueError:
                continue
            events.append({"id": f"commit:{parts[0]}", "type": "commit", "ts": ts, "src": "git",
                           "data": {"sha": parts[0][:8], "subject": parts[2]}})
        return events

    def _lanes(self) -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]], bool]:
        data = read_json(os.path.join(self.repo, "docs", "port", "LANES.json"), None)
        status = read_json(os.path.join(self.repo, "Saved", "Lanes", "status.json"), None)
        lanes = (data or {}).get("lanes", []) if isinstance(data, dict) else []
        verdicts: dict[str, dict[str, Any]] = {}
        if isinstance(status, dict):
            for item in status.get("lanes", []):
                verdicts[str(item.get("id"))] = item
        return lanes, verdicts, data is not None

    def _inbox(self) -> tuple[list[dict[str, Any]], dict[str, dict[str, str]]]:
        if task_board is None:
            return [], {}
        try:
            moves = task_board.parse_lead_moves(self.repo)
            stubs = task_board.scan_inbox_tasks(self.repo, moves)
            reports = task_board.scan_outbox_reports(self.repo)
            return stubs, reports
        except Exception:
            return [], {}

    # -- run -------------------------------------------------------------------------------------------------------
    def collect(self, rebuild: bool = False) -> tuple[dict[str, Any], list[dict[str, Any]], dict[str, Any]]:
        os.makedirs(self.out_dir, exist_ok=True)
        cursors = {} if rebuild else (read_json(os.path.join(self.out_dir, "cursors.json"), {}) or {})
        old_refs: dict[str, str] = cursors.get("refs", {}) if isinstance(cursors.get("refs"), dict) else {}
        events_path = os.path.join(self.out_dir, "events.jsonl")
        old_events: list[dict[str, Any]] = []
        if not rebuild and os.path.isfile(events_path):
            with open(events_path, "r", encoding="utf-8") as fh:
                for line in fh:
                    try:
                        old_events.append(json.loads(line))
                    except ValueError:
                        pass
        seen = {e["id"] for e in old_events}

        plan = read_json(self.plan_path, {}) or {}
        today = dt.datetime.fromtimestamp(self.now).strftime("%Y-%m-%d")

        refs = list_refs(self.repo)
        main_ref = pick_main_ref(self.repo)
        new_events: list[dict[str, Any]] = []
        new_events += self._commit_events(refs, old_refs, rebuild)
        c_events, last_chat_id, chat_ok = chat_events(self.chat_path)
        new_events += c_events

        lanes, verdicts, lanes_ok = self._lanes()
        for lane in lanes:
            v = verdicts.get(str(lane.get("id")), {})
            new_events.append({"id": f"lane:{lane.get('id')}:{v.get('verdict')}:{v.get('last_activity')}", "type": "lane", "ts": iso_to_ts(v.get("last_activity")),
                               "src": "lanes", "data": {"id": lane.get("id"), "verdict": v.get("verdict"), "branch": lane.get("branch"), "state": lane.get("state")}})

        stubs, reports = self._inbox()
        for stub in stubs:
            new_events.append({"id": f"task_stub:{stub['id']}", "type": "task_stub", "ts": 0, "src": "inbox",
                               "data": {"id": stub["id"], "title": stub.get("title"), "file": stub.get("task_file")}})
        for key, rep in sorted(reports.items()):
            if ":" in key:
                new_events.append({"id": f"report:{rep['report_file']}", "type": "report", "ts": 0, "src": "outbox", "data": {"file": rep["report_file"]}})

        day_path = find_day_plan(self.repo, today)
        day_plan: dict[str, Any] = {"date": None, "file": None, "rows": []}
        if day_path:
            parsed = parse_day_plan(day_path)
            day_plan = {"date": parsed["date"], "file": os.path.relpath(day_path, self.repo).replace("\\", "/"), "rows": parsed["rows"]}
            for row in parsed["rows"]:
                rh = hashlib.sha1(json.dumps(row, sort_keys=True, ensure_ascii=False).encode("utf-8")).hexdigest()[:8]
                new_events.append({"id": f"day_row:{parsed['date']}:{row['n']}:{rh}", "type": "day_row", "ts": 0, "src": "day_plan", "data": row})

        # de-duplicate against what is already stored (event ids come from the source identity)
        fresh: list[dict[str, Any]] = []
        for ev in new_events:
            if ev["id"] not in seen:
                seen.add(ev["id"])
                fresh.append(ev)
        fresh.sort(key=lambda e: (e["ts"], e["id"]))
        all_events = old_events + fresh if not rebuild else fresh
        if rebuild:
            all_events.sort(key=lambda e: (e["ts"], e["id"]))

        state = self._fold(plan, all_events, refs, main_ref, lanes, verdicts, stubs, reports, day_plan,
                           {"chat_ok": chat_ok, "chat_last_id": last_chat_id, "lanes_ok": lanes_ok, "day_plan_file": day_plan["file"]})
        cursors_out = {"refs": {r["name"]: r["sha"] for r in refs}, "chat_last_id": last_chat_id,
                       "last_run": ts_to_iso(self.now), "events": len(all_events), "schema": "progress-cursors/1"}
        return state, (all_events if rebuild else fresh), cursors_out | {"_append": not rebuild}

    def run_once(self, rebuild: bool = False, html_path: str | None = None) -> dict[str, Any]:
        state, events, cursors = self.collect(rebuild)
        append = cursors.pop("_append")
        events_path = os.path.join(self.out_dir, "events.jsonl")
        lines = "".join(json.dumps(e, sort_keys=True, ensure_ascii=False) + "\n" for e in events)
        if append and os.path.isfile(events_path):
            with open(events_path, "ab") as fh:
                fh.write(lines.encode("utf-8"))
        else:
            write_atomic(events_path, lines.encode("utf-8"))
        write_atomic(os.path.join(self.out_dir, "state.json"), dump_json(state))
        write_atomic(os.path.join(self.out_dir, "cursors.json"), dump_json(cursors))
        if html_path:
            plan = read_json(self.plan_path, {}) or {}
            write_atomic(html_path, render_html(state, plan, self.now).encode("utf-8"))
        return state

    # -- fold ------------------------------------------------------------------------------------------------------
    def _fold(self, plan: dict[str, Any], events: list[dict[str, Any]], refs: list[dict[str, Any]], main_ref: str,
              lanes: list[dict[str, Any]], verdicts: dict[str, dict[str, Any]], stubs: list[dict[str, Any]],
              reports: dict[str, dict[str, str]], day_plan: dict[str, Any], src: dict[str, Any]) -> dict[str, Any]:
        repo, now = self.repo, self.now
        ref_by_name = {r["name"]: r for r in refs}
        plan_tasks: list[dict[str, Any]] = plan.get("tasks", []) or []
        stub_by_key = {(s["folder_owner"], s["num"]): s for s in stubs}
        stub_by_num: dict[str, list[dict[str, Any]]] = {}
        for s in stubs:
            stub_by_num.setdefault(s["num"], []).append(s)

        finishes = [e for e in events if e["type"] == "finish_claimed"]
        reviews = [e for e in events if e["type"] == "review"]

        # resolve every hash we need in one go
        wanted: list[str] = []
        for t in plan_tasks:
            wanted += (t.get("match") or {}).get("commits", [])
        for e in finishes:
            if e["data"].get("hash"):
                wanted.append(e["data"]["hash"])
        resolved = resolve_commits(repo, wanted)

        # merge status for everything we may judge
        to_check = {s for s in resolved.values() if s}
        for t in plan_tasks:
            for b in (t.get("match") or {}).get("branches", []):
                if b in ref_by_name:
                    to_check.add(ref_by_name[b]["sha"])
        inbox_branches: dict[str, list[str]] = {}
        for t in plan_tasks:
            num = ((t.get("match") or {}).get("inbox"))
            if num:
                for name in ref_by_name:
                    m = _BRANCH_NUM_RE.match(name)
                    if m and m.group(1).upper() == str(num).upper():
                        inbox_branches.setdefault(t["id"], []).append(name)
                        to_check.add(ref_by_name[name]["sha"])
        cand = [r for r in refs if r["name"] not in ("main", "master") and now - r["ts"] <= RECENT_BRANCH_SEC]
        to_check.update(r["sha"] for r in cand)
        merged = merge_status(repo, sorted(to_check), main_ref)

        tasks_out: list[dict[str, Any]] = []
        discrepancies: list[dict[str, Any]] = []
        claimed_hashes_seen: set[str] = set()
        used_branches: set[str] = set()
        lane_by_branch = {str(l.get("branch")): l for l in lanes if l.get("branch")}
        lane_by_topic = {str(l.get("topic")): l for l in lanes if l.get("topic")}
        plan_nums: set[str] = set()

        for t in plan_tasks:
            match = t.get("match") or {}
            num = str(match["inbox"]).upper() if match.get("inbox") else None
            if num:
                plan_nums.add(num)
            branches = list(dict.fromkeys(list(match.get("branches", [])) + inbox_branches.get(t["id"], [])))
            used_branches.update(branches)
            br_info = []
            for b in branches:
                r = ref_by_name.get(b)
                if r:
                    br_info.append({"name": b, "tip": r["sha"][:8], "ts": r["ts"], "in_main": merged.get(r["sha"])})
            last_branch_ts = max([b["ts"] for b in br_info], default=0)

            commits = []
            for name in match.get("commits", []):
                full = resolved.get(name)
                if not full:
                    discrepancies.append({"kind": "plan_commit_missing", "task": t["id"], "hash": name,
                                          "text_ru": f"Коммит {name} из плана задачи {t['id']} не найден ни в одной ветке"})
                    commits.append({"sha": name, "found": False, "in_main": None})
                    continue
                info = commit_info(repo, full)
                commits.append({"sha": full[:8], "found": True, "subject": info["subject"], "ts": info["ts"], "in_main": merged.get(full)})

            # chat claims
            my_fin, my_rev = [], []
            for e in finishes:
                d = e["data"]
                h = resolved.get(d.get("hash")) if d.get("hash") else None
                belongs = bool(num and d["num"] == num) or bool(h and any(c.get("sha") == h[:8] for c in commits))
                if belongs:
                    my_fin.append({"chat_id": e["id"], "ts": e["ts"], "hash": d.get("hash"), "found": bool(h), "status": d.get("status"), "full": h})
                    if h:
                        claimed_hashes_seen.add(h)
            for e in reviews:
                d = e["data"]
                if num and d["num"] == num:
                    my_rev.append({"chat_id": e["id"], "ts": e["ts"], "verdict": d["verdict"]})
            valid_fin = [f for f in my_fin if f["found"]]
            last_fin = max(valid_fin, key=lambda f: f["ts"], default=None)
            last_rev = max(my_rev, key=lambda r: r["ts"], default=None)

            # merged?
            evidence_status: list[str | None]
            if commits:
                evidence_status = [c["in_main"] for c in commits if c.get("found")]
            elif br_info:
                evidence_status = [b["in_main"] for b in br_info]
            elif valid_fin:
                evidence_status = [merged.get(f["full"]) for f in valid_fin]
            else:
                evidence_status = []
            is_merged = bool(evidence_status) and all(evidence_status)

            last_activity = max([last_branch_ts] + [c.get("ts", 0) for c in commits] + [f["ts"] for f in my_fin] + [r["ts"] for r in my_rev])
            if is_merged:
                status = "merged"
            elif last_rev and last_rev["verdict"] == "REWORK" and not (last_fin and last_fin["ts"] > last_rev["ts"]):
                status = "in_progress" if last_branch_ts > last_rev["ts"] else "rework"
            elif last_fin:
                if last_rev and last_rev["ts"] > last_fin["ts"] and last_rev["verdict"] != "REWORK":
                    status = "in_review"
                elif last_branch_ts > last_fin["ts"] + 120:
                    status = "in_progress"
                else:
                    status = "claimed"
            elif last_branch_ts and now - last_branch_ts <= IN_PROGRESS_WINDOW_SEC:
                status = "in_progress"
            else:
                status = "planned"

            lane = None
            for b in branches:
                if b in lane_by_branch:
                    lane = lane_by_branch[b]
                    break
            if lane is None:
                for tp in match.get("topics", []):
                    if tp in lane_by_topic:
                        lane = lane_by_topic[tp]
            lane_out = None
            if lane:
                v = verdicts.get(str(lane.get("id")), {})
                lane_out = {"id": lane.get("id"), "state": lane.get("state"), "verdict": v.get("verdict")}

            flags = []
            if t.get("needs_designer"):
                flags.append("blocked_designer")
            stub = None
            if num:
                owner = t.get("owner")
                stub = stub_by_key.get((owner, num)) or (stub_by_num.get(num) or [None])[0]
            inbox_out = None
            if stub:
                rep = None
                if task_board is not None:
                    rep = task_board.find_task_report(stub, reports)
                inbox_out = {"file": stub.get("task_file"), "report": rep}
            tasks_out.append({
                "id": t["id"], "title_ru": t.get("title_ru"), "stage": t.get("stage"), "week": t.get("week"), "track": t.get("track"),
                "kind": t.get("kind"), "owner": t.get("owner"), "day_row": t.get("day_row"), "due": t.get("due"),
                "status": status, "status_ru": STATUS_RU[status], "color": STATUS_COLOR[status],
                "flags": flags, "depends_on": t.get("depends_on", []), "needs_designer": t.get("needs_designer"),
                "check": t.get("check"), "gates": t.get("gates"), "note_ru": t.get("note_ru"),
                "last_activity": ts_to_iso(last_activity),
                "evidence": {"branches": br_info, "commits": commits, "finish": [{k: v for k, v in f.items() if k != "full"} for f in my_fin],
                             "reviews": my_rev, "lane": lane_out, "inbox": inbox_out},
            })
        status_of = {t["id"]: t["status"] for t in tasks_out}
        for t in tasks_out:
            blocked = [d for d in t["depends_on"] if status_of.get(d, "planned") != "merged"]
            if blocked:
                t["flags"].append("blocked_dependency")
                t["blocked_by"] = blocked

        # discrepancies: FINISH names a hash found in no ref (recent claims only)
        for e in finishes:
            d = e["data"]
            h = d.get("hash")
            if h and not resolved.get(h) and now - e["ts"] <= FINISH_DISCREPANCY_DAYS * 86400:
                discrepancies.append({"kind": "finish_hash_missing", "chat_id": e["id"], "hash": h, "num": d["num"],
                                      "text_ru": f"FINISH {d['raw_num']} называет коммит {h}, его нет ни в одной ветке"})
        discrepancies.sort(key=lambda x: (x["kind"], x.get("chat_id", ""), x.get("task", ""), x.get("hash", "")))

        # branches with recent work and no task / no lane
        stub_nums = {s["num"] for s in stubs} | plan_nums
        lane_branches = set(lane_by_branch)
        anc = merged
        unmatched: list[dict[str, Any]] = []
        for r in cand:
            if anc.get(r["sha"]):
                continue
            name = r["name"]
            m = _BRANCH_NUM_RE.match(name)
            inbox_driven = bool(m and m.group(1).upper() in stub_nums)
            has_task = name in used_branches or inbox_driven
            has_lane = name in lane_branches
            reasons = []
            if not has_task:
                reasons.append("no_task")
            if not has_lane and not inbox_driven:
                reasons.append("no_lane")
            if reasons:
                unmatched.append({"branch": name, "tip": r["sha"][:8], "ts": ts_to_iso(r["ts"]), "subject": r["subject"], "reasons": reasons,
                                  "tasks": sorted(t["id"] for t in tasks_out if name in [b["name"] for b in t["evidence"]["branches"]])})
        unmatched.sort(key=lambda x: (x["ts"] or "", x["branch"]), reverse=True)

        lane_stalled = [{"id": l["id"], "why": (verdicts.get(str(l["id"])) or {}).get("why")} for l in lanes
                        if (verdicts.get(str(l["id"])) or {}).get("verdict") == "stalled"]

        # day plan rows -> statuses
        by_row: dict[str, list[dict[str, Any]]] = {}
        for t in tasks_out:
            if t.get("day_row"):
                by_row.setdefault(t["day_row"], []).append(t)
        rows_out = []
        for row in day_plan["rows"]:
            key = f"{day_plan['date']}:{row['n']}"
            ts_list = by_row.get(key, [])
            if row.get("plan_id"):
                ts_list = ts_list + [t for t in tasks_out if t["id"] == row["plan_id"] and t not in ts_list]
            if ts_list:
                worst = min(ts_list, key=lambda t: STATUS_RANK[t["status"]])
                status = worst["status"]
            else:
                status = "planned"
            rows_out.append({**row, "id": f"d{(day_plan['date'] or '')[5:].replace('-', '')}-{row['n']:02d}", "status": status,
                             "status_ru": STATUS_RU[status], "color": STATUS_COLOR[status], "task_ids": [t["id"] for t in ts_list],
                             "no_plan_task": not ts_list})

        # metrics (P0: values come only from last_known in plan.json; never green)
        metrics_out = []
        for m in plan.get("metrics", []) or []:
            lk = m.get("last_known")
            min_n = m.get("min_n")
            state_ru = "нет данных"
            if lk:
                n = lk.get("n")
                state_ru = "мало данных" if (min_n and (n is None or n < min_n)) else "по данным плана"
            metrics_out.append({"id": m["id"], "title_ru": m.get("title_ru"), "target": m.get("target"), "min_n": min_n, "unit": m.get("unit"),
                                "also_ru": m.get("also_ru"), "last_known": lk, "state_ru": state_ru})

        needs = []
        for t in tasks_out:
            if t.get("needs_designer"):
                needs.append({**t["needs_designer"], "task": t["id"]})
        for st in plan.get("stages", []) or []:
            if st.get("needs_designer"):
                needs.append({**st["needs_designer"], "stage": st["id"]})
        main_sha = ""
        rc, out = git(repo, "rev-parse", main_ref)
        if rc == 0:
            main_sha = out.strip()
        return {
            "schema": SCHEMA,
            "plan_updated": plan.get("updated"),
            "main": {"ref": main_ref, "head": main_sha[:8], "ts": (commit_info(repo, main_sha)["ts"] if main_sha else 0)},
            "sources": {
                "git": {"ok": bool(refs), "refs": len(refs)},
                "chat": {"ok": src["chat_ok"], "last_id": src["chat_last_id"]},
                "lanes": {"ok": src["lanes_ok"]},
                "inbox": {"ok": task_board is not None, "stubs": len(stubs)},
                "day_plan": {"ok": bool(src["day_plan_file"]), "file": src["day_plan_file"]},
            },
            "day_plan": {"date": day_plan["date"], "file": day_plan["file"], "rows": rows_out},
            "tasks": tasks_out,
            "metrics": metrics_out,
            "needs_designer": needs,
            "designer_open": plan.get("designer_open", []),
            "flags": {"unmatched": unmatched, "discrepancies": discrepancies, "lane_stalled": lane_stalled},
        }


# --------------------------------------------------------------------------------------------------------------------
# html
# --------------------------------------------------------------------------------------------------------------------
CSS = """
:root{--bg:#f5f6f8;--card:#fff;--text:#1d2330;--mute:#6b7385;--line:#dde1e8;--grey:#8a92a3;--grey-bg:#eceef2;--blue:#2563c9;--blue-bg:#e3edfb;
--amber:#a8650a;--amber-bg:#fdf0d8;--red:#b42318;--red-bg:#fde8e6;--cur:#2563c9}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){--bg:#12151c;--card:#1b2029;--text:#e6e9ef;--mute:#9aa3b5;--line:#2c3340;
--grey:#9aa3b5;--grey-bg:#262c38;--blue:#7aa7f0;--blue-bg:#1c2b47;--amber:#e8b25a;--amber-bg:#3a2c12;--red:#f2877d;--red-bg:#42201d;--cur:#7aa7f0}}
:root[data-theme="dark"]{--bg:#12151c;--card:#1b2029;--text:#e6e9ef;--mute:#9aa3b5;--line:#2c3340;--grey:#9aa3b5;--grey-bg:#262c38;--blue:#7aa7f0;
--blue-bg:#1c2b47;--amber:#e8b25a;--amber-bg:#3a2c12;--red:#f2877d;--red-bg:#42201d;--cur:#7aa7f0}
*{box-sizing:border-box}html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
main{max-width:820px;margin:0 auto;padding:16px}
h1{font-size:22px;margin:4px 0 2px}h2{font-size:17px;margin:26px 0 10px}h3{font-size:14px;margin:14px 0 6px;color:var(--mute);text-transform:uppercase;letter-spacing:.04em}
p{margin:4px 0}.mute{color:var(--mute)}.small{font-size:13px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px 12px;margin:8px 0;overflow-wrap:anywhere}
.row{display:flex;gap:8px;align-items:flex-start;flex-wrap:wrap}.row .grow{flex:1 1 220px;min-width:0}
.badge{display:inline-block;border-radius:999px;padding:1px 9px;font-size:12px;font-weight:600;white-space:nowrap}
.b-grey{background:var(--grey-bg);color:var(--grey)}.b-blue{background:var(--blue-bg);color:var(--blue)}
.b-amber{background:var(--amber-bg);color:var(--amber)}.b-red{background:var(--red-bg);color:var(--red)}
.bar{border-left:4px solid var(--grey)}.bar.blue{border-left-color:var(--blue)}.bar.amber{border-left-color:var(--amber)}.bar.red{border-left-color:var(--red)}
.num{font-variant-numeric:tabular-nums;color:var(--mute);min-width:1.6em}
.check{margin-top:4px;padding:6px 8px;border-radius:8px;background:var(--grey-bg);font-size:13px}
code{font:12.5px ui-monospace,Consolas,monospace;background:var(--grey-bg);padding:0 4px;border-radius:4px}
.stage{border-left:4px solid var(--line);padding-left:10px}.stage.cur{border-left-color:var(--cur)}
details>summary{cursor:pointer;color:var(--mute)}
footer{margin:28px 0 12px;color:var(--mute);font-size:12px}
.pills{display:flex;flex-wrap:wrap;gap:6px;margin:6px 0}
"""


def _e(value: Any) -> str:
    return html.escape("" if value is None else str(value))


def _ago(ts_iso: str | None, now: int) -> str:
    ts = iso_to_ts(ts_iso)
    if not ts:
        return "нет коммитов"
    delta = max(0, now - ts)
    if delta < 3600:
        return f"{delta // 60} мин назад"
    if delta < 86400:
        return f"{delta // 3600} ч назад"
    return f"{delta // 86400} дн назад"


def _badge(color: str, text: str) -> str:
    return f'<span class="badge b-{color}">{_e(text)}</span>'


def _check_block(check: dict[str, Any] | None) -> str:
    if not check:
        return ""
    parts = []
    if check.get("map"):
        parts.append(f"<div>Карта: {_e(check['map'])}</div>")
    if check.get("how_ru"):
        parts.append(f"<div>Как: {_e(check['how_ru'])}</div>")
    if check.get("console"):
        parts.append(f"<div>Консоль: <code>{_e(check['console'])}</code></div>")
    return f'<div class="check"><b>Где проверить</b>{"".join(parts)}</div>' if parts else ""


def _task_flags(t: dict[str, Any]) -> str:
    out = []
    if "blocked_designer" in t.get("flags", []):
        out.append(_badge("red", "ждёт дизайнера"))
    if "blocked_dependency" in t.get("flags", []):
        out.append(_badge("red", "ждёт: " + ", ".join(t.get("blocked_by", []))))
    return " ".join(out)


def _evidence_line(t: dict[str, Any], now: int) -> str:
    ev = t["evidence"]
    bits = []
    for b in ev["branches"]:
        bits.append(f"ветка <code>{_e(b['name'])}</code> @{_e(b['tip'])}" + (" (в main)" if b["in_main"] else ""))
    merged = [c for c in ev["commits"] if c.get("found") and c.get("in_main")]
    if merged:
        how = "cherry-pick" if any(c["in_main"] == "patch-id" for c in merged) else "в main"
        bits.append("коммиты " + ", ".join(f"<code>{_e(c['sha'])}</code>" for c in merged) + f" ({how})")
    if ev["lane"]:
        bits.append(f"линия {_e(ev['lane']['id'])}: {_e(ev['lane']['verdict'])}")
    if t.get("last_activity"):
        bits.append("активность " + _ago(t["last_activity"], now))
    return f'<p class="small mute">{" · ".join(bits)}</p>' if bits else ""


def _task_card(t: dict[str, Any], now: int, with_check: bool = True) -> str:
    return (f'<div class="card bar {t["color"]}"><div class="row"><div class="grow"><b>{_e(t["title_ru"])}</b>'
            f'<p class="small mute">{_e(t["owner"])} · {_e(t["track"])}'
            + (f' · срок {_e(t["due"])}' if t.get("due") else "") + "</p></div>"
            f'<div>{_badge(t["color"], t["status_ru"])} {_task_flags(t)}</div></div>'
            f'{_evidence_line(t, now)}{_check_block(t.get("check")) if with_check else ""}</div>')


def render_html(state: dict[str, Any], plan: dict[str, Any], now: int | None = None) -> str:
    now = int(now if now is not None else time.time())
    today = dt.datetime.fromtimestamp(now).strftime("%Y-%m-%d")
    tasks = state["tasks"]
    by_id = {t["id"]: t for t in tasks}
    out: list[str] = []
    out.append('<!doctype html><html lang="ru"><head><meta charset="utf-8">'
               '<meta name="viewport" content="width=device-width,initial-scale=1">'
               '<title>Прогресс разработки</title>'
               f"<style>{CSS}</style></head><body><main>")
    out.append("<h1>Прогресс разработки</h1>")
    out.append(f'<p class="mute small">{_e(plan.get("goal_ru"))}</p>')
    src = state["sources"]
    pills = []
    for key, label in (("git", "git"), ("chat", "чат"), ("lanes", "линии"), ("inbox", "задачи"), ("day_plan", "план дня")):
        pills.append(_badge("grey" if src[key]["ok"] else "red", f"{label}: {'есть' if src[key]['ok'] else 'нет данных'}"))
    out.append(f'<div class="pills">{"".join(pills)}</div>')
    out.append('<p class="small mute">Статусы выводятся из коммитов, чата и слияний, а не печатаются вручную. '
               'Зелёного нет: «проверено» и «принято» появятся в следующей фазе.</p>')

    # ---- Today
    dp = state["day_plan"]
    out.append(f"<h2>Сегодня{' · ' + _e(dp['date']) if dp['date'] else ''}</h2>")
    if not dp["rows"]:
        out.append(f'<div class="card bar red">{_badge("red", "нет плана дня")} Файла DAY_PLAN_{_e(today)}.md нет.</div>')
    for row in dp["rows"]:
        linked = [by_id[i] for i in row["task_ids"] if i in by_id]
        out.append(f'<div class="card bar {row["color"]}"><div class="row"><span class="num">{row["n"]}</span>'
                   f'<div class="grow"><b>{_e(row["task"])}</b><p class="small mute">{_e(row["who"])} · готово, когда: {_e(row["done_when"])}</p></div>'
                   f'<div>{_badge(row["color"], row["status_ru"])}</div></div>')
        if row["no_plan_task"]:
            out.append('<p class="small mute">нет задачи в плане: статус не из чего вывести</p>')
        for t in linked:
            out.append(f'<p class="small"><span class="badge b-{t["color"]}">{_e(t["status_ru"])}</span> {_e(t["title_ru"])}</p>')
            out.append(_evidence_line(t, now))
            out.append(_check_block(t.get("check")))
        out.append("</div>")

    # ---- Week
    wk = next((w for w in plan.get("weeks", []) if _in_week(w["id"], today)), (plan.get("weeks") or [None])[0])
    out.append("<h2>Неделя</h2>")
    if wk:
        out.append(f'<div class="card"><b>{_e(wk["id"])}</b><p>{_e(wk.get("goal_ru"))}</p></div>')
        out.append("<h3>Ворота (цели недели)</h3>")
        for m in state["metrics"]:
            if m["id"] in wk.get("gates", []):
                lk = m["last_known"]
                if lk:
                    n_part = f"n={lk['n']}, " if lk.get("n") is not None else ""
                    val = lk["value"]
                    if isinstance(val, float) and (m.get("unit") == "share"):
                        val = f"{val * 100:.0f} %"
                    cur = f"{val} -> цель {m['target']} ({n_part}{lk.get('date')})"
                else:
                    cur = f"нет данных -> цель {m['target']}"
                colour = "grey" if m["state_ru"] != "по данным плана" else "blue"
                out.append(f'<div class="card"><div class="row"><div class="grow"><b>{_e(m["title_ru"])}</b>'
                           f'<p class="small">{_e(cur)}</p>'
                           + (f'<p class="small mute">{_e("; ".join(m["also_ru"]))}</p>' if m.get("also_ru") else "")
                           + f'</div><div>{_badge(colour, m["state_ru"])}</div></div></div>')
        week_tasks = [t for t in tasks if t.get("week") == wk["id"]]
        tracks: dict[str, list[dict[str, Any]]] = {}
        for t in week_tasks:
            tracks.setdefault(t["track"] or "—", []).append(t)
        for track in sorted(tracks):
            out.append(f"<h3>Трек: {_e(track)}</h3>")
            for t in tracks[track]:
                out.append(_task_card(t, now, with_check=False))
    # ---- Stages
    out.append("<h2>Этапы</h2>")
    for st in plan.get("stages", []):
        if st["id"] == "LATER":
            continue
        dates = st.get("dates")
        cur = bool(dates and dates[0] <= today <= dates[1])
        done = st.get("status_hint") == "done" or bool(dates and today > dates[1])
        dtxt = f"{dates[0]} .. {dates[1]}" if dates else "даты не заданы"
        if st.get("estimate"):
            dtxt += " (оценка)"
        out.append(f'<div class="stage{" cur" if cur else ""}"><b>{_e(st["title_ru"])}</b> '
                   + (_badge("blue", "мы здесь") if cur else _badge("grey", "завершён") if done else _badge("grey", "впереди"))
                   + f'<p class="small mute">{_e(dtxt)}</p>'
                   + (f'<p class="small">Ворота: {_e("; ".join(st["gates_ru"]))}</p>' if st.get("gates_ru") else "") + "</div>")
    later = next((s for s in plan.get("stages", []) if s["id"] == "LATER"), None)
    if later:
        out.append(f'<h3>{_e(later["title_ru"])}</h3><div class="card"><ul>'
                   + "".join(f"<li>{_e(i)}</li>" for i in later.get("items_ru", [])) + "</ul></div>")
    out.append("<h3>Месяцы</h3>")
    for mo in plan.get("months", []):
        out.append(f'<p class="small"><b>{_e(mo["id"])}</b>: {_e(mo["theme_ru"])}</p>')

    # ---- needs designer
    out.append("<h2>Ждёт вашего решения</h2>")
    items = state["needs_designer"]
    open_items = state["designer_open"]
    if not items and not open_items:
        out.append('<p class="mute">Ничего не ждёт.</p>')
    for it in items:
        where = it.get("task") or it.get("stage")
        out.append(f'<div class="card bar red"><b>{_e(it["question_ru"])}</b><p class="small mute">{_e(where)} · с {_e(it.get("asked"))}'
                   + (f' · <code>{_e(it["doc"])}</code>' if it.get("doc") else "") + "</p></div>")
    for it in open_items:
        out.append(f'<div class="card bar red"><b>{_e(it["question_ru"])}</b><p class="small mute">с {_e(it.get("asked"))}</p></div>')

    # ---- flags
    fl = state["flags"]
    out.append("<h2>Без задачи / расхождения</h2>")
    out.append("<h3>Ветки с работой за 48 ч без задачи или без линии</h3>")
    if not fl["unmatched"]:
        out.append('<p class="mute">Нет.</p>')
    for u in fl["unmatched"]:
        rs = [{"no_task": "без задачи в плане", "no_lane": "нет линии в LANES.json"}[r] for r in u["reasons"]]
        out.append(f'<div class="card bar red"><div class="row"><div class="grow"><code>{_e(u["branch"])}</code> @{_e(u["tip"])}'
                   f'<p class="small mute">{_e(u["subject"])} · {_e(_ago(u["ts"], now))}</p>'
                   + (f'<p class="small mute">задачи: {_e(", ".join(u["tasks"]))}</p>' if u["tasks"] else "")
                   + f'</div><div>{" ".join(_badge("red", r) for r in rs)}</div></div></div>')
    out.append("<h3>Расхождения источников</h3>")
    if not fl["discrepancies"]:
        out.append('<p class="mute">Нет.</p>')
    for d in fl["discrepancies"]:
        out.append(f'<div class="card bar red">{_e(d["text_ru"])}</div>')
    if fl["lane_stalled"]:
        out.append("<h3>Линии, помеченные зависшими (LANES.json мог устареть)</h3>")
        for l in fl["lane_stalled"]:
            out.append(f'<p class="small"><b>{_e(l["id"])}</b>: {_e(l["why"])}</p>')

    main = state["main"]
    out.append(f'<footer>Сформировано {_e(dt.datetime.fromtimestamp(now).strftime("%Y-%m-%d %H:%M:%S"))} · main {_e(main["head"])} ({_e(main["ref"])}) · '
               f'план от {_e(state.get("plan_updated"))} · сборщик progress_collect.py (P0)</footer>')
    out.append("</main></body></html>")
    return "".join(out)


def _in_week(week_id: str, today: str) -> bool:
    m = re.match(r"^(\d{4})-(\d{2})-(\d{2})\.\.(\d{2})$", week_id)
    if not m:
        return False
    start = f"{m.group(1)}-{m.group(2)}-{m.group(3)}"
    end = f"{m.group(1)}-{m.group(2)}-{m.group(4)}"
    return start <= today <= end


# --------------------------------------------------------------------------------------------------------------------
# cli
# --------------------------------------------------------------------------------------------------------------------
def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Progress collector (P0): folds git / chat / lanes / day plan into Saved/Progress.")
    ap.add_argument("--rebuild", action="store_true", help="forget events and cursors, rebuild everything from the sources")
    ap.add_argument("--once", action="store_true", help="run one pass and exit (default: loop every 30 s)")
    ap.add_argument("--html", default=None, help="also write the self-contained progress page to this path")
    ap.add_argument("--interval", type=int, default=30)
    ap.add_argument("--repo", default=REPO_ROOT)
    args = ap.parse_args(argv)

    html_path = os.path.abspath(args.html) if args.html else None
    col = Collector(repo=args.repo)
    lock = CollectorLock(os.path.join(col.out_dir, "collector.lock"))
    if not lock.acquire():
        print("progress_collect: another collector holds the lock, exiting", file=sys.stderr)
        return 3
    try:
        rebuild = args.rebuild
        while True:
            col.now = int(time.time())
            state = col.run_once(rebuild=rebuild, html_path=html_path)
            rebuild = False
            print(f"progress_collect: {len(state['tasks'])} tasks, {len(state['day_plan']['rows'])} day rows, main {state['main']['head']}")
            if args.once:
                return 0
            lock.touch()
            time.sleep(args.interval)
    finally:
        lock.release()


if __name__ == "__main__":
    sys.exit(main())
