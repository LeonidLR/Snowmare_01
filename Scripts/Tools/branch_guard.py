"""Read-only pre-merge branch guard. Run from the repository, not a checkout of the agent branch.

python Scripts/Tools/branch_guard.py gemini/task --base main --json
Uses merge-base..branch (the branch contribution), never checks out/fetches/runs hooks.
Evidence means matching output lines, not proof of their provenance/authenticity.
At least one changed, surviving *.report.md must contain BOTH build and test output.
Installed CODEX_AGENT pre-push guard was found at .git/hooks/pre-push. That local
hook is not tracked: no branch diff can attest to its on-disk integrity. Versioned
.githooks/** and pre-push-named guard files are protected below.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path, PurePosixPath

PROTECTED = frozenset(path.casefold() for path in (
    "Scripts/Tools/typesafe_triage.py", "Scripts/Tools/branch_guard.py",
    "Scripts/agent_lock.ps1", "Config/DefaultScalability.ini",
    "Content/Data/LevelJson/L_Gen_Depot_1.json", "Scripts/Tools/local_task.py",
    "CLAUDE.md", ".git/hooks/pre-push",
))
PREFIXES = (".githooks/", ".claude/", "content/vfx/", "content/maps/")
REPORT_DIR = "docs/port/gemini/outbox"
CPP_SUFFIXES = {".cpp", ".h", ".hpp", ".cc", ".cxx", ".hh", ".hxx", ".inl"}
# Allow engine timestamp/log prefixes, not prose/bullets/quoted examples.
PREFIX = r"\s*(?:\[[^\]\r\n]*\]\s*)*(?:Log[\w]+:\s*(?:(?:Display|Warning):\s*)?)?"
BUILD = re.compile(PREFIX + r"(?:Result:\s*Succeeded|Exit code\s*:?\s*0)\s*[.!]?\s*$", re.I)
TESTS = re.compile(PREFIX + r"([1-9]\d*) tests?,\s*0 failed\b[^\r\n]*$", re.I)
SMOKE_MENTION = re.compile(r"\bsmokes?\b|\b\w+Smoke\b", re.I)
SMOKE_OUTPUT = re.compile(
    PREFIX + r"(?:\[Smoke\]\s*|Smoke\b|\w+Smoke\b)[^\r\n]*\b(?:PASS|FAIL|passed|failed)\b", re.I)
NOT_RUN = re.compile(r"\b(?:not run|not executed|not tested|not claimed|pending|no smokes)\b", re.I)


def git(repo: Path, *args: str) -> str:
    result = subprocess.run(["git", "-C", str(repo), *args], capture_output=True,
                            encoding="utf-8", errors="replace", check=False)
    if result.returncode:
        raise RuntimeError((result.stderr or result.stdout).strip() or "git command failed")
    return result.stdout


def is_protected(path: str) -> bool:
    normalized = path.replace("\\", "/").casefold()
    name = PurePosixPath(normalized).name
    # Cover a future relocated versioned pre-push guard too.
    pre_push = name in {"pre-push", "pre-push.sample"} or bool(re.fullmatch(
        r"(?:pre[-_]push(?:[-_]guard)?|guard[-_]pre[-_]push)\.(?:py|ps1|sh|cmd|bat)", name))
    return (normalized in PROTECTED or normalized.startswith(PREFIXES)
            or normalized in {prefix.rstrip("/") for prefix in PREFIXES} or pre_push)


def evidence(text: str) -> dict:
    lines = text.splitlines()
    build = [line.strip() for line in lines if BUILD.fullmatch(line)]
    tests = [line.strip() for line in lines if TESTS.fullmatch(line)]
    claimed_smokes = any(SMOKE_MENTION.search(line) and not NOT_RUN.search(line) for line in lines)
    smoke_output = any(SMOKE_OUTPUT.match(line) for line in lines)
    return {"build": build, "tests": tests, "complete": bool(build and tests),
            "smoke_claim_without_output": claimed_smokes and not smoke_output}


def check_branch(branch: str, base: str = "main", repo: Path | None = None) -> dict:
    repo = Path.cwd() if repo is None else Path(repo)
    result = {"branch": branch, "base": base, "status": "FAIL", "failures": [],
              "warnings": [], "protected_paths": [], "reports": [], "cpp_lines": 0,
              "note": "Local .git/hooks/pre-push is outside the versioned branch diff."}
    try:
        # Resolve first, then use only verified commit IDs in subsequent git commands.
        head = git(repo, "rev-parse", "--verify", "--end-of-options", branch + "^{commit}").strip()
        base_commit = git(repo, "rev-parse", "--verify", "--end-of-options", base + "^{commit}").strip()
        ancestor = git(repo, "merge-base", base_commit, head).strip()
        result.update(commit=head, base_commit=base_commit, merge_base=ancestor)
        paths = git(repo, "diff", "--no-ext-diff", "--no-textconv", "--no-renames",
                    "--name-only", "-z", ancestor, head, "--").split("\0")
        paths = sorted(path for path in paths if path)
        result["protected_paths"] = [path for path in paths if is_protected(path)]
        if result["protected_paths"]:
            result["failures"].append("PROTECTED: " + ", ".join(result["protected_paths"]))
        stats = git(repo, "diff", "--no-ext-diff", "--no-textconv", "--no-renames",
                    "--numstat", "-z", ancestor, head, "--")
        for record in filter(None, stats.split("\0")):
            added, deleted, path = record.split("\t", 2)
            if PurePosixPath(path).suffix.casefold() in CPP_SUFFIXES and added != "-" and deleted != "-":
                result["cpp_lines"] += int(added) + int(deleted)
        if result["cpp_lines"] > 800:
            result["warnings"].append("SIZE: %d changed C++ lines (>800); split into smaller parts." % result["cpp_lines"])
        # A deleted report is not evidence. Read report bytes from the branch, not the worktree.
        surviving = set(filter(None, git(repo, "diff", "--no-ext-diff", "--no-textconv", "--no-renames",
                        "--diff-filter=ACMT", "--name-only", "-z", ancestor, head, "--").split("\0")))
        reports = [path for path in paths if path in surviving
                   and str(PurePosixPath(path).parent) == REPORT_DIR and path.endswith(".report.md")]
        for path in reports:
            checked = evidence(git(repo, "show", "--no-ext-diff", "--no-textconv", head + ":" + path))
            result["reports"].append(dict(path=path, **checked))
            if checked["smoke_claim_without_output"]:
                result["warnings"].append("EVIDENCE: smoke claim without output lines: " + path)
        if not reports:
            result["failures"].append("EVIDENCE: no changed, surviving docs/port/gemini/outbox/*.report.md")
        elif not any(report["complete"] for report in result["reports"]):
            details = []
            for report in result["reports"]:
                missing = []
                if not report["build"]: missing.append("build output (Result: Succeeded / Exit code 0)")
                if not report["tests"]: missing.append("test output (N tests, 0 failed; N > 0)")
                details.append(report["path"] + " missing " + " and ".join(missing))
            result["failures"].append("EVIDENCE: no report contains both outputs; " + "; ".join(details))
    except (OSError, RuntimeError, ValueError) as exc:
        result["failures"].append("GIT: " + " ".join(str(exc).split()))
    result["status"] = "FAIL" if result["failures"] else ("WARN" if result["warnings"] else "PASS")
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("branch")
    parser.add_argument("--base", default="main")
    parser.add_argument("--json", action="store_true", dest="as_json")
    args = parser.parse_args(argv)
    result = check_branch(args.branch, args.base)
    if args.as_json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        print("%s: %s against %s (%d changed C++ lines)" %
              (result["status"], args.branch, args.base, result["cpp_lines"]))
        for failure in result["failures"]: print("FAIL " + failure)
        for warning in result["warnings"]: print("WARN " + warning)
        for report in result["reports"]:
            print("REPORT %s: build=%s tests=%s" % (report["path"], bool(report["build"]), bool(report["tests"])))
        print("NOTE " + result["note"])
    return 1 if result["failures"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
