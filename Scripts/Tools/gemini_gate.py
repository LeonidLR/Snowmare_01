#!/usr/bin/env python3
"""Pre-'done' quality gate helper for Gemini tasks (task 036).

Implements checks 1, 2, 5, 6, 7 and Markdown gate reporting:
  1. Location check (worktree + gemini/<NNN>-* branch, never main checkout)
  2. Allowed paths check (parses docs/port/gemini/inbox/<NNN>-*.md + always-forbidden paths)
  5. Citation check (verifies path:line and repo paths in report exist & line <= EOF)
  6. Evidence check (Claim | Evidence / Утверждение | Доказательство table with no empty cells)
  7. Optional Sonnet review on C++ diffs via Scripts/Tools/api_task.py (FAIL on HIGH only)
"""
from __future__ import annotations

import argparse
import datetime as dt
import fnmatch
import glob
import json
import os
import re
import stat
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from typing import Iterable, List, Optional, Sequence, Tuple


for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


FORBIDDEN_PREFIXES = (
    "content/maps/",
    "config/",
    "content/vfx/",
)
FORBIDDEN_EXACT = {
    "content/maps",
    "config",
    "content/vfx",
    "content/data/leveljson/l_gen_depot_1.json",
    "claude.md",
}
FORBIDDEN_SUFFIXES = (".uasset", ".umap")
PLACEHOLDER_EVIDENCE = {"", "-", "--", "---", "n/a", "na", "none", "tbd", "todo", "?", "...", "pending"}
KNOWN_REPO_ROOT_PREFIXES = ("source/", "scripts/", "content/", "docs/", "tools/", "config/")
KNOWN_ROOT_FILES = {"gemini.md", "claude.md", "codextactics.uproject"}
CPP_EXTENSIONS = (".h", ".hpp", ".hh", ".c", ".cpp", ".cc", ".cxx", ".inl")


@dataclass
class CheckOutcome:
    name: str
    passed: Optional[bool]  # True = PASS, False = FAIL, None = SKIP
    summary: str
    details: List[str] = field(default_factory=list)

    @property
    def status_label(self) -> str:
        if self.passed is True:
            return "PASS"
        if self.passed is False:
            return "FAIL"
        return "SKIP"


def normalize_task_id(task: str) -> str:
    digits = re.sub(r"[^0-9]", "", task.strip())
    if not digits:
        return task.strip()
    return digits.zfill(3) if len(digits) < 3 else digits


def norm_rel(path: str) -> str:
    return path.replace("\\", "/").lstrip("./")


def _run_git(repo: str, args: Sequence[str]) -> Tuple[int, str, str]:
    proc = subprocess.run(
        ["git", "-C", repo, *args],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return proc.returncode, proc.stdout.strip(), proc.stderr.strip()


def find_task_file(repo: str, task_id: str) -> Optional[str]:
    norm = normalize_task_id(task_id)
    pattern = os.path.join(repo, "docs", "port", "gemini", "inbox", f"{norm}-*.md")
    matches = sorted(glob.glob(pattern))
    if not matches:
        pattern_alt = os.path.join(repo, "docs", "port", "gemini", "inbox", f"{norm}*.md")
        matches = sorted(glob.glob(pattern_alt))
    return matches[0] if matches else None


def find_report_files(repo: str, task_id: str) -> List[str]:
    norm = normalize_task_id(task_id)
    outbox = os.path.join(repo, "docs", "port", "gemini", "outbox")
    matches = sorted(glob.glob(os.path.join(outbox, f"{norm}-*.report.md")))
    if not matches:
        matches = sorted(glob.glob(os.path.join(outbox, f"{norm}*.report.md")))
    return matches


def detect_base_branch(repo: str, task_text: str, explicit_base: Optional[str] = None) -> str:
    if explicit_base:
        return explicit_base
    if task_text:
        m = re.search(
            r"(?im)(?:^\s*Base(?:\s+branch)?\s*:|from)\s*`?([A-Za-z0-9._/\-]+)`?",
            task_text,
        )
        if m:
            candidate = m.group(1).strip()
            for ref in (candidate, f"origin/{candidate}"):
                rc, _, _ = _run_git(repo, ["rev-parse", "--verify", "--quiet", ref])
                if rc == 0:
                    return ref
    for ref in ("origin/main", "main"):
        rc, _, _ = _run_git(repo, ["rev-parse", "--verify", "--quiet", ref])
        if rc == 0:
            return ref
    return "HEAD"


def check_location(repo: str, task_id: str, branch_override: Optional[str] = None,
                   is_worktree_override: Optional[bool] = None) -> CheckOutcome:
    norm = normalize_task_id(task_id)
    abs_repo = os.path.abspath(repo)
    base_name = os.path.basename(abs_repo).lower()
    git_entry = os.path.join(abs_repo, ".git")

    if is_worktree_override is not None:
        is_worktree = is_worktree_override
    else:
        # In a git worktree `.git` is a file (`gitdir: .../.git/worktrees/...`); in the main checkout `.git` is a dir.
        is_worktree = os.path.isfile(git_entry) and base_name != "codextactics"

    if branch_override is not None:
        branch = branch_override
    else:
        rc, branch, err = _run_git(abs_repo, ["rev-parse", "--abbrev-ref", "HEAD"])
        if rc != 0:
            return CheckOutcome("Location", False, f"git rev-parse failed: {err or 'unknown'}", [abs_repo])

    expected_re = re.compile(rf"^gemini/{re.escape(norm)}(-|$)", re.IGNORECASE)
    problems = []
    if not is_worktree:
        problems.append(f"Main checkout detected ({abs_repo}); must run inside an isolated worktree")
    if branch in ("main", "master", "HEAD") or branch.startswith("interim/"):
        problems.append(f"Branch '{branch}' is protected; expected gemini/{norm}-*")
    elif not expected_re.match(branch):
        problems.append(f"Branch '{branch}' does not match expected 'gemini/{norm}-*'")

    if problems:
        return CheckOutcome("Location", False, "; ".join(problems), [f"Worktree: {abs_repo}", f"Branch: {branch}"])
    return CheckOutcome("Location", True, f"Worktree `{os.path.basename(abs_repo)}`, branch `{branch}`", [f"Repo: {abs_repo}"])


def _is_reparse_point(path: str) -> bool:
    try:
        st = os.lstat(path)
    except OSError:
        return False
    if stat.S_ISLNK(st.st_mode):
        return True
    attrs = getattr(st, "st_file_attributes", 0)
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    return bool(attrs & reparse_flag)


def is_forbidden_path(rel_path: str, repo: Optional[str] = None) -> Optional[str]:
    clean = norm_rel(rel_path)
    low = clean.lower()
    if low in FORBIDDEN_EXACT:
        return f"forbidden path '{clean}'"
    for pref in FORBIDDEN_PREFIXES:
        if low.startswith(pref):
            return f"forbidden directory '{pref.rstrip('/')}' ({clean})"
    if low.endswith(FORBIDDEN_SUFFIXES):
        return f"forbidden binary asset '{clean}'"
    if low.startswith("content/"):
        parts = clean.split("/")
        top = parts[1].lower() if len(parts) > 1 else ""
        if repo and len(parts) > 1:
            candidate = repo
            for seg in parts[:2]:
                candidate = os.path.join(candidate, seg)
                if _is_reparse_point(candidate):
                    return f"forbidden Content pack junction '{clean}'"
        # Any Content/<Subdir> outside Content/Data (maps/vfx already caught above)
        if top and top != "data":
            return f"forbidden Content pack / asset dir '{parts[1]}' ({clean})"
    return None


def _split_outside_braces(text: str) -> List[str]:
    tokens: List[str] = []
    buf: List[str] = []
    depth = 0
    for ch in text:
        if ch == "{":
            depth += 1
            buf.append(ch)
        elif ch == "}":
            depth = max(0, depth - 1)
            buf.append(ch)
        elif ch in (",", ";", "\n") and depth == 0:
            part = "".join(buf).strip()
            if part:
                tokens.append(part)
            buf.clear()
        else:
            buf.append(ch)
    tail = "".join(buf).strip()
    if tail:
        tokens.append(tail)
    return tokens


def _expand_braces(pattern: str) -> List[str]:
    m = re.search(r"\{([^{}]+)\}", pattern)
    if not m:
        return [pattern]
    prefix = pattern[:m.start()]
    suffix = pattern[m.end():]
    out: List[str] = []
    for option in m.group(1).split(","):
        out.extend(_expand_braces(prefix + option.strip() + suffix))
    return out


def parse_allowed_paths(task_text: str) -> Tuple[bool, List[str]]:
    """Extract Allowed paths patterns from a Gemini task markdown file."""
    lines = task_text.splitlines()
    collected: List[str] = []
    has_section = False

    # Format 1: ## Allowed paths ... until next ## heading
    in_section = False
    for line in lines:
        if re.match(r"^\s*##\s+Allowed\s+paths\b", line, re.IGNORECASE):
            has_section = True
            in_section = True
            continue
        if in_section:
            if re.match(r"^\s*##\s+", line):
                break
            collected.append(line)

    # Format 2: inline "Allowed paths: ..." paragraph
    if not has_section:
        in_inline = False
        for line in lines:
            m = re.match(r"^\s*Allowed\s+paths\s*:\s*(.*)$", line, re.IGNORECASE)
            if m:
                has_section = True
                in_inline = True
                if m.group(1).strip():
                    collected.append(m.group(1))
                continue
            if in_inline:
                if not line.strip() or re.match(r"^\s*(?:##|Checks|Spec|Report|Steps|Goal|Base)\b", line, re.IGNORECASE):
                    break
                collected.append(line)

    if not has_section:
        return False, []

    raw_block = "\n".join(collected)
    # Strip markdown list markers and inline parenthetical comments like "(stealth only)"
    cleaned_lines: List[str] = []
    for raw_line in raw_block.splitlines():
        line = raw_line.strip()
        line = re.sub(r"^[-*]\s+", "", line)
        line = re.sub(r"\s*\([^)]*\)", "", line)
        line = re.sub(r"\s+—\s+.*$", "", line)
        line = re.sub(r"\s+-\s+.*$", "", line)
        cleaned_lines.append(line)
    raw_clean = "\n".join(cleaned_lines)

    patterns: List[str] = []
    for chunk in _split_outside_braces(raw_clean):
        # Split on " + " outside braces if multiple paths are joined with +
        subchunks = re.split(r"\s+\+\s+", chunk)
        for sub in subchunks:
            sub = sub.strip().strip("`'\". ")
            if not sub:
                continue
            low = sub.lower()
            if low.startswith(("nothing else", "no .uasset", "anything else", "<exact files")):
                continue
            # Handle shorthand "Dir/Prefix + Suffix" e.g. "OperativeCharacterMovement / Stance"
            if " / " in sub:
                for piece in sub.split(" / "):
                    piece = piece.strip().strip("`'\". ")
                    if piece:
                        patterns.extend(_expand_braces(norm_rel(piece)))
            else:
                patterns.extend(_expand_braces(norm_rel(sub)))

    # Deduplicate preserving order
    seen = set()
    unique: List[str] = []
    for p in patterns:
        if p and p not in seen:
            seen.add(p)
            unique.append(p)
    return True, unique


def _pattern_matches(pattern: str, rel_path: str) -> bool:
    p = norm_rel(pattern).lower()
    f = norm_rel(rel_path).lower()
    if not p:
        return False

    # Bare stem/phrase with spaces without wildcard or slash (e.g. "the stealth smokes" or "ActionBarWidget")
    if "/" not in p and "*" not in p and "?" not in p and "." not in p:
        if " " in p:
            words = [w for w in p.split() if len(w) > 3 and w not in ("the", "and", "with", "only", "files", "tests", "smokes")]
            return any(w in f for w in words) if words else False
        return p in f

    is_root_anchored = p.startswith(KNOWN_REPO_ROOT_PREFIXES) or p in KNOWN_ROOT_FILES
    has_wild = ("*" in p or "?" in p)
    if not has_wild:
        if f == p or f.startswith(p.rstrip("/") + "/"):
            return True
        if not is_root_anchored and (f.endswith("/" + p.rstrip("/")) or f"/{p.rstrip('/')}/" in f):
            return True
        if "/" not in p and os.path.basename(f) == p:
            return True
        return False

    # Build regex supporting ** across path segments and * within or across when Pattern uses /**/
    # Expand common "**/" optional zero-dir case as well as arbitrary segments
    variants = {p}
    if "**/" in p:
        variants.add(p.replace("**/", ""))
    anchor_start = "^" if is_root_anchored else r"(?:^|.*/)"
    for var in variants:
        regex_parts: List[str] = [anchor_start]
        i = 0
        n = len(var)
        while i < n:
            if var[i:i + 2] == "**":
                regex_parts.append(".*")
                i += 2
            elif var[i] == "*":
                regex_parts.append("[^/]*")
                i += 1
            elif var[i] == "?":
                regex_parts.append("[^/]")
                i += 1
            else:
                regex_parts.append(re.escape(var[i]))
                i += 1
        regex_parts.append("$")
        if re.match("".join(regex_parts), f):
            return True
        # Also allow bare wildcard patterns without '/' (e.g. "Operative*Stealth*") to match basename or full path
        if "/" not in var and fnmatch.fnmatch(os.path.basename(f), var):
            return True
    return False


def check_allowed_paths(
    changed_files: Sequence[str],
    task_text: str,
    task_id: str,
    repo: Optional[str] = None,
    include_task_outbox: bool = True,
) -> CheckOutcome:
    norm = normalize_task_id(task_id)
    has_section, patterns = parse_allowed_paths(task_text)
    if include_task_outbox:
        patterns.append(f"docs/port/gemini/outbox/{norm}*")
        patterns.append(f"saved/orchestrator/gate/{norm}*")

    forbidden_hits: List[str] = []
    outside_hits: List[str] = []

    for rel in changed_files:
        clean = norm_rel(rel)
        forb = is_forbidden_path(clean, repo=repo)
        if forb:
            forbidden_hits.append(forb)
            continue
        if has_section:
            if not any(_pattern_matches(pat, clean) for pat in patterns):
                outside_hits.append(clean)
        else:
            # Older task files without an Allowed paths header: still forbid C++ changes without explicit allowlist
            low = clean.lower()
            is_outbox = low.startswith(f"docs/port/gemini/outbox/{norm}")
            is_safe_tool = low.startswith(("scripts/tools/", "docs/port/gemini/"))
            if not (is_outbox or is_safe_tool):
                outside_hits.append(f"{clean} (no Allowed paths section in task file)")

    if forbidden_hits or outside_hits:
        details = [f"FORBIDDEN: {x}" for x in forbidden_hits] + [f"OUTSIDE ALLOWED: {x}" for x in outside_hits]
        summary_parts = []
        if forbidden_hits:
            summary_parts.append(f"{len(forbidden_hits)} forbidden")
        if outside_hits:
            summary_parts.append(f"{len(outside_hits)} outside allowed paths")
        return CheckOutcome("Allowed paths", False, ", ".join(summary_parts), details)

    note = f"{len(changed_files)} file(s) checked against {len(patterns)} pattern(s)" if has_section else f"{len(changed_files)} file(s) checked (no Allowed paths header; non-Source tooling/outbox scope)"
    return CheckOutcome("Allowed paths", True, note, [])


def _is_pycache_rel(rel_path: str) -> bool:
    clean = norm_rel(rel_path).rstrip("/")
    parts = [seg.lower() for seg in clean.split("/") if seg]
    if "__pycache__" in parts:
        return True
    return clean.lower().endswith((".pyc", ".pyo"))


def get_changed_files(repo: str, base_branch: str) -> List[str]:
    files = set()
    rc, committed, _ = _run_git(repo, ["diff", "--name-only", f"{base_branch}...HEAD"])
    if rc == 0:
        for line in committed.splitlines():
            if line.strip():
                files.add(norm_rel(line.strip()))
    for extra in (["diff", "--name-only"], ["diff", "--cached", "--name-only"]):
        rc, out, _ = _run_git(repo, extra)
        if rc == 0:
            for line in out.splitlines():
                if line.strip():
                    files.add(norm_rel(line.strip()))
    rc, status_out, _ = _run_git(repo, ["status", "--porcelain", "--untracked-files=normal"])
    if rc == 0:
        for line in status_out.splitlines():
            if line.startswith("?? "):
                p = norm_rel(line[3:].strip().strip('"'))
                if _is_pycache_rel(p):
                    continue
                full_p = os.path.join(repo, p.rstrip("/")) if repo else ""
                # Skip untracked Content pack junctions when not modified by us
                if p.startswith("Content/") and full_p and _is_reparse_point(full_p):
                    continue
                if p.endswith("/") and full_p and os.path.isdir(full_p) and not _is_reparse_point(full_p):
                    for root, dirnames, filenames in os.walk(full_p, followlinks=False):
                        dirnames[:] = [
                            d for d in dirnames
                            if d.lower() != "__pycache__" and not _is_reparse_point(os.path.join(root, d))
                        ]
                        for fname in filenames:
                            rel_child = norm_rel(os.path.relpath(os.path.join(root, fname), repo))
                            if not _is_pycache_rel(rel_child):
                                files.add(rel_child)
                    continue
                files.add(p)
    return sorted(files)


_PATH_LINE_RE = re.compile(
    r"(?<![A-Za-z0-9_:/\\])"
    r"((?:Source|Scripts|Content|docs|Tools|Config)/[A-Za-z0-9_.\-/\\]+|[A-Za-z0-9_.\-/\\]+\.(?:cpp|h|hpp|cs|py|ps1|ts|tsx|js|jsx|json|md|ini|uproject))"
    r":(\d+)(?:-(\d+))?(?![0-9])",
    re.IGNORECASE,
)
_REPO_PATH_RE = re.compile(
    r"(?<![A-Za-z0-9_:/\\])"
    r"((?:Source|Scripts|Content|docs|Tools|Config)/[A-Za-z0-9_.\-/]+|GEMINI\.md|CLAUDE\.md)"
)


def _strip_code_blocks_if_pasted_log(lines: Sequence[str]) -> List[Tuple[int, str]]:
    """Return non-log lines with original 1-based line numbers? Keep all lines except compiler diagnostic noise in fenced logs?
    Wait: citations inside pasted logs might be noisy compiler paths; however real citations exist throughout reports.
    We filter out Windows absolute paths (C:/...) and template globs (<NNN>, *, **, {a,b}).
    """
    return [(idx + 1, line) for idx, line in enumerate(lines)]


def _looks_like_template_or_glob(path: str) -> bool:
    return any(ch in path for ch in ("*", "?", "<", ">", "{", "}"))


def check_citations(repo: str, report_paths: Sequence[str]) -> CheckOutcome:
    if not report_paths:
        return CheckOutcome("Citation check", False, "No report file found in docs/port/gemini/outbox/", [])

    broken: List[str] = []
    checked_count = 0
    line_count_cache: dict[str, int] = {}

    def get_line_count(rel_path: str) -> Optional[int]:
        clean = norm_rel(rel_path)
        if clean in line_count_cache:
            return line_count_cache[clean]
        abs_path = os.path.join(repo, clean)
        if not os.path.isfile(abs_path):
            return None
        try:
            with open(abs_path, encoding="utf-8", errors="replace") as f:
                cnt = sum(1 for _ in f)
        except OSError:
            return None
        line_count_cache[clean] = cnt
        return cnt

    for rep in report_paths:
        if not os.path.isfile(rep):
            broken.append(f"Report missing: {rep}")
            continue
        with open(rep, encoding="utf-8", errors="replace") as f:
            rep_lines = f.read().splitlines()
        rep_rel = norm_rel(os.path.relpath(rep, repo)) if os.path.abspath(rep).startswith(os.path.abspath(repo)) else rep

        for line_no, raw in _strip_code_blocks_if_pasted_log(rep_lines):
            seen_spans: List[Tuple[int, int]] = []
            for m in _PATH_LINE_RE.finditer(raw):
                path_tok = norm_rel(m.group(1)).rstrip(".,;)]'\"")
                if _looks_like_template_or_glob(path_tok):
                    continue
                # Ignore Windows drive prefixes if regex overlapped after C:
                prefix_start = m.start()
                if prefix_start >= 2 and raw[prefix_start - 1] == ":" and raw[prefix_start - 2].isalpha():
                    continue
                start_ln = int(m.group(2))
                end_ln = int(m.group(3)) if m.group(3) else start_ln
                checked_count += 1
                seen_spans.append((m.start(1), m.end(1)))
                total_lines = get_line_count(path_tok)
                if total_lines is None:
                    broken.append(f"{rep_rel}:{line_no} -> missing file '{path_tok}' (cited :{start_ln})")
                elif start_ln < 1 or start_ln > total_lines or end_ln < start_ln or end_ln > total_lines:
                    broken.append(
                        f"{rep_rel}:{line_no} -> line {start_ln}" + (f"-{end_ln}" if end_ln != start_ln else "")
                        + f" out of range for '{path_tok}' ({total_lines} lines)"
                    )

            for m in _REPO_PATH_RE.finditer(raw):
                # Skip if already validated as path:line
                if any(s <= m.start(1) and m.end(1) <= e for s, e in seen_spans):
                    continue
                path_tok = norm_rel(m.group(1)).rstrip(".,;:)]`'\"")
                if not path_tok or _looks_like_template_or_glob(path_tok):
                    continue
                # Skip Saved/ or truncated log fragments
                prefix_start = m.start()
                if prefix_start >= 2 and raw[prefix_start - 1] == ":" and raw[prefix_start - 2].isalpha():
                    continue
                # Skip brace-template or ellipsis fragments (e.g. docs/port/gemini/outbox/036- or trailing '-')
                if path_tok.endswith(("/", "-")):
                    continue
                after = raw[m.end():m.end() + 1]
                if after in ("*", "{", "<"):
                    continue
                checked_count += 1
                abs_target = os.path.join(repo, path_tok)
                if not os.path.exists(abs_target):
                    broken.append(f"{rep_rel}:{line_no} -> missing path '{path_tok}'")

    # Deduplicate broken messages while keeping order
    dedup = list(dict.fromkeys(broken))
    if dedup:
        return CheckOutcome("Citation check", False, f"{len(dedup)} broken citation(s) out of {checked_count} checked", dedup)
    return CheckOutcome("Citation check", True, f"{checked_count} citation(s) verified", [])


def _split_md_row(line: str) -> Optional[List[str]]:
    s = line.strip()
    if "|" not in s:
        return None
    if s.startswith("|"):
        s = s[1:]
    if s.endswith("|"):
        s = s[:-1]
    return [cell.strip() for cell in s.split("|")]


def _is_separator_row(cells: Sequence[str]) -> bool:
    return len(cells) >= 2 and all(bool(re.match(r"^:?-{2,}:?$", c)) for c in cells)


def _is_evidence_header(cells: Sequence[str]) -> bool:
    if len(cells) < 2:
        return False
    c0 = cells[0].lower()
    c1 = cells[1].lower()
    col0_ok = ("claim" in c0) or ("утвержден" in c0)
    col1_ok = ("evidence" in c1) or ("доказател" in c1)
    return col0_ok and col1_ok


def check_evidence(report_paths: Sequence[str]) -> CheckOutcome:
    if not report_paths:
        return CheckOutcome("Evidence check", False, "No report file found in docs/port/gemini/outbox/", [])

    for rep in report_paths:
        if not os.path.isfile(rep):
            return CheckOutcome("Evidence check", False, f"Report missing: {rep}", [])
        with open(rep, encoding="utf-8", errors="replace") as f:
            lines = f.read().splitlines()

        found_table = False
        rows_checked = 0
        empty_rows: List[str] = []

        i = 0
        while i + 1 < len(lines):
            hdr = _split_md_row(lines[i])
            sep = _split_md_row(lines[i + 1])
            if hdr and sep and _is_separator_row(sep) and _is_evidence_header(hdr):
                found_table = True
                j = i + 2
                while j < len(lines):
                    row = _split_md_row(lines[j])
                    if not row or len(row) < 2:
                        break
                    rows_checked += 1
                    claim = row[0].strip()
                    evidence = row[1].strip()
                    ev_norm = re.sub(r"[`*_]", "", evidence).strip().lower()
                    if not claim or ev_norm in PLACEHOLDER_EVIDENCE:
                        empty_rows.append(f"Row {rows_checked} (line {j + 1}): claim='{claim[:60]}' evidence='{evidence}'")
                    j += 1
                i = j
                continue
            i += 1

        if not found_table:
            return CheckOutcome(
                "Evidence check",
                False,
                f"Missing 'Claim | Evidence' (or 'Утверждение | Доказательство') table in {os.path.basename(rep)}",
                [],
            )
        if rows_checked == 0:
            return CheckOutcome("Evidence check", False, f"Evidence table in {os.path.basename(rep)} has 0 rows", [])
        if empty_rows:
            return CheckOutcome("Evidence check", False, f"{len(empty_rows)} row(s) missing evidence in {os.path.basename(rep)}", empty_rows)

    return CheckOutcome("Evidence check", True, f"Evidence table verified ({len(report_paths)} report file(s))", [])


def read_weekly_budget_cap(repo: str, default_cap: float = 64.61) -> float:
    files = sorted(glob.glob(os.path.join(repo, "docs", "port", "PLAN_WEEK_*.md")))
    if not files:
        return default_cap
    latest = files[-1]
    try:
        with open(latest, encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError:
        return default_cap
    caps = re.findall(r"cap\s+([0-9]+(?:\.[0-9]+)?)\s*\$", text, re.IGNORECASE)
    if caps:
        try:
            return float(caps[-1])
        except ValueError:
            pass
    return default_cap


def has_high_sonnet_findings(review_text: str) -> Tuple[bool, List[str]]:
    """Return True only when Sonnet reports active findings marked HIGH severity."""
    findings: List[str] = []
    for line in review_text.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if re.search(r"\b(no|zero|0)\s+high\b|\bhigh\s*[:=]\s*(none|0|no)\b", stripped, re.IGNORECASE):
            continue
        if re.match(r"^(?:[-*]|\d+\.|#+)?\s*(?:\*\*|\[)?HIGH(?:\*\*|\])?(?:\s*[:\-\]]|\s+)", stripped):
            findings.append(stripped)
        elif re.search(r"(?:Severity|SEVERITY)\s*[:=]\s*(?:\*\*|\[)?HIGH\b", stripped):
            findings.append(stripped)
    return bool(findings), findings


def run_sonnet_review(repo: str, task_id: str, base_branch: str, changed_files: Sequence[str],
                      report_paths: Sequence[str]) -> CheckOutcome:
    norm = normalize_task_id(task_id)
    cpp_files = [f for f in changed_files if f.lower().startswith("source/") and f.lower().endswith(CPP_EXTENSIONS)]
    if not cpp_files:
        return CheckOutcome("Sonnet review", None, "Skipped (no C++ diff under Source/)", [])

    if not os.environ.get("ANTHROPIC_API_KEY"):
        return CheckOutcome("Sonnet review", False, "ANTHROPIC_API_KEY is not set in environment", [])

    rc, diff_committed, _ = _run_git(repo, ["diff", f"{base_branch}...HEAD", "--", *cpp_files])
    rc2, diff_working, _ = _run_git(repo, ["diff", "HEAD", "--", *cpp_files])
    combined_diff = "\n".join(x for x in (diff_committed, diff_working) if x).strip()
    if not combined_diff:
        return CheckOutcome("Sonnet review", None, "Skipped (empty C++ diff)", [])

    budget_cap = read_weekly_budget_cap(repo)
    if report_paths:
        out_dir = os.path.dirname(report_paths[0])
    else:
        out_dir = os.path.join(repo, "docs", "port", "gemini", "outbox")
    os.makedirs(out_dir, exist_ok=True)
    review_out = os.path.join(out_dir, f"{norm}-sonnet-review.md")

    brief = (
        f"You are reviewing Gemini C++ task {norm} for CodexTactics (UE 5.8.2).\n"
        "Check strictly for correctness bugs, non-stealth regressions, UObject/GC lifetime bugs, null dereferences, "
        "boundary violations, or missed requirements.\n"
        "Output findings grouped as [HIGH], [MEDIUM], or [LOW] at the start of each bullet. "
        "If none are high severity, explicitly write 'No HIGH findings.'\n"
    )
    with tempfile.TemporaryDirectory() as tmpdir:
        prompt_file = os.path.join(tmpdir, "prompt.md")
        diff_file = os.path.join(tmpdir, "cpp_diff.patch")
        with open(prompt_file, "w", encoding="utf-8") as f:
            f.write(brief)
        with open(diff_file, "w", encoding="utf-8") as f:
            f.write(combined_diff[:120000])  # keep bounded

        cmd = [
            sys.executable,
            os.path.join(repo, "Scripts", "Tools", "api_task.py"),
            "--model", "claude-sonnet-5-5",
            "--effort", "medium",
            "--label", f"gemini-review-{norm}",
            "--budget-usd", f"{budget_cap:.2f}",
            "--prompt-file", prompt_file,
            "--files", diff_file,
            "--out", review_out,
        ]
        proc = subprocess.run(cmd, cwd=repo, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if proc.returncode != 0:
            err_tail = (proc.stderr or proc.stdout).strip().splitlines()[-5:]
            return CheckOutcome("Sonnet review", False, f"api_task.py failed (exit {proc.returncode})", err_tail)

    review_text = ""
    if os.path.isfile(review_out):
        with open(review_out, encoding="utf-8", errors="replace") as f:
            review_text = f.read()
    has_high, highs = has_high_sonnet_findings(review_text)
    rel_review = norm_rel(os.path.relpath(review_out, repo))
    if has_high:
        return CheckOutcome("Sonnet review", False, f"{len(highs)} HIGH finding(s) -> {rel_review}", highs[:10])
    return CheckOutcome("Sonnet review", True, f"0 HIGH findings -> {rel_review}", [])


def format_gate_markdown(task_id: str, branch: str, base_branch: str, outcomes: Sequence[CheckOutcome]) -> str:
    norm = normalize_task_id(task_id)
    now = dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    overall = "PASS" if all(o.passed is not False for o in outcomes) else "FAIL"
    lines = [
        f"# Gemini Gate `{norm}` - **{overall}** ({now})",
        f"- Branch: `{branch}` vs `{base_branch}`",
        "",
        "| Check | Status | Details |",
        "|---|---|---|",
    ]
    for o in outcomes:
        summary_clean = o.summary.replace("|", "\\|")
        lines.append(f"| {o.name} | **{o.status_label}** | {summary_clean} |")
    for o in outcomes:
        if o.details:
            lines.append("")
            lines.append(f"## {o.name} ({o.status_label})")
            for d in o.details:
                lines.append(f"- {d}")
    return "\n".join(lines) + "\n"


def cmd_pre(args: argparse.Namespace) -> int:
    repo = os.path.abspath(args.worktree)
    norm = normalize_task_id(args.task)
    task_file = find_task_file(repo, norm)
    task_text = ""
    if task_file:
        with open(task_file, encoding="utf-8", errors="replace") as f:
            task_text = f.read()
    base = detect_base_branch(repo, task_text, args.base)
    loc = check_location(repo, norm)
    changed = get_changed_files(repo, base)
    if task_file:
        allowed = check_allowed_paths(changed, task_text, norm, repo=repo)
    else:
        allowed = CheckOutcome("Allowed paths", False, f"Task file docs/port/gemini/inbox/{norm}-*.md not found", [])

    rc, branch, _ = _run_git(repo, ["rev-parse", "--abbrev-ref", "HEAD"])
    has_cpp = any(f.lower().startswith("source/") and f.lower().endswith((*CPP_EXTENSIONS, ".build.cs", ".target.cs")) for f in changed)
    has_tg = any(f.lower().startswith("tools/trainingground/") for f in changed)
    has_py_tools = any(f.lower().startswith("scripts/tools/") and f.lower().endswith(".py") for f in changed)

    payload = {
        "task": norm,
        "repo": repo,
        "branch": branch if rc == 0 else "unknown",
        "base": base,
        "task_file": norm_rel(os.path.relpath(task_file, repo)) if task_file else "",
        "changed_files": changed,
        "has_cpp": has_cpp,
        "has_tg": has_tg,
        "has_py_tools": has_py_tools,
        "pre_outcomes": [
            {"name": loc.name, "passed": loc.passed, "summary": loc.summary, "details": loc.details},
            {"name": allowed.name, "passed": allowed.passed, "summary": allowed.summary, "details": allowed.details},
        ],
    }
    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump(payload, f, ensure_ascii=False, indent=2)
    else:
        print(json.dumps(payload, ensure_ascii=False, indent=2))
    return 0


def cmd_post(args: argparse.Namespace) -> int:
    with open(args.results_json, encoding="utf-8-sig") as f:
        data = json.load(f)

    repo = data["repo"]
    norm = data["task"]
    base = data["base"]
    branch = data["branch"]
    changed = data["changed_files"]

    outcomes: List[CheckOutcome] = [
        CheckOutcome(o["name"], o["passed"], o["summary"], list(o.get("details") or []))
        for o in data.get("outcomes", [])
    ]

    report_files = find_report_files(repo, norm)
    outcomes.append(check_citations(repo, report_files))
    outcomes.append(check_evidence(report_files))

    if args.sonnet_review:
        outcomes.append(run_sonnet_review(repo, norm, base, changed, report_files))
    else:
        outcomes.append(CheckOutcome("Sonnet review", None, "Not requested (pass -SonnetReview to enable)", []))

    md = format_gate_markdown(norm, branch, base, outcomes)
    gate_dir = os.path.join(repo, "Saved", "Orchestrator", "gate")
    os.makedirs(gate_dir, exist_ok=True)
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    gate_path = os.path.join(gate_dir, f"{norm}-{stamp}.md")
    with open(gate_path, "w", encoding="utf-8") as f:
        f.write(md)

    print(md)
    print(f"Saved gate output: {gate_path}")
    overall_ok = all(o.passed is not False for o in outcomes)
    return 0 if overall_ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description="Gemini pre-done quality gate helper")
    ap.add_argument("--phase", choices=["pre", "post"], required=True)
    ap.add_argument("--task", required=True)
    ap.add_argument("--worktree", default=os.getcwd())
    ap.add_argument("--base", default=None)
    ap.add_argument("--json-out", default="")
    ap.add_argument("--results-json", default="")
    ap.add_argument("--sonnet-review", action="store_true")
    args = ap.parse_args()
    if args.phase == "pre":
        return cmd_pre(args)
    return cmd_post(args)


if __name__ == "__main__":
    sys.exit(main())
