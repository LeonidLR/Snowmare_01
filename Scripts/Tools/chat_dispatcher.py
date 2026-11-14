#!/usr/bin/env python3
"""Chat dispatcher daemon: classifies new chat messages and wakes recipients safely.

Reads Saved/AgentChat/chat.jsonl via agent_chat.py storage rules, classifies messages
using deterministic rules by default (keywords, addressee, question/blocker),
enforces rate limits, quiet hours, spend caps, kill switch (STOP), and forwards
messages VERBATIM with fixed attribution header. Standard library only.
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
import time
from pathlib import Path
from typing import Any, Callable

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(TOOLS_DIR, "..", ".."))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import agent_chat  # noqa: E402

try:
    import codex_wake_helper  # type: ignore[import-not-found] # noqa: E402
except ImportError:
    class _CodexWakeFallback:
        DEFAULT_INTERIM_NAME = "CodexTactics-interim"
        DEFAULT_INTERIM_BRANCH = "interim/2026-11-12"
        HARD_SAFETY_RULES_BLOCK = (
            "ЖЁСТКИЕ ПРАВИЛА БЕЗОПАСНОСТИ (НЕ НАРУШАТЬ):\n"
            "- НИКОГДА не коммить, не перемещай, не удаляй, не делай `git stash` / `git clean` для паков `Content/*` "
            "(кроме `Content/Data/`), `Config/**`, карт `Content/Maps/**` и `.uasset`.\n"
            "- Никогда не трогай ветку `main` и основной каталог (`MAIN`); работай, коммить и тестируй только в `interim/2026-11-12`.\n"
            "- Не пушить в remote. Прогонять тесты через `Scripts/test.ps1`."
        )

        @staticmethod
        def _git_branch_name(worktree_dir: str) -> str:
            try:
                out = subprocess.check_output(
                    ["git", "-C", worktree_dir, "branch", "--show-current"],
                    stderr=subprocess.DEVNULL,
                    encoding="utf-8",
                    errors="replace",
                )
                return out.strip()
            except Exception:
                return ""

        @classmethod
        def find_interim_worktree(cls, project_root: str | None = None, expected_branch: str = DEFAULT_INTERIM_BRANCH) -> str:
            root = os.path.abspath(project_root or REPO_ROOT)
            expected_ref = f"refs/heads/{expected_branch}"
            try:
                out = subprocess.check_output(
                    ["git", "-C", root, "worktree", "list", "--porcelain"],
                    stderr=subprocess.DEVNULL,
                    encoding="utf-8",
                    errors="replace",
                )
                wt, br = None, None
                for raw_line in out.splitlines() + [""]:
                    line = raw_line.strip()
                    if not line:
                        if wt and br == expected_ref and os.path.isdir(wt):
                            return wt
                        wt, br = None, None
                    elif line.startswith("worktree "):
                        wt = line.split(" ", 1)[1].strip()
                    elif line.startswith("branch "):
                        br = line.split(" ", 1)[1].strip()
            except Exception:
                pass
            candidate = os.path.join(os.path.dirname(root), cls.DEFAULT_INTERIM_NAME)
            if os.path.isdir(candidate) and cls._git_branch_name(candidate) == expected_branch:
                return candidate
            return ""

        @staticmethod
        def parse_iso_timestamp(ts_str: Any) -> float | None:
            if not ts_str:
                return None
            s = str(ts_str).strip()
            if s.endswith("Z"):
                s = s[:-1] + "+00:00"
            try:
                parsed = dt.datetime.fromisoformat(s)
                if parsed.tzinfo is None:
                    parsed = parsed.replace(tzinfo=dt.timezone.utc)
                return parsed.timestamp()
            except Exception:
                return None

    codex_wake_helper = _CodexWakeFallback()  # type: ignore[assignment]

VALID_CATEGORIES = ("status", "task", "question", "blocker", "merge-request")
ACTIONABLE_CATEGORIES = ("task", "question", "blocker", "merge-request")
CLAUDE_WAKE_CATEGORIES = ("question", "blocker", "merge-request")
AGENTS = ("claude", "codex", "gemini", "designer")

DEFAULT_CONFIG_PATH = os.path.join(TOOLS_DIR, "chat_dispatcher.json")

_URL_RE = re.compile(r"https?://\S+", re.IGNORECASE)
_META_TARGET_RE = re.compile(
    r"(?:\bдиспетчер\b|\bdispatcher\b|\bchat_dispatcher\b)", re.IGNORECASE
)
_META_UNSAFE_ACTION_RE = re.compile(
    r"(?:измени|отключи|удали|смени|обойди|игнорируй|change|disable|remove|override|bypass|ignore)"
    r".{0,40}"
    r"(?:правил|ключ|расписани|лимит|бюджет|тихие\s+час|стоп|rules?|keys?|api_key|schedules?|stop|budget|quiet)",
    re.IGNORECASE,
)


def load_config(config_path: str | None = None) -> dict[str, Any]:
    path = config_path or DEFAULT_CONFIG_PATH
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def resolve_repo_path(path_value: str, base_root: str) -> str:
    if os.path.isabs(path_value):
        return os.path.abspath(path_value)
    return os.path.abspath(os.path.join(base_root, path_value))


def redact_secrets(text: str) -> str:
    out = text
    for env_name in ("GEMINI_API_KEY", "ANTHROPIC_API_KEY", "AGENT_CHAT_TOKEN", "OPENAI_API_KEY"):
        val = os.environ.get(env_name, "").strip()
        if val:
            out = out.replace(val, "[REDACTED]")
    return out


def is_unsafe_meta_instruction(msg: dict[str, Any]) -> bool:
    """Rejects messages attempting to instruct the running dispatcher to alter rules, keys, or schedules."""
    text = str(msg.get("text") or "")
    topic = str(msg.get("topic") or "")
    if _META_UNSAFE_ACTION_RE.search(text) and (_META_TARGET_RE.search(text) or topic == "dispatcher-control"):
        return True
    return False


def classify_message_keywords(msg: dict[str, Any]) -> str:
    """Deterministic rules classification into status|task|question|blocker|merge-request.

    Uses keywords, addressee (`from`/`to`/`topic`), question markers, and blocker markers.
    """
    if is_unsafe_meta_instruction(msg):
        return "status"

    text = str(msg.get("text") or "").strip()
    low = text.lower()
    topic = str(msg.get("topic") or "").lower()
    sender = str(msg.get("from") or "").strip().lower()
    recipient = str(msg.get("to") or "").strip().lower()

    # Check blockers first (only genuine urgency/breakage)
    blocker_patterns = (
        r"\bblocker\b",
        r"\bблокер\b",
        r"\bfatal\b",
        r"\bcrash(?:ed|ing)?\b",
        r"ошибка\s+сборки",
        r"build\s+failed",
        r"не\s+собирается",
        r"опасн(?:ый|о)\s+баг",
        r"сломал(?:ось|и)?\b",
    )
    if any(re.search(pat, low) for pat in blocker_patterns) or "blocker" in topic:
        return "blocker"

    # Merge / branch review requests
    merge_patterns = (
        r"\bmerge-request\b",
        r"\bready\s+for\s+review\b",
        r"\breview\s+branch\b",
        r"\bмерж\b",
        r"\bвлит[ьи]\b",
        r"\bволью\b",
        r"\bвлей\b",
        r"ревью\s+ветк",
        r"проверь\s+и\s+вол",
    )
    if any(re.search(pat, low) for pat in merge_patterns) or "merge-review" in topic:
        return "merge-request"

    # Questions (ignore `?` inside URLs)
    text_no_urls = _URL_RE.sub("", text)
    if "?" in text_no_urls or any(
        w in low for w in ("вопрос", "выбери вариант", "подтверди", "question:", "нужно решение")
    ):
        return "question"

    # Pure concise done-reports ("N done <hash>") are status updates unless requesting merge/review
    if re.match(r"^(?:[\w-]+\s+)?done\b(?:\s+[0-9a-f]{6,12})?\b", low) and "review" not in low:
        return "status"

    # Tasks vs status updates (taking addressee + task directives into account)
    task_patterns = (
        r"\bсделай\b",
        r"\bисправь\b",
        r"\bреализуй\b",
        r"\bдобавь\b",
        r"\bнапиши\b",
        r"\bпроверь\b",
        r"\bзадача\b",
        r"теперь\s+обязательно",
        r"следующая:",
        r"твоя\s+очередь",
        r"решение\s+дизайнера:",
        r"\bimplement\b",
        r"\bfix\b",
        r"\badd\b",
        r"docs/port/(?:codex|gemini)/inbox/",
    )
    if any(re.search(pat, low) for pat in task_patterns):
        return "task"
    if sender in ("claude", "designer") and recipient in ("codex", "gemini") and "очередь" in low:
        return "task"

    return "status"


def classify_message_local(
    msg: dict[str, Any],
    model: str = "codex-local",
    timeout_sec: int = 20,
) -> str | None:
    """Calls local_task.py to classify chat message; returns None if offline or invalid."""
    local_script = os.path.join(TOOLS_DIR, "local_task.py")
    if not os.path.exists(local_script):
        return None
    prompt = (
        "Classify the following project chat message into EXACTLY ONE category word from this set:\n"
        "status | task | question | blocker | merge-request\n\n"
        "Rules:\n"
        "- Reply with ONLY the single category word, nothing else.\n"
        "- 'status': pure progress update, confirmation ('принял', 'готово'), done report without merge request.\n"
        "- 'task': assigning or requesting implementation/fixes/work.\n"
        "- 'question': asking for decision, clarification, confirmation.\n"
        "- 'blocker': urgent broken build, crash, fatal/dangerous bug.\n"
        "- 'merge-request': asking to review and merge a branch.\n\n"
        f"from: {msg.get('from')}\n"
        f"to: {msg.get('to')}\n"
        f"topic: {msg.get('topic')}\n"
        f"text: {msg.get('text')}\n"
    )
    try:
        proc = subprocess.run(
            [sys.executable, local_script, "--prompt", prompt, "--model", model, "--timeout", str(timeout_sec)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            timeout=timeout_sec + 5,
        )
        if proc.returncode != 0:
            return None
        raw = (proc.stdout or "").strip().lower()
        for cat in VALID_CATEGORIES:
            if re.search(rf"\b{re.escape(cat)}\b", raw):
                return cat
    except Exception:
        return None
    return None


def classify_message(
    msg: dict[str, Any],
    cfg: dict[str, Any],
    classifier_fn: Callable[[dict[str, Any]], str | None] | None = None,
) -> tuple[str, str]:
    """Returns (category, source) where source is 'safety_guard' | 'rules' | 'custom' | 'local_model'."""
    if is_unsafe_meta_instruction(msg):
        return "status", "safety_guard"
    if classifier_fn is not None:
        cat = classifier_fn(msg)
        if cat in VALID_CATEGORIES:
            return cat, "custom"
    mode = str(cfg.get("classifier_mode") or "rules").strip().lower()
    if mode == "local_model":
        model = str(cfg.get("local_model") or "").strip()
        if model:
            cat = classify_message_local(
                msg,
                model=model,
                timeout_sec=int(cfg.get("local_model_timeout_sec", 20)),
            )
            if cat in VALID_CATEGORIES:
                return cat, "local_model"
    return classify_message_keywords(msg), "rules"


def format_forwarded_message(msg: dict[str, Any]) -> str:
    """Strictly preserves exact original chat text with fixed attribution header."""
    msg_id = msg.get("id", 0)
    sender = msg.get("from", "unknown")
    verbatim_text = str(msg.get("text") or "")
    return f"[переслал диспетчер, оригинал #{msg_id} от {sender}]\n{verbatim_text}"


def is_in_quiet_hours(now_dt: dt.datetime, cfg: dict[str, Any]) -> bool:
    qh = cfg.get("quiet_hours") or {}
    if not qh.get("enabled", True):
        return False

    def _to_min(hhmm: str) -> int:
        parts = str(hhmm).strip().split(":")
        return int(parts[0]) * 60 + (int(parts[1]) if len(parts) > 1 else 0)

    start_min = _to_min(qh.get("start", "23:00"))
    end_min = _to_min(qh.get("end", "07:00"))
    cur_min = now_dt.hour * 60 + now_dt.minute
    if start_min == end_min:
        return False
    if start_min < end_min:
        return start_min <= cur_min < end_min
    return cur_min >= start_min or cur_min < end_min


def load_weekly_spend_usd(spend_file: str, now_ts: float, window_days: int = 7) -> float:
    if not os.path.exists(spend_file):
        return 0.0
    cutoff = now_ts - window_days * 86400
    total = 0.0
    with open(spend_file, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
                ts = float(rec.get("ts", 0.0))
                if ts >= cutoff:
                    total += float(rec.get("usd", 0.0))
            except (ValueError, TypeError, json.JSONDecodeError):
                continue
    return total


def _default_state() -> dict[str, Any]:
    return {
        "version": 1,
        "last_processed_id": 0,
        "last_wake_ts": {},
        "pending_gemini": [],
        "codex_runs": 0,
        "gemini_runs": 0,
    }


def load_state(state_file: str) -> dict[str, Any]:
    if not os.path.exists(state_file):
        return _default_state()
    try:
        with open(state_file, "r", encoding="utf-8") as fh:
            data = json.load(fh)
        if isinstance(data, dict):
            base = _default_state()
            base.update(data)
            return base
    except (OSError, json.JSONDecodeError):
        pass
    return _default_state()


def save_state_atomic(state_file: str, state: dict[str, Any]) -> None:
    os.makedirs(os.path.dirname(state_file), exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=".state_", suffix=".tmp", dir=os.path.dirname(state_file))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            json.dump(state, fh, indent=2, ensure_ascii=False)
            fh.write("\n")
        os.replace(tmp_name, state_file)
    except Exception:
        if os.path.exists(tmp_name):
            os.unlink(tmp_name)
        raise


def default_is_process_running(process_names: list[str]) -> bool:
    if sys.platform != "win32":
        return False
    try:
        out = subprocess.check_output(
            ["tasklist", "/FO", "CSV", "/NH"],
            stderr=subprocess.DEVNULL,
            encoding="utf-8",
            errors="replace",
        )
        lowered_targets = {f"{n.lower()}.exe" if not n.lower().endswith(".exe") else n.lower() for n in process_names}
        for line in out.splitlines():
            if not line.startswith('"'):
                continue
            proc_name = line.split('"', 2)[1].strip().lower()
            if proc_name in lowered_targets:
                return True
    except Exception:
        return False
    return False


def verify_codex_worktree(cfg: dict[str, Any], repo_root: str) -> str:
    """Reuses codex_wake fail-closed branch/non-main guard to resolve safe interim worktree."""
    expected_branch = str(cfg.get("interim_branch") or codex_wake_helper.DEFAULT_INTERIM_BRANCH)
    configured_wt = str(cfg.get("interim_worktree") or "").strip()
    main_checkout = os.path.abspath(str(cfg.get("main_checkout") or REPO_ROOT))

    candidate = ""
    if configured_wt and os.path.isdir(configured_wt):
        candidate = configured_wt
    else:
        candidate = codex_wake_helper.find_interim_worktree(repo_root, expected_branch=expected_branch)

    if not candidate or not os.path.isdir(candidate):
        raise RuntimeError(f"Interim worktree missing for branch '{expected_branch}'")

    resolved = os.path.abspath(candidate)
    if resolved.lower() == main_checkout.lower():
        raise RuntimeError(f"Refusing Codex wake inside main checkout: {resolved}")

    branch = codex_wake_helper._git_branch_name(resolved)
    if branch != expected_branch:
        raise RuntimeError(f"Interim worktree branch '{branch}' != expected '{expected_branch}'")
    return resolved


def resolve_recipients(msg: dict[str, Any]) -> list[str]:
    to_field = str(msg.get("to") or "").strip().lower()
    sender = str(msg.get("from") or "").strip().lower()
    if to_field == "all":
        return [a for a in AGENTS if a != sender]
    if to_field in AGENTS and to_field != sender:
        return [to_field]
    return []


def _append_jsonl(file_path: str, obj: dict[str, Any]) -> None:
    os.makedirs(os.path.dirname(file_path), exist_ok=True)
    with open(file_path, "a", encoding="utf-8") as fh:
        fh.write(json.dumps(obj, ensure_ascii=False) + "\n")


def _append_designer_inbox(file_path: str, msg: dict[str, Any], category: str) -> None:
    os.makedirs(os.path.dirname(file_path), exist_ok=True)
    header = f"## #{msg.get('id', 0)} [{category}] от {msg.get('from')} ({msg.get('ts', '')}) topic={msg.get('topic', '')}"
    block = f"{header}\n\n{format_forwarded_message(msg)}\n\n---\n\n"
    with open(file_path, "a", encoding="utf-8") as fh:
        fh.write(block)


def _run_codex_wake(
    interim_wt: str,
    forwarded_text: str,
    runner: Callable[[list[str]], tuple[int, str]] | None = None,
) -> tuple[int, str]:
    prompt_payload = f"{forwarded_text}\n\n{codex_wake_helper.HARD_SAFETY_RULES_BLOCK}"

    def _exec(cmd: list[str]) -> tuple[int, str]:
        if runner is not None:
            return runner(cmd)
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8")
        return proc.returncode, redact_secrets(proc.stdout or "")

    resume_cmd = ["codex", "exec", "resume", "--last", "--cd", interim_wt, prompt_payload]
    code, out = _exec(resume_cmd)
    if code == 0:
        return code, out
    fallback_cmd = ["codex", "exec", "--cd", interim_wt, prompt_payload]
    return _exec(fallback_cmd)


def _run_gemini_wake(
    gemini_wt: str,
    prompt_dir: str,
    msg: dict[str, Any],
    category: str,
    forwarded_text: str,
    dry_run: bool,
    runner: Callable[[list[str]], tuple[int, str]] | None = None,
) -> tuple[int, str]:
    mode = "plan" if category == "question" else "yolo"
    msg_id = msg.get("id", 0)
    script = os.path.join(TOOLS_DIR, "gemini_agent.ps1")
    if dry_run and runner is None:
        return 0, f"[dry-run] would invoke gemini_agent.ps1 -Mode {mode} in {gemini_wt}"

    os.makedirs(prompt_dir, exist_ok=True)
    prompt_file = os.path.join(prompt_dir, f"msg_{msg_id}_gemini.txt")
    with open(prompt_file, "w", encoding="utf-8") as fh:
        fh.write(forwarded_text)

    cmd = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        script,
        "-Worktree",
        gemini_wt,
        "-PromptFile",
        prompt_file,
        "-Mode",
        mode,
        "-Label",
        f"chat-{msg_id}",
    ]
    if runner is not None:
        return runner(cmd)
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8")
    return proc.returncode, redact_secrets(proc.stdout or "")


def run_once(
    cfg: dict[str, Any],
    chat_file: str,
    state_dir: str,
    dry_run: bool = False,
    now_dt: dt.datetime | None = None,
    classifier_fn: Callable[[dict[str, Any]], str | None] | None = None,
    is_process_running_fn: Callable[[list[str]], bool] | None = None,
    subprocess_runner: Callable[[list[str]], tuple[int, str]] | None = None,
    repo_root: str = REPO_ROOT,
) -> dict[str, Any]:
    """Processes unread messages and pending Gemini idle wakes in a single dispatcher tick."""
    ref_dt = now_dt or dt.datetime.now().astimezone()
    now_ts = ref_dt.timestamp()

    stop_file = resolve_repo_path(str(cfg.get("stop_file") or "Saved/Orchestrator/dispatcher/STOP"), repo_root)
    if os.path.exists(stop_file):
        return {"killed_by_stop_file": True, "stop_file": stop_file, "actions": []}

    state_file = os.path.join(state_dir, "state.json")
    state = load_state(state_file)
    messages = agent_chat.read_all_messages(chat_file)

    wake_claude_file = resolve_repo_path(
        str(cfg.get("wake_claude_file") or os.path.join(state_dir, "wake_claude.jsonl")), repo_root
    )
    designer_inbox_file = resolve_repo_path(
        str(cfg.get("designer_inbox_file") or os.path.join(state_dir, "designer_inbox.md")), repo_root
    )
    gemini_spend_file = resolve_repo_path(
        str(cfg.get("gemini_spend_file") or os.path.join(state_dir, "gemini_spend.jsonl")), repo_root
    )
    prompts_dir = os.path.join(state_dir, "prompts")
    log_file = os.path.join(state_dir, "dispatcher.jsonl")

    quiet_now = is_in_quiet_hours(ref_dt, cfg)
    rate_limit_sec = float(cfg.get("rate_limit_min", 10)) * 60.0
    gemini_idle_sec = float(cfg.get("gemini_idle_min", 15)) * 60.0
    gemini_budget = float(cfg.get("gemini_weekly_budget_usd", 20.0))
    gemini_est_usd = float(cfg.get("estimated_gemini_wake_usd", 0.25))
    proc_check = is_process_running_fn or default_is_process_running

    # Track newest gemini post id/timestamp across entire chat to detect Antigravity responsiveness
    latest_gemini_ts = 0.0
    latest_gemini_id = 0
    for m in messages:
        if str(m.get("from") or "").strip().lower() == "gemini":
            mid = int(m.get("id") or 0)
            if mid > latest_gemini_id:
                latest_gemini_id = mid
            t = codex_wake_helper.parse_iso_timestamp(m.get("ts"))
            if t is not None and t > latest_gemini_ts:
                latest_gemini_ts = t

    actions: list[dict[str, Any]] = []
    last_id = int(state.get("last_processed_id", 0))
    new_messages = [m for m in messages if int(m.get("id") or 0) > last_id]

    def _rate_limited(recipient: str) -> bool:
        prev = float(state["last_wake_ts"].get(recipient, 0.0))
        return prev > 0.0 and (now_ts - prev) < rate_limit_sec

    def _record_event(evt: dict[str, Any]) -> None:
        actions.append(evt)
        if not dry_run:
            _append_jsonl(log_file, {"ts": ref_dt.isoformat(timespec="seconds"), **evt})

    for msg in new_messages:
        msg_id = int(msg.get("id") or 0)
        state["last_processed_id"] = max(int(state["last_processed_id"]), msg_id)
        category, cls_src = classify_message(msg, cfg, classifier_fn=classifier_fn)
        recipients = resolve_recipients(msg)

        if category not in ACTIONABLE_CATEGORIES or not recipients:
            _record_event(
                {
                    "msg_id": msg_id,
                    "category": category,
                    "classifier": cls_src,
                    "decision": "skip_status_or_no_recipient",
                }
            )
            continue

        msg_ts = codex_wake_helper.parse_iso_timestamp(msg.get("ts")) or now_ts
        forwarded = format_forwarded_message(msg)

        for recipient in recipients:
            if quiet_now and category != "blocker":
                _record_event(
                    {
                        "msg_id": msg_id,
                        "recipient": recipient,
                        "category": category,
                        "decision": "skip_quiet_hours",
                    }
                )
                continue

            if _rate_limited(recipient):
                _record_event(
                    {
                        "msg_id": msg_id,
                        "recipient": recipient,
                        "category": category,
                        "decision": "skip_rate_limit",
                    }
                )
                continue

            if recipient == "claude":
                if category not in CLAUDE_WAKE_CATEGORIES:
                    _record_event(
                        {
                            "msg_id": msg_id,
                            "recipient": "claude",
                            "category": category,
                            "decision": "skip_claude_task_only",
                        }
                    )
                    continue
                if not dry_run:
                    _append_jsonl(
                        wake_claude_file,
                        {
                            "when": ref_dt.isoformat(timespec="seconds"),
                            "msg_id": msg_id,
                            "from": msg.get("from"),
                            "topic": msg.get("topic"),
                            "category": category,
                            "forwarded": forwarded,
                        },
                    )
                    state["last_wake_ts"]["claude"] = now_ts
                _record_event(
                    {
                        "msg_id": msg_id,
                        "recipient": "claude",
                        "category": category,
                        "decision": "wake_claude",
                        "forwarded": forwarded,
                        "dry_run": dry_run,
                    }
                )

            elif recipient == "designer":
                if not dry_run:
                    _append_designer_inbox(designer_inbox_file, msg, category)
                    state["last_wake_ts"]["designer"] = now_ts
                _record_event(
                    {
                        "msg_id": msg_id,
                        "recipient": "designer",
                        "category": category,
                        "decision": "deliver_designer_inbox",
                        "forwarded": forwarded,
                        "dry_run": dry_run,
                    }
                )

            elif recipient == "codex":
                proc_names = list(cfg.get("codex_process_names") or ["codex", "Codex"])
                if proc_check(proc_names):
                    _record_event(
                        {
                            "msg_id": msg_id,
                            "recipient": "codex",
                            "category": category,
                            "decision": "skip_codex_already_running",
                        }
                    )
                    continue
                try:
                    interim_wt = verify_codex_worktree(cfg, repo_root)
                except Exception as exc:
                    _record_event(
                        {
                            "msg_id": msg_id,
                            "recipient": "codex",
                            "category": category,
                            "decision": "skip_codex_guard_failed",
                            "error": str(exc),
                        }
                    )
                    continue
                if dry_run:
                    _record_event(
                        {
                            "msg_id": msg_id,
                            "recipient": "codex",
                            "category": category,
                            "decision": "wake_codex",
                            "worktree": interim_wt,
                            "forwarded": forwarded,
                            "dry_run": True,
                        }
                    )
                else:
                    code, out = _run_codex_wake(interim_wt, forwarded, runner=subprocess_runner)
                    state["last_wake_ts"]["codex"] = now_ts
                    state["codex_runs"] = int(state.get("codex_runs", 0)) + 1
                    _record_event(
                        {
                            "msg_id": msg_id,
                            "recipient": "codex",
                            "category": category,
                            "decision": "wake_codex",
                            "exit_code": code,
                            "worktree": interim_wt,
                            "forwarded": forwarded,
                        }
                    )

            elif recipient == "gemini":
                # Enqueue so we check the 15-min Antigravity idle window cleanly
                state["pending_gemini"].append(
                    {
                        "msg": msg,
                        "category": category,
                        "msg_ts": msg_ts,
                    }
                )
                _record_event(
                    {
                        "msg_id": msg_id,
                        "recipient": "gemini",
                        "category": category,
                        "decision": "queued_gemini_idle_wait",
                    }
                )

    # Evaluate pending_gemini wakes
    remaining_gemini = []
    for item in list(state.get("pending_gemini") or []):
        msg = item["msg"]
        msg_id = int(msg.get("id") or 0)
        category = str(item["category"])
        msg_ts = float(item["msg_ts"])

        # Antigravity already replied after this message -> drop
        if latest_gemini_id > msg_id or latest_gemini_ts > msg_ts:
            _record_event(
                {
                    "msg_id": msg_id,
                    "recipient": "gemini",
                    "category": category,
                    "decision": "cancel_gemini_already_replied",
                }
            )
            continue

        if (now_ts - msg_ts) < gemini_idle_sec:
            remaining_gemini.append(item)
            continue

        if quiet_now and category != "blocker":
            remaining_gemini.append(item)
            _record_event(
                {
                    "msg_id": msg_id,
                    "recipient": "gemini",
                    "category": category,
                    "decision": "skip_quiet_hours",
                }
            )
            continue

        if _rate_limited("gemini"):
            remaining_gemini.append(item)
            _record_event(
                {
                    "msg_id": msg_id,
                    "recipient": "gemini",
                    "category": category,
                    "decision": "skip_rate_limit",
                }
            )
            continue

        spent_week = load_weekly_spend_usd(gemini_spend_file, now_ts)
        if spent_week + gemini_est_usd > gemini_budget:
            _record_event(
                {
                    "msg_id": msg_id,
                    "recipient": "gemini",
                    "category": category,
                    "decision": "skip_gemini_budget_cap",
                    "spent_week_usd": round(spent_week, 4),
                    "budget_usd": gemini_budget,
                }
            )
            continue

        gemini_wt = os.path.abspath(str(cfg.get("gemini_worktree") or repo_root))
        main_checkout = os.path.abspath(str(cfg.get("main_checkout") or REPO_ROOT))
        if gemini_wt.lower() == main_checkout.lower():
            _record_event(
                {
                    "msg_id": msg_id,
                    "recipient": "gemini",
                    "category": category,
                    "decision": "skip_gemini_main_checkout_guard",
                }
            )
            continue

        forwarded = format_forwarded_message(msg)
        mode = "plan" if category == "question" else "yolo"
        if dry_run:
            _record_event(
                {
                    "msg_id": msg_id,
                    "recipient": "gemini",
                    "category": category,
                    "mode": mode,
                    "decision": "wake_gemini",
                    "forwarded": forwarded,
                    "dry_run": True,
                }
            )
        else:
            code, out = _run_gemini_wake(
                gemini_wt,
                prompts_dir,
                msg,
                category,
                forwarded,
                dry_run=False,
                runner=subprocess_runner,
            )
            state["last_wake_ts"]["gemini"] = now_ts
            state["gemini_runs"] = int(state.get("gemini_runs", 0)) + 1
            _append_jsonl(
                gemini_spend_file,
                {
                    "when": ref_dt.isoformat(timespec="seconds"),
                    "ts": now_ts,
                    "msg_id": msg_id,
                    "mode": mode,
                    "usd": gemini_est_usd,
                },
            )
            _record_event(
                {
                    "msg_id": msg_id,
                    "recipient": "gemini",
                    "category": category,
                    "mode": mode,
                    "exit_code": code,
                    "decision": "wake_gemini",
                    "forwarded": forwarded,
                }
            )

    state["pending_gemini"] = remaining_gemini
    if not dry_run:
        save_state_atomic(state_file, state)

    return {
        "killed_by_stop_file": False,
        "dry_run": dry_run,
        "actions": actions,
    }


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="CodexTactics Chat Dispatcher")
    ap.add_argument("--config", default=DEFAULT_CONFIG_PATH)
    ap.add_argument("--chat-file", default=None)
    ap.add_argument("--state-dir", default=None)
    ap.add_argument("--dry-run", action="store_true", help="Log intended actions without side effects")
    ap.add_argument("--once", action="store_true", help="Run a single tick and exit")
    ap.add_argument("--from-start", action="store_true", help="Process full chat history on first run")
    ap.add_argument("--tail", type=int, default=5, help="In --dry-run without state.json, preview only last N messages")
    args = ap.parse_args(argv)

    cfg = load_config(args.config)
    storage_dir = agent_chat.get_storage_dir()
    chat_file = args.chat_file or os.path.join(storage_dir, "chat.jsonl")
    state_dir = args.state_dir or resolve_repo_path(
        str(cfg.get("state_dir") or "Saved/Orchestrator/dispatcher"), REPO_ROOT
    )
    poll_sec = max(2, int(cfg.get("poll_interval_sec", 10)))

    state_file = os.path.join(state_dir, "state.json")
    if not args.from_start and not os.path.exists(state_file):
        existing = agent_chat.read_all_messages(chat_file)
        if existing:
            max_id = max(int(m.get("id") or 0) for m in existing)
            if args.dry_run:
                # Preview only the last --tail messages without writing state.json
                preview_min_id = max(0, max_id - max(0, args.tail))
                trimmed = [m for m in existing if int(m.get("id") or 0) > preview_min_id]
                fd, tmp_chat = tempfile.mkstemp(prefix="dispatcher_preview_", suffix=".jsonl")
                try:
                    with os.fdopen(fd, "w", encoding="utf-8") as fh:
                        for m in trimmed:
                            fh.write(json.dumps(m, ensure_ascii=False) + "\n")
                    res = run_once(cfg, chat_file=tmp_chat, state_dir=state_dir, dry_run=True)
                    print(redact_secrets(json.dumps(res, ensure_ascii=False)), flush=True)
                finally:
                    if os.path.exists(tmp_chat):
                        os.unlink(tmp_chat)
                return 0
            state = _default_state()
            state["last_processed_id"] = max_id
            save_state_atomic(state_file, state)

    while True:
        res = run_once(cfg, chat_file=chat_file, state_dir=state_dir, dry_run=args.dry_run)
        if args.dry_run or res.get("actions") or res.get("killed_by_stop_file"):
            print(redact_secrets(json.dumps(res, ensure_ascii=False)), flush=True)
        if args.once or res.get("killed_by_stop_file"):
            break
        time.sleep(poll_sec)
    return 0


if __name__ == "__main__":
    sys.exit(main())
