#!/usr/bin/env python3
"""Live task board builder for CodexTactics Agent Chat.

Derives Saved/AgentChat/task_board.json read-only from:
  1. Task files: docs/port/{codex,gemini}/inbox/*.md + handover moves in INTERIM_LEAD.md
  2. Reports: docs/port/{codex,gemini}/outbox/*report*
  3. Git branches & merges: codex/*, gemini/0NN-* -> interim / main
  4. Chat history: Saved/AgentChat/chat.jsonl linked by --topic <task-id> or slug
  5. Mechanics register: docs/port/mechanics_register.json (linked + unmatched 'not started')

Standard library only.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import subprocess
import sys
import tempfile
from typing import Any, Callable

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(TOOLS_DIR, "..", ".."))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import agent_chat  # noqa: E402

IN_PROGRESS_WINDOW_SEC = 2 * 3600  # 2 hours

_TASK_FILENAME_RE = re.compile(r"^(\d+)-([a-zA-Z0-9_-]+)\.md$")
_MOVE_LINE_RE = re.compile(
    r"\b(\d{3})\b[^.\n]*?\bMOVED\s+to\s+(Gemini|Codex|Claude)\b(?:[^.\n]*?\btask\s+(\d{3}))?",
    re.IGNORECASE,
)
_URL_RE = re.compile(r"https?://\S+", re.IGNORECASE)
_QUESTION_WORDS_RE = re.compile(r"(?:\bвопрос\b|\bquestion\b|подтверди|выбери вариант|нужно решение)", re.IGNORECASE)
_BLOCKER_WORDS_RE = re.compile(r"(?:\bblocker\b|\bблокер\b|\bfatal\b|не собирается|build failed)", re.IGNORECASE)

_GENERIC_TOKENS = frozenset(
    {
        "the",
        "and",
        "for",
        "with",
        "from",
        "mode",
        "game",
        "task",
        "start",
        "night",
        "day",
        "queue",
        "build",
        "tool",
        "tools",
        "docs",
        "tests",
        "test",
        "trial",
        "cpp",
        "fix",
        "live",
        "page",
    }
)


def parse_iso_timestamp(ts_str: Any) -> float | None:
    if not ts_str:
        return None
    s = str(ts_str).strip()
    if not s:
        return None
    if s.endswith("Z"):
        s = s[:-1] + "+00:00"
    try:
        parsed = dt.datetime.fromisoformat(s)
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=dt.timezone.utc)
        return parsed.timestamp()
    except ValueError:
        return None


def _extract_markdown_title(file_path: str, fallback: str) -> tuple[str, str]:
    """Returns (title, preview_text) from a markdown task or report file."""
    try:
        with open(file_path, "r", encoding="utf-8", errors="replace") as fh:
            lines = [fh.readline() for _ in range(12)]
    except OSError:
        return fallback, ""
    title = ""
    for line in lines:
        s = line.strip()
        if s.startswith("#"):
            title = s.lstrip("#").strip()
            break
    if not title:
        for line in lines:
            s = line.strip()
            if s:
                title = s
                break
    return (title or fallback), "\n".join(lines)


def parse_lead_moves(repo_root: str) -> dict[str, dict[str, str]]:
    """Extracts task ownership moves from INTERIM_LEAD.md and RESOURCES.md."""
    moves: dict[str, dict[str, str]] = {}
    for rel in ("docs/port/codex/INTERIM_LEAD.md", "docs/port/RESOURCES.md"):
        full = os.path.join(repo_root, rel)
        if not os.path.exists(full):
            continue
        try:
            with open(full, "r", encoding="utf-8", errors="replace") as fh:
                text = fh.read()
        except OSError:
            continue
        for match in _MOVE_LINE_RE.finditer(text):
            src_num = match.group(1)
            new_owner = match.group(2).lower()
            target_num = match.group(3)
            moves[f"codex-{src_num}"] = {
                "owner": new_owner,
                "moved_to": f"{new_owner}-{target_num}" if target_num else new_owner,
                "source_doc": rel,
            }
    return moves


def scan_inbox_tasks(repo_root: str, moves: dict[str, dict[str, str]]) -> list[dict[str, Any]]:
    """Scans docs/port/{codex,gemini}/inbox/*.md for tasks."""
    tasks: list[dict[str, Any]] = []
    for folder_owner in ("codex", "gemini"):
        inbox_dir = os.path.join(repo_root, "docs", "port", folder_owner, "inbox")
        if not os.path.isdir(inbox_dir):
            continue
        for fname in sorted(os.listdir(inbox_dir)):
            if not fname.endswith(".md") or fname in (".gitkeep", "README.md"):
                continue
            m = _TASK_FILENAME_RE.match(fname)
            if not m:
                continue
            num, slug = m.group(1), m.group(2)
            task_id = f"{folder_owner}-{num}"
            full_path = os.path.join(inbox_dir, fname)
            rel_path = f"docs/port/{folder_owner}/inbox/{fname}"
            title, preview = _extract_markdown_title(full_path, fallback=fname)
            owner = folder_owner
            moved_note = ""
            if task_id in moves:
                owner = moves[task_id]["owner"]
                moved_note = f"moved -> {moves[task_id]['moved_to']}"
            tasks.append(
                {
                    "id": task_id,
                    "num": num,
                    "slug": slug,
                    "folder_owner": folder_owner,
                    "owner": owner,
                    "moved_note": moved_note,
                    "title": title,
                    "preview": preview,
                    "task_file": rel_path,
                }
            )
    return tasks


def scan_outbox_reports(repo_root: str) -> dict[str, dict[str, str]]:
    """Indexes reports in docs/port/{codex,gemini}/outbox/ by owner+num and stem."""
    reports: dict[str, dict[str, str]] = {}
    for folder_owner in ("codex", "gemini"):
        outbox_dir = os.path.join(repo_root, "docs", "port", folder_owner, "outbox")
        if not os.path.isdir(outbox_dir):
            continue
        for fname in sorted(os.listdir(outbox_dir)):
            if not fname.endswith(".md") or "report" not in fname.lower():
                continue
            rel_path = f"docs/port/{folder_owner}/outbox/{fname}"
            num_m = re.match(r"^(\d+)-", fname)
            if num_m:
                num = num_m.group(1)
                stem = fname[:-3]
                if stem.endswith(".report"):
                    stem = stem[:-7]
                reports[f"{folder_owner}-{num}:{stem}"] = {"report_file": rel_path, "stem": stem, "owner": folder_owner, "num": num}
                reports.setdefault(f"{folder_owner}-{num}", {"report_file": rel_path, "stem": stem, "owner": folder_owner, "num": num})
    return reports


def find_task_report(task: dict[str, Any], reports_index: dict[str, dict[str, str]]) -> str | None:
    folder_owner = task["folder_owner"]
    num = task["num"]
    slug = task["slug"]
    exact_key = f"{folder_owner}-{num}:{num}-{slug}"
    if exact_key in reports_index:
        return reports_index[exact_key]["report_file"]
    prefix_key = f"{folder_owner}-{num}"
    if prefix_key in reports_index:
        return reports_index[prefix_key]["report_file"]
    return None


def default_git_snapshot(repo_root: str) -> dict[str, Any]:
    """Collects branch heads, commit timestamps/hashes, and merge status in interim/main."""
    snapshot: dict[str, Any] = {"branches": [], "merged_interim": set(), "merged_main": set(), "main_subjects": []}
    if not os.path.isdir(repo_root):
        return snapshot
    try:
        fmt = "%(refname:short)|%(objectname:short)|%(committerdate:unix)|%(subject)"
        out = subprocess.check_output(
            ["git", "-C", repo_root, "for-each-ref", "--format=" + fmt, "refs/heads", "refs/remotes/origin"],
            stderr=subprocess.DEVNULL,
            encoding="utf-8",
            errors="replace",
        )
        seen_names = set()
        for line in out.splitlines():
            parts = line.split("|", 3)
            if len(parts) < 4:
                continue
            name, sha, ts_s, subj = parts
            if name.startswith("origin/"):
                short = name[len("origin/"):]
            else:
                short = name
            if short == "HEAD" or short in seen_names:
                continue
            seen_names.add(short)
            try:
                ts_val = float(ts_s)
            except ValueError:
                ts_val = 0.0
            snapshot["branches"].append({"name": short, "commit": sha, "ts": ts_val, "subject": subj})
    except Exception:
        pass

    for ref_name, target_key in (("interim/2026-11-12", "merged_interim"), ("main", "merged_main")):
        for ref_try in (ref_name, f"origin/{ref_name}"):
            try:
                out = subprocess.check_output(
                    ["git", "-C", repo_root, "branch", "-a", "--merged", ref_try],
                    stderr=subprocess.DEVNULL,
                    encoding="utf-8",
                    errors="replace",
                )
                for line in out.splitlines():
                    br = line.lstrip("* ").strip()
                    if br.startswith("remotes/origin/"):
                        br = br[len("remotes/origin/"):]
                    snapshot[target_key].add(br)
                break
            except Exception:
                continue

    try:
        out = subprocess.check_output(
            ["git", "-C", repo_root, "log", "-n", "120", "--format=%h|%ct|%s", "origin/main"],
            stderr=subprocess.DEVNULL,
            encoding="utf-8",
            errors="replace",
        )
        for line in out.splitlines():
            parts = line.split("|", 2)
            if len(parts) == 3:
                snapshot["main_subjects"].append({"commit": parts[0], "ts": float(parts[1]), "subject": parts[2]})
    except Exception:
        pass
    return snapshot


def match_git_for_task(task: dict[str, Any], git_snap: dict[str, Any]) -> dict[str, Any]:
    task_id = task["id"].lower()
    folder_owner = task["folder_owner"].lower()
    num = task["num"]
    slug_tokens = [t for t in task["slug"].lower().split("-") if len(t) >= 3 and t not in _GENERIC_TOKENS]

    matched_branch = None
    best_ts = -1.0
    for br in git_snap.get("branches") or []:
        name = br["name"].lower()
        # Match patterns like gemini/030-dispatcher, gemini/029-audio-tool, codex/008-...
        if name.startswith(f"{folder_owner}/{num}-") or name == f"{folder_owner}/{num}" or f"{folder_owner}/{num}" in name:
            if br["ts"] > best_ts:
                best_ts = br["ts"]
                matched_branch = br
        elif name.startswith(f"{task['owner'].lower()}/") and slug_tokens and all(tok in name for tok in slug_tokens[:2]):
            if br["ts"] > best_ts:
                best_ts = br["ts"]
                matched_branch = br

    in_main = False
    in_interim = False
    last_commit = None
    last_commit_ts = None
    branch_name = None

    if matched_branch:
        branch_name = matched_branch["name"]
        last_commit = matched_branch["commit"]
        last_commit_ts = matched_branch["ts"]
        if branch_name in git_snap.get("merged_main", set()):
            in_main = True
        if branch_name in git_snap.get("merged_interim", set()):
            in_interim = True

    return {
        "branch": branch_name,
        "last_commit": last_commit,
        "last_commit_ts": last_commit_ts,
        "in_interim": in_interim,
        "in_main": in_main,
    }


def _message_matches_task(msg: dict[str, Any], task: dict[str, Any]) -> bool:
    topic = str(msg.get("topic") or "").strip().lower()
    text = str(msg.get("text") or "").strip().lower()
    task_id = task["id"].lower()
    owner = task["folder_owner"].lower()
    num = task["num"]
    slug = task["slug"].lower()

    if task_id in topic:
        return True
    if f"{owner}/{num}" in topic or f"{owner}-{num}" in topic:
        return True
    if f"/{num}-{slug}" in text or f"docs/port/{owner}/inbox/{num}-" in text:
        return True
    if topic and slug and slug in topic:
        return True
    # Also allow `<slug_word>-<num>` such as `dispatcher-030` or `audio-029`
    if re.search(rf"\b{re.escape(num)}\b", topic) and owner in (
        str(msg.get("from") or "").lower(),
        str(msg.get("to") or "").lower(),
    ):
        return True
    return False


def match_chat_for_task(task: dict[str, Any], messages: list[dict[str, Any]]) -> dict[str, Any]:
    matched = [m for m in messages if _message_matches_task(m, task)]
    if not matched:
        return {
            "chat_count": 0,
            "last_chat_ts": None,
            "last_chat_text": "",
            "last_chat_from": "",
            "last_chat_topic": "",
            "asked_question": False,
            "blocked": False,
        }
    last = matched[-1]
    last_text = str(last.get("text") or "")
    text_no_urls = _URL_RE.sub("", last_text)
    last_to = str(last.get("to") or "").lower()
    asked_question = bool(
        ("?" in text_no_urls or _QUESTION_WORDS_RE.search(text_no_urls))
        and last_to in ("designer", "claude", "all")
    )
    blocked = bool(_BLOCKER_WORDS_RE.search(last_text.lower()))
    return {
        "chat_count": len(matched),
        "last_chat_ts": parse_iso_timestamp(last.get("ts")),
        "last_chat_text": last_text[:200],
        "last_chat_from": str(last.get("from") or ""),
        "last_chat_topic": str(last.get("topic") or ""),
        "asked_question": asked_question,
        "blocked": blocked,
    }


def load_mechanics_register(repo_root: str) -> list[dict[str, str]]:
    reg_path = os.path.join(repo_root, "docs", "port", "mechanics_register.json")
    if not os.path.exists(reg_path):
        return []
    try:
        with open(reg_path, "r", encoding="utf-8") as fh:
            raw = json.load(fh)
    except (OSError, json.JSONDecodeError):
        return []
    items: list[dict[str, str]] = []
    for grp in raw.get("groups") or []:
        grp_id = str(grp.get("id") or "")
        grp_title = str(grp.get("title") or "")
        for it in grp.get("items") or []:
            items.append(
                {
                    "id": str(it.get("id") or ""),
                    "name": str(it.get("name") or ""),
                    "status": str(it.get("status") or ""),
                    "doc": str(it.get("doc") or ""),
                    "group_id": grp_id,
                    "group_title": grp_title,
                }
            )
    return items


def match_register_for_task(task: dict[str, Any], register_items: list[dict[str, str]]) -> list[dict[str, str]]:
    slug_parts = [p for p in task["slug"].lower().split("-") if len(p) >= 4 and p not in _GENERIC_TOKENS]
    title_low = (task["title"] + " " + task.get("preview", "")).lower()
    matches: list[dict[str, str]] = []
    for reg in register_items:
        reg_tokens = [p for p in reg["id"].lower().split("-") if len(p) >= 4 and p not in _GENERIC_TOKENS]
        doc_base = os.path.basename(reg["doc"]).replace(".md", "").lower() if reg["doc"] else ""
        matched = False
        if doc_base and len(doc_base) >= 5 and doc_base in (task["slug"].lower() + " " + title_low):
            matched = True
        elif reg_tokens and any(tok in slug_parts for tok in reg_tokens):
            matched = True
        if matched:
            matches.append(reg)
    return matches


def compute_task_state(
    has_report: bool,
    in_interim: bool,
    in_main: bool,
    asked_question: bool,
    blocked: bool,
    last_commit_ts: float | None,
    last_chat_ts: float | None,
    now_ts: float,
) -> tuple[str, float | None, int | None]:
    """Computes (state, last_activity_ts, idle_min)."""
    candidates = [t for t in (last_commit_ts, last_chat_ts) if t is not None and t > 0]
    last_activity_ts = max(candidates) if candidates else None
    idle_min = max(0, int((now_ts - last_activity_ts) / 60)) if last_activity_ts is not None else None

    if in_main:
        return "in_main", last_activity_ts, idle_min
    if in_interim:
        return "in_interim", last_activity_ts, idle_min
    if has_report:
        return "done", last_activity_ts, idle_min
    if asked_question or blocked:
        return "waiting", last_activity_ts, idle_min
    if last_activity_ts is not None and (now_ts - last_activity_ts) <= IN_PROGRESS_WINDOW_SEC:
        return "in_progress", last_activity_ts, idle_min
    return "queued", last_activity_ts, idle_min


def build_task_board(
    repo_root: str = REPO_ROOT,
    chat_file: str | None = None,
    now_ts: float | None = None,
    git_provider: Callable[[str], dict[str, Any]] | None = None,
) -> dict[str, Any]:
    ref_now = now_ts if now_ts is not None else dt.datetime.now(dt.timezone.utc).timestamp()
    resolved_chat = chat_file or os.path.join(agent_chat.get_storage_dir(), "chat.jsonl")

    moves = parse_lead_moves(repo_root)
    raw_tasks = scan_inbox_tasks(repo_root, moves)
    reports_index = scan_outbox_reports(repo_root)
    git_snap = git_provider(repo_root) if git_provider is not None else default_git_snapshot(repo_root)
    messages = agent_chat.read_all_messages(resolved_chat)
    register_items = load_mechanics_register(repo_root)

    matched_reg_ids: set[str] = set()
    board_tasks: list[dict[str, Any]] = []

    for task in raw_tasks:
        report_file = find_task_report(task, reports_index)
        git_info = match_git_for_task(task, git_snap)
        chat_info = match_chat_for_task(task, messages)
        reg_matches = match_register_for_task(task, register_items)
        for rm in reg_matches:
            matched_reg_ids.add(rm["id"])

        state, last_act_ts, idle_min = compute_task_state(
            has_report=bool(report_file),
            in_interim=git_info["in_interim"],
            in_main=git_info["in_main"],
            asked_question=chat_info["asked_question"],
            blocked=chat_info["blocked"],
            last_commit_ts=git_info["last_commit_ts"],
            last_chat_ts=chat_info["last_chat_ts"],
            now_ts=ref_now,
        )
        title_ru = reg_matches[0]["name"] if reg_matches else ""
        area = reg_matches[0]["group_title"] if reg_matches else "Tools / Port"
        board_tasks.append(
            {
                "id": task["id"],
                "num": task["num"],
                "slug": task["slug"],
                "owner": task["owner"],
                "moved_note": task["moved_note"],
                "title": task["title"],
                "title_ru": title_ru,
                "area": area,
                "state": state,
                "task_file": task["task_file"],
                "report_file": report_file,
                "branch": git_info["branch"],
                "last_commit": git_info["last_commit"],
                "last_commit_ts": git_info["last_commit_ts"],
                "in_interim": git_info["in_interim"],
                "in_main": git_info["in_main"],
                "chat_count": chat_info["chat_count"],
                "last_chat_ts": chat_info["last_chat_ts"],
                "last_chat_text": chat_info["last_chat_text"],
                "last_chat_topic": chat_info["last_chat_topic"],
                "asked_question": chat_info["asked_question"],
                "last_activity_ts": last_act_ts,
                "idle_min": idle_min,
                "register_items": [
                    {"id": r["id"], "name": r["name"], "group_title": r["group_title"], "status": r["status"]}
                    for r in reg_matches[:4]
                ],
            }
        )

    # Sort newest/active tasks first per state cleanly
    board_tasks.sort(key=lambda t: (t["last_activity_ts"] or 0.0, t["num"]), reverse=True)

    # Unmatched register entries marked as "not started" (no inbox task linked)
    not_started_register = [
        {
            "id": r["id"],
            "name": r["name"],
            "area": r["group_title"],
            "group_id": r["group_id"],
            "register_status": r["status"],
            "doc": r["doc"],
            "state": "not_started",
        }
        for r in register_items
        if r["id"] not in matched_reg_ids
    ]

    # Per-agent "Сейчас" strip
    now_by_agent: dict[str, dict[str, Any]] = {}
    for agent_name in ("codex", "gemini", "claude"):
        agent_chats = [
            parse_iso_timestamp(m.get("ts"))
            for m in messages
            if str(m.get("from") or "").lower() == agent_name
        ]
        agent_chats = [t for t in agent_chats if t is not None]
        agent_commits = [
            float(br["ts"])
            for br in (git_snap.get("branches") or [])
            if br["name"].lower().startswith(f"{agent_name}/")
        ]
        last_agent_ts = max(agent_chats + agent_commits) if (agent_chats or agent_commits) else None
        agent_idle = max(0, int((ref_now - last_agent_ts) / 60)) if last_agent_ts is not None else None

        agent_tasks = [t for t in board_tasks if t["owner"] == agent_name]
        active = next((t for t in agent_tasks if t["state"] in ("in_progress", "waiting")), None)
        if active is None:
            active = next((t for t in agent_tasks if t["state"] == "queued"), None)
        now_by_agent[agent_name] = {
            "agent": agent_name,
            "task_id": active["id"] if active else None,
            "task_title": (active["title_ru"] or active["title"]) if active else None,
            "state": active["state"] if active else "idle",
            "idle_min": agent_idle,
        }

    return {
        "version": 1,
        "generated_at": dt.datetime.fromtimestamp(ref_now, tz=dt.timezone.utc).isoformat(timespec="seconds"),
        "now_strip": now_by_agent,
        "tasks": board_tasks,
        "not_started_register": not_started_register,
        "areas": sorted({t["area"] for t in board_tasks} | {r["area"] for r in not_started_register if r["area"]}),
    }


def save_board_atomic(board_file: str, board: dict[str, Any]) -> None:
    os.makedirs(os.path.dirname(board_file), exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=".task_board_", suffix=".tmp", dir=os.path.dirname(board_file))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            json.dump(board, fh, indent=2, ensure_ascii=False)
            fh.write("\n")
        os.replace(tmp_name, board_file)
    except Exception:
        if os.path.exists(tmp_name):
            os.unlink(tmp_name)
        raise


def build_and_save_board(
    repo_root: str = REPO_ROOT,
    storage_dir: str | None = None,
    chat_file: str | None = None,
    now_ts: float | None = None,
    git_provider: Callable[[str], dict[str, Any]] | None = None,
) -> dict[str, Any]:
    sdir = storage_dir or agent_chat.get_storage_dir()
    board_path = os.path.join(sdir, "task_board.json")
    resolved_chat = chat_file or os.path.join(sdir, "chat.jsonl")
    board = build_task_board(
        repo_root=repo_root,
        chat_file=resolved_chat,
        now_ts=now_ts,
        git_provider=git_provider,
    )
    save_board_atomic(board_path, board)
    return board


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Build Saved/AgentChat/task_board.json")
    ap.add_argument("--repo-root", default=REPO_ROOT)
    ap.add_argument("--storage-dir", default=None)
    ap.add_argument("--out", default=None)
    args = ap.parse_args(argv)

    board = build_and_save_board(repo_root=args.repo_root, storage_dir=args.storage_dir)
    if args.out:
        save_board_atomic(args.out, board)
    print(f"task_board: {len(board['tasks'])} tasks, {len(board['not_started_register'])} register items")
    return 0


if __name__ == "__main__":
    sys.exit(main())
