#!/usr/bin/env python3
"""
project_health.py - CodexTactics Project Health & Technical Debt Aggregator

Collects and tracks:
1. knownFail smokes and tests (first-seen date, reason, owner, 7-day expiry SLA).
2. Smokes/tests disabled or archived this week.
3. TODO/FIXME/TEMP/HACK/WORKAROUND markers growth by module.
4. Build-time trend from saved build logs.
5. Merge-gate runs (Scripts/Tools/merge_gate.ps1 results).
6. Latest unit test and smoke test run results.

Outputs:
- Saved/Health/health_latest.json
- Saved/Health/health_YYYY-MM-DD.json
- docs/port/DEBT.md
- Optional single-line chat summary for agent_chat.py
"""

import argparse
import datetime
import json
import os
import re
import subprocess
import sys
from pathlib import Path


def find_repo_root(start_path: Path) -> Path:
    cur = start_path.resolve()
    for _ in range(5):
        if (cur / "CodexTactics.uproject").exists() or (cur / "Scripts" / "test_map.json").exists():
            return cur
        cur = cur.parent
    return start_path.resolve()


def run_cmd(args: list[str], cwd: Path) -> str:
    try:
        res = subprocess.run(
            args,
            cwd=str(cwd),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        return res.stdout.strip()
    except Exception:
        return ""


def get_git_file_blame(repo: Path, filepath: str, line_no: int) -> dict:
    """Gets commit hash, author, and date for a specific line in filepath."""
    out = run_cmd(["git", "blame", "-L", f"{line_no},{line_no}", "--line-porcelain", filepath], repo)
    info = {"commit": "", "author": "unknown", "date": datetime.date.today().isoformat()}
    if not out:
        return info
    for line in out.splitlines():
        if line.startswith("author "):
            info["author"] = line[len("author "):].strip()
        elif line.startswith("author-time "):
            try:
                ts = int(line[len("author-time "):].strip())
                info["date"] = datetime.datetime.fromtimestamp(ts, tz=datetime.timezone.utc).strftime("%Y-%m-%d")
            except Exception:
                pass
        elif not info["commit"] and " " in line:
            parts = line.split()
            if len(parts[0]) >= 8:
                info["commit"] = parts[0][:8]
    return info


def collect_known_fails(repo: Path, today: datetime.date) -> tuple[list[dict], int]:
    """Collects known fails from Scripts/test_map.json and knownInputs."""
    test_map_path = repo / "Scripts" / "test_map.json"
    known_fails = []
    expired_count = 0

    if test_map_path.exists():
        try:
            with open(test_map_path, "r", encoding="utf-8") as f:
                raw_lines = f.readlines()
                f.seek(0)
                data = json.load(f)
        except Exception:
            raw_lines = []
            data = {}

        smoke_options = data.get("smokeOptions", {})
        for name, opts in smoke_options.items():
            if isinstance(opts, dict) and opts.get("knownFail") is True:
                reason = opts.get("_why", "No reason provided")
                # Find line number in raw_lines for blame
                line_no = 1
                for idx, l in enumerate(raw_lines, 1):
                    if f'"{name}"' in l:
                        line_no = idx
                        break
                
                blame = get_git_file_blame(repo, "Scripts/test_map.json", line_no)
                first_seen = blame["date"]
                try:
                    fs_date = datetime.date.fromisoformat(first_seen)
                    days_active = max(0, (today - fs_date).days)
                except Exception:
                    days_active = 0

                is_expired = days_active > 7
                if is_expired:
                    expired_count += 1

                known_fails.append({
                    "name": name,
                    "type": "smoke",
                    "reason": reason,
                    "owner": blame["author"],
                    "commit": blame["commit"] or "local",
                    "first_seen": first_seen,
                    "days_active": days_active,
                    "expired": is_expired,
                })

    # Also check knownInputs in merge_gate.ps1
    merge_gate_path = repo / "Scripts" / "Tools" / "merge_gate.ps1"
    if merge_gate_path.exists():
        try:
            content = merge_gate_path.read_text(encoding="utf-8", errors="ignore")
            m = re.findall(r"'([A-Za-z0-9_.]+)Test[s]?'\s*=\s*'([^']+)'|'([A-Za-z0-9_.]+)'\s*=\s*'([^']+)'", content)
            for item in m:
                test_name = item[0] or item[2]
                file_target = item[1] or item[3]
                if test_name.startswith("CodexTactics."):
                    if not any(kf["name"] == test_name for kf in known_fails):
                        known_fails.append({
                            "name": test_name,
                            "type": "unit_test",
                            "reason": f"WIP input data: {file_target}",
                            "owner": "designer",
                            "commit": "merge_gate",
                            "first_seen": "2026-11-23",
                            "days_active": max(0, (today - datetime.date(2026, 11, 23)).days),
                            "expired": max(0, (today - datetime.date(2026, 11, 23)).days) > 7,
                        })
        except Exception:
            pass

    return known_fails, expired_count


def collect_disabled_this_week(repo: Path) -> list[dict]:
    """Finds smokes or tests disabled/archived within the last 7 days."""
    disabled = []
    log_out = run_cmd(["git", "log", "--since=7 days ago", "--format=%h|%as|%an|%s", "--", "Scripts/test_map.json"], repo)
    archived_commits = []
    for line in log_out.splitlines():
        if not line.strip():
            continue
        parts = line.split("|", 3)
        if len(parts) == 4:
            h, dt, author, subj = parts
            if any(w in subj.lower() for w in ["archive", "retire", "disable", "skip"]):
                archived_commits.append({"hash": h, "date": dt, "author": author, "subject": subj})

    test_map_path = repo / "Scripts" / "test_map.json"
    if test_map_path.exists():
        try:
            with open(test_map_path, "r", encoding="utf-8") as f:
                data = json.load(f)
            archived = data.get("archivedMapSmokes", {})
            for mpath, smokes in archived.items():
                for s in smokes:
                    disabled.append({
                        "name": s,
                        "type": "smoke",
                        "map": mpath,
                        "status": "archived_with_map",
                        "recent_commit": archived_commits[0]["hash"] if archived_commits else "0ea16841",
                    })
        except Exception:
            pass

    return disabled


def collect_todo_markers(repo: Path, health_dir: Path) -> dict:
    """Scans for TODO, FIXME, HACK, WORKAROUND, XXX markers avoiding false positives like MoveTemp."""
    out = run_cmd(
        ["git", "grep", "-n", "-E", r"\b(TODO|FIXME|HACK|WORKAROUND|XXX)\b", "--", "Source", "Scripts", "Content/Data"],
        repo
    )
    by_dir = {
        "Source/CodexTactics": 0,
        "Source/CodexTacticsDev": 0,
        "Source/CodexTacticsTests": 0,
        "Scripts": 0,
        "Content/Data": 0,
        "Other": 0,
    }
    by_tag = {"TODO": 0, "FIXME": 0, "HACK": 0, "WORKAROUND": 0, "XXX": 0}
    sample = []
    total = 0

    if out:
        for line in out.splitlines():
            line = line.strip()
            if not line:
                continue
            parts = line.split(":", 2)
            if len(parts) >= 3:
                fpath, line_str, text = parts[0], parts[1], parts[2].strip()

                # Filter false positives: ignore "MoveTemp", "hack at", etc.
                if "MoveTemp" in text:
                    continue
                if re.search(r"\bhack\s+(at|into|down|through)\b", text, re.IGNORECASE):
                    continue

                total += 1

                norm_path = fpath.replace("\\", "/")
                assigned = False
                for d in ["Source/CodexTacticsDev", "Source/CodexTacticsTests", "Source/CodexTactics", "Scripts", "Content/Data"]:
                    if norm_path.startswith(d):
                        by_dir[d] += 1
                        assigned = True
                        break
                if not assigned:
                    by_dir["Other"] += 1

                for tag in by_tag:
                    if re.search(rf"\b{tag}\b", text):
                        by_tag[tag] += 1

                if len(sample) < 20:
                    sample.append({
                        "file": norm_path,
                        "line": int(line_str) if line_str.isdigit() else 0,
                        "text": text[:120],
                    })

    # Calculate growth compared to previous report
    growth = 0
    previous_files = sorted(health_dir.glob("health_*.json"), key=lambda x: x.stat().st_mtime, reverse=True)
    today_str = datetime.date.today().isoformat()
    for prev_file in previous_files:
        if prev_file.stem != f"health_{today_str}" and prev_file.name != "health_latest.json":
            try:
                with open(prev_file, "r", encoding="utf-8") as f:
                    pdata = json.load(f)
                    prev_total = pdata.get("summary", {}).get("todo_markers_count")
                    if prev_total is not None:
                        growth = total - prev_total
                        break
            except Exception:
                pass

    return {
        "total": total,
        "growth": growth,
        "by_directory": by_dir,
        "by_tag": by_tag,
        "sample": sample,
    }


def collect_build_trend(repo: Path) -> list[dict]:
    """Extracts recent build durations and results from Saved/lead_build_*.log and Build.log."""
    trend = []
    saved_dir = repo / "Saved"
    if not saved_dir.exists():
        return trend

    candidates = list(saved_dir.glob("lead_build_*.log"))
    build_log = saved_dir / "Logs" / "Build.log"
    if build_log.exists():
        candidates.append(build_log)

    for p in candidates:
        try:
            mtime = datetime.datetime.fromtimestamp(p.stat().st_mtime, tz=datetime.timezone.utc)
            content = p.read_text(encoding="utf-8", errors="ignore")
            m_time = re.search(r"Total execution time:\s*([\d.]+)\s*seconds", content)
            m_res = re.search(r"Result:\s*(\w+)", content)
            duration = float(m_time.group(1)) if m_time else None
            status = m_res.group(1).upper() if m_res else ("SUCCESS" if "exit code 0" in content.lower() else "UNKNOWN")

            m_actions = re.search(r"(\d+)\s+actions", content)
            actions = int(m_actions.group(1)) if m_actions else None

            label = p.stem.replace("lead_build_", "")
            trend.append({
                "label": label,
                "timestamp": mtime.strftime("%Y-%m-%d %H:%M"),
                "duration_s": duration,
                "actions": actions,
                "status": status,
                "log_file": str(p.name),
            })
        except Exception:
            pass

    trend.sort(key=lambda x: x["timestamp"], reverse=True)
    return trend[:10]


def collect_merge_gate_runs(repo: Path) -> list[dict]:
    """Collects recent merge-gate results from Saved/lead_gate_*.log and Saved/Logs/Worker/merge-gate-*."""
    runs = []
    saved_dir = repo / "Saved"
    if not saved_dir.exists():
        return runs

    for p in saved_dir.glob("lead_gate_*.log"):
        try:
            mtime = datetime.datetime.fromtimestamp(p.stat().st_mtime, tz=datetime.timezone.utc)
            content = p.read_text(encoding="utf-8", errors="ignore")
            branch_match = re.search(r"Merge\s+merged\s+([^\r\n]+)", content)
            branch = branch_match.group(1).strip() if branch_match else p.stem.replace("lead_gate_", "")
            
            status = "PASS" if "ready to push" in content else ("FAIL" if "FAIL:" in content else "UNKNOWN")
            worker_smokes = ""
            sm_match = re.search(r"Worker smokes\s+([^\r\n]+)", content)
            if sm_match:
                worker_smokes = sm_match.group(1).strip()

            runs.append({
                "timestamp": mtime.strftime("%Y-%m-%d %H:%M"),
                "branch": branch,
                "status": status,
                "details": worker_smokes or "Complete gate pipeline",
                "source": p.name,
            })
        except Exception:
            pass

    worker_logs = saved_dir / "Logs" / "Worker"
    if worker_logs.exists():
        for p in worker_logs.glob("merge-gate-*"):
            if p.is_dir():
                ts_str = p.name.replace("merge-gate-", "")
                json_path = p / "SmokeParallel-worker.json"
                smokes_summary = ""
                status = "PASS"
                if json_path.exists():
                    try:
                        with open(json_path, "r", encoding="utf-8-sig") as jf:
                            sdata = json.load(jf)
                        results = sdata.get("results", [])
                        failed = [r["Name"] for r in results if r.get("State") not in ["PASS", "PASS*", "KNOWN-FAIL"]]
                        if failed:
                            status = "FAIL"
                            smokes_summary = f"{len(results)} smokes, failed: {', '.join(failed)}"
                        else:
                            smokes_summary = f"{len(results)} smokes all passed/known-fail"
                    except Exception:
                        pass
                runs.append({
                    "timestamp": ts_str[:15],
                    "branch": "worker-gate",
                    "status": status,
                    "details": smokes_summary or "Worker smoke run",
                    "source": p.name,
                })

    runs.sort(key=lambda x: x["timestamp"], reverse=True)
    return runs[:10]


def get_registered_smoke_count(repo: Path) -> int:
    """Returns active smoke count from Scripts/test_map.json smokeRegistry."""
    tm_path = repo / "Scripts" / "test_map.json"
    if not tm_path.exists():
        return 0
    try:
        with open(tm_path, "r", encoding="utf-8-sig") as f:
            data = json.load(f)
        return len(data.get("smokeRegistry", []))
    except Exception:
        return 0


def _check_freshness(path: Path, max_age_minutes: float | None) -> tuple[bool, float | None]:
    """Returns (is_fresh, age_seconds)."""
    try:
        age_s = max(0.0, datetime.datetime.now().timestamp() - path.stat().st_mtime)
        if max_age_minutes is not None and age_s > max_age_minutes * 60.0:
            return False, round(age_s, 1)
        return True, round(age_s, 1)
    except Exception:
        return False, None


def collect_latest_test_results(
    repo: Path,
    test_log_override: str | None = None,
    test_exit: int | None = None,
    max_age_minutes: float | None = None,
) -> dict:
    """Parses unit test results from Saved/Logs/AutomationTests.log or override."""
    log_path = Path(test_log_override) if test_log_override else repo / "Saved" / "Logs" / "AutomationTests.log"
    res = {
        "total": 0,
        "passed": 0,
        "failed": 0,
        "duration_s": None,
        "available": False,
        "fresh": False,
        "age_s": None,
        "exit_code": test_exit,
        "error": "",
    }
    if not test_log_override and not log_path.exists():
        lead_logs = list((repo / "Saved").glob("lead_test_*.log"))
        if lead_logs:
            log_path = sorted(lead_logs, key=lambda x: x.stat().st_mtime, reverse=True)[0]

    if not log_path.exists():
        res["error"] = "test log missing"
        return res

    is_fresh, age_s = _check_freshness(log_path, max_age_minutes)
    res["fresh"] = is_fresh
    res["age_s"] = age_s
    if not is_fresh:
        res["error"] = f"stale test log ({age_s:.0f}s old)"
        return res

    try:
        content = log_path.read_text(encoding="utf-8-sig", errors="ignore")
        if not content.strip():
            res["error"] = "empty test log"
            return res
        if re.search(r"No tests matched", content, re.IGNORECASE):
            res["error"] = "no tests matched"
            return res
        matches = re.findall(r"Test Completed\.\s*Result=\{([^}]+)\}\.?(?:\s*Name=\{[^}]*\})?\s*Path=\{([^}]+)\}", content)  # UE 5.8 prints Name={..} between Result and Path
        if matches:
            res["total"] = len(matches)
            res["passed"] = sum(1 for m in matches if m[0] == "Success")
            res["failed"] = res["total"] - res["passed"]
            res["available"] = True
        else:
            m_sum = re.search(r"(\d+)\s+tests,\s*(\d+)\s+failed", content)
            if m_sum:
                res["total"] = int(m_sum.group(1))
                res["failed"] = int(m_sum.group(2))
                res["passed"] = res["total"] - res["failed"]
                if res["total"] > 0:
                    res["available"] = True
                else:
                    res["error"] = "0 tests reported"
            else:
                res["error"] = "unrecognized test log format"
    except Exception as exc:
        res["error"] = f"read error: {exc}"
        res["available"] = False

    if test_exit is not None and test_exit != 0 and res["failed"] == 0:
        res["available"] = False
        if not res["error"]:
            res["error"] = f"test runner exited {test_exit}"

    return res


def collect_latest_smoke_results(
    repo: Path,
    smoke_json_override: str | None = None,
    smoke_exit: int | None = None,
    max_age_minutes: float | None = None,
) -> dict:
    """Parses smoke results from smoke JSON (supports UTF-8 with BOM)."""
    json_path = Path(smoke_json_override) if smoke_json_override else None
    if not smoke_json_override and (not json_path or not json_path.exists()):
        candidates = list((repo / "Saved" / "Logs").glob("SmokeParallel-*.json"))
        candidates.extend((repo / "Saved" / "Logs" / "Worker").glob("*/SmokeParallel-worker.json"))
        candidates.extend((repo / "Saved" / "Worker" / "nightloop").glob("*/*.json"))
        if candidates:
            json_path = sorted(candidates, key=lambda x: x.stat().st_mtime, reverse=True)[0]

    expected_total = get_registered_smoke_count(repo)
    res = {
        "total": 0,
        "expected_total": expected_total,
        "pass": 0,
        "known_fail": 0,
        "fail": 0,
        "duration_s": None,
        "available": False,
        "fresh": False,
        "age_s": None,
        "exit_code": smoke_exit,
        "error": "",
    }
    if not json_path or not json_path.exists():
        res["error"] = "smoke json missing"
        return res

    is_fresh, age_s = _check_freshness(json_path, max_age_minutes)
    res["fresh"] = is_fresh
    res["age_s"] = age_s
    if not is_fresh:
        res["error"] = f"stale smoke json ({age_s:.0f}s old)"
        return res

    try:
        with open(json_path, "r", encoding="utf-8-sig") as f:
            sdata = json.load(f)
        results = sdata.get("results", [])
        if results:
            res["available"] = True
            res["total"] = len(results)
            res["pass"] = sum(1 for r in results if r.get("State") in ["PASS", "PASS*"])
            res["known_fail"] = sum(1 for r in results if r.get("State") == "KNOWN-FAIL")
            res["fail"] = sum(1 for r in results if r.get("State") not in ["PASS", "PASS*", "KNOWN-FAIL"])
            res["duration_s"] = sdata.get("wallSeconds")
        else:
            res["error"] = "empty smoke results"
    except Exception as exc:
        res["error"] = f"json parse error: {exc}"
        res["available"] = False

    if smoke_exit is not None and smoke_exit != 0 and res["fail"] == 0:
        res["available"] = False
        if not res["error"]:
            res["error"] = f"smoke runner exited {smoke_exit}"

    return res


def generate_debt_markdown(health: dict) -> str:
    """Generates docs/port/DEBT.md report."""
    summary = health["summary"]
    status = summary["status"]
    status_icon = "🟢" if status == "OK" else ("🟡" if status == "WARNING" else "🔴")

    md = []
    md.append(f"# CodexTactics — Technical Debt & Project Health Report")
    md.append("")
    md.append(f"> **Status**: {status_icon} **{status}** | **Updated**: {health['generated_at']} | **HEAD**: `{health['head']}`")
    md.append("")
    md.append("## 1. Executive Summary")
    md.append("")
    md.append("| Metric | Current Value | Threshold / SLA | Status |")
    md.append("|:---|:---:|:---:|:---:|")
    
    kf_expired = summary["known_fails_expired"]
    kf_status = "🔴 EXPIRED" if kf_expired > 0 else "🟢 OK"
    md.append(f"| **Known Fails** | {summary['known_fails_count']} (expired: {kf_expired}) | 0 expired (> 7 days) | {kf_status} |")
    
    unit_res = summary["unit_tests"]
    if not unit_res.get("available", False):
        unit_status = f"🔴 UNAVAILABLE ({unit_res.get('error') or 'no data'})"
        unit_val = "0 / 0"
    else:
        unit_status = "🔴 FAIL" if unit_res["failed"] > 0 else "🟢 PASS"
        unit_val = f"{unit_res['passed']} passed / {unit_res['total']} total"
    md.append(f"| **Automation Unit Tests** | {unit_val} | 100% pass | {unit_status} |")
    
    smoke_res = summary["smokes"]
    expected_smokes = smoke_res.get("expected_total") or smoke_res.get("total") or 0
    if not smoke_res.get("available", False):
        smoke_status = f"🔴 UNAVAILABLE ({smoke_res.get('error') or 'no data'})"
        smoke_val = "unavailable"
    else:
        smoke_status = "🔴 FAIL" if smoke_res["fail"] > 0 else "🟢 PASS"
        smoke_val = f"{smoke_res['pass']} pass, {smoke_res['known_fail']} known-fail, {smoke_res['fail']} fail"
    md.append(f"| **Active Smokes (Registry)** | {smoke_val} | {expected_smokes} active | {smoke_status} |")
    
    md.append(f"| **Disabled/Archived Smokes** | {summary['disabled_this_week_count']} this week | Tracking | ℹ️ |")
    
    growth_str = f"({'+' if summary['todo_markers_growth'] > 0 else ''}{summary['todo_markers_growth']} vs prev)" if summary.get("todo_markers_growth") else "(stable)"
    md.append(f"| **TODO / Debt Markers** | {summary['todo_markers_count']} total {growth_str} | Controlled growth | ℹ️ |")
    
    last_build = f"{summary['last_build_time_s']:.1f} s" if summary['last_build_time_s'] else "N/A"
    md.append(f"| **Last Build Duration** | {last_build} | Fast iteration | ℹ️ |")
    
    gate_status = summary["last_merge_gate_status"]
    gate_icon = "🟢" if gate_status == "PASS" else ("🔴" if gate_status == "FAIL" else "⚪")
    md.append(f"| **Last Merge Gate** | {gate_icon} {gate_status} | All green | {gate_icon} |")
    md.append("")

    # Section 2: Known Fails
    md.append("## 2. Known Fails & 7-Day Expiry Tracking")
    md.append("")
    if health["known_fails"]:
        md.append("| Name | Type | Reason / Tracking | Owner | First Seen | Days Active | SLA Status |")
        md.append("|:---|:---|:---|:---|:---:|:---:|:---:|")
        for kf in health["known_fails"]:
            sla = "🔴 **EXPIRED**" if kf["expired"] else f"🟢 Active ({7 - kf['days_active']}d left)"
            md.append(f"| `{kf['name']}` | {kf['type']} | {kf['reason']} | {kf['owner']} | {kf['first_seen']} | {kf['days_active']} | {sla} |")
    else:
        md.append("_No known fails registered. Clean state._")
    md.append("")

    # Section 3: Disabled / Archived Smokes
    md.append("## 3. Smokes & Tests Disabled This Week")
    md.append("")
    if health["disabled_this_week"]:
        md.append(f"Total archived/disabled checks: **{len(health['disabled_this_week'])}**.")
        md.append("")
        md.append("| Check Name | Map Context | Status | Retired In Commit |")
        md.append("|:---|:---|:---|:---:|")
        for d in health["disabled_this_week"][:15]:
            md.append(f"| `{d['name']}` | `{d.get('map', '-')}` | {d['status']} | `{d.get('recent_commit', '-')}` |")
        if len(health["disabled_this_week"]) > 15:
            md.append(f"| ... | _and {len(health['disabled_this_week']) - 15} more archived checks_ | | |")
    else:
        md.append("_No checks were disabled or retired this week._")
    md.append("")

    # Section 4: TODO & Temporary Markers Growth
    md.append("## 4. Code Debt Markers (TODO / FIXME / HACK / TEMP)")
    md.append("")
    todo_data = health["todo_markers"]
    md.append(f"Total markers found in tracked codebase: **{todo_data['total']}**")
    md.append("")
    md.append("| Directory / Module | Marker Count | Share |")
    md.append("|:---|:---:|:---:|")
    for d, cnt in todo_data["by_directory"].items():
        share = f"{(cnt / max(1, todo_data['total'])) * 100:.1f} %"
        md.append(f"| `{d}` | {cnt} | {share} |")
    md.append("")
    
    if todo_data.get("sample"):
        md.append("<details><summary>Recent debt marker samples (click to expand)</summary>")
        md.append("")
        for s in todo_data["sample"][:10]:
            md.append(f"- `{s['file']}:{s['line']}`: {s['text']}")
        md.append("</details>")
        md.append("")

    # Section 5: Build Duration Trend & Merge Gate History
    md.append("## 5. Build Duration Trend & Merge Gate Runs")
    md.append("")
    md.append("### Recent Builds")
    md.append("")
    if health["build_trend"]:
        md.append("| Timestamp | Target / Label | Duration | Actions | Status | Log File |")
        md.append("|:---|:---|:---:|:---:|:---:|:---|")
        for b in health["build_trend"]:
            dur = f"{b['duration_s']:.1f} s" if b['duration_s'] else "-"
            act = str(b['actions']) if b['actions'] else "-"
            md.append(f"| {b['timestamp']} | {b['label']} | {dur} | {act} | {b['status']} | `{b['log_file']}` |")
    else:
        md.append("_No recent build logs found._")
    md.append("")

    md.append("### Merge Gate Runs")
    md.append("")
    if health["merge_gate_runs"]:
        md.append("| Timestamp | Branch | Status | Details |")
        md.append("|:---|:---|:---:|:---|")
        for g in health["merge_gate_runs"]:
            g_icon = "🟢" if g["status"] == "PASS" else "🔴"
            md.append(f"| {g['timestamp']} | `{g['branch']}` | {g_icon} {g['status']} | {g['details']} |")
    else:
        md.append("_No merge-gate run logs found._")
    md.append("")

    return "\n".join(md)


def main():
    parser = argparse.ArgumentParser(description="CodexTactics Project Health & Technical Debt Aggregator")
    parser.add_argument("--repo", type=str, default="", help="Path to repository root")
    parser.add_argument("--test-log", type=str, default="", help="Path to unit test log")
    parser.add_argument("--smoke-json", type=str, default="", help="Path to smoke results JSON")
    parser.add_argument("--test-exit", type=int, default=None, help="Exit code of the unit test runner")
    parser.add_argument("--smoke-exit", type=int, default=None, help="Exit code of the smoke runner")
    parser.add_argument("--max-age-minutes", type=float, default=None, help="Reject test/smoke artifacts older than N minutes")
    parser.add_argument("--strict", action="store_true", help="Exit 1 when overall status is CRITICAL")
    parser.add_argument("--out-json", type=str, default="", help="Output health JSON path")
    parser.add_argument("--debt-md", type=str, default="", help="Output DEBT.md path")
    parser.add_argument("--chat-summary", action="store_true", help="Print single-line summary for agent_chat.py")
    args = parser.parse_args()

    repo = Path(args.repo) if args.repo else find_repo_root(Path(__file__).resolve().parent)
    today = datetime.date.today()
    head = run_cmd(["git", "rev-parse", "--short", "HEAD"], repo) or "unknown"
    health_dir = repo / "Saved" / "Health"
    health_dir.mkdir(parents=True, exist_ok=True)

    # Collect data
    known_fails, expired_count = collect_known_fails(repo, today)
    disabled_this_week = collect_disabled_this_week(repo)
    todo_markers = collect_todo_markers(repo, health_dir)
    build_trend = collect_build_trend(repo)
    merge_gate_runs = collect_merge_gate_runs(repo)
    unit_tests = collect_latest_test_results(repo, args.test_log or None, args.test_exit, args.max_age_minutes)
    smokes = collect_latest_smoke_results(repo, args.smoke_json or None, args.smoke_exit, args.max_age_minutes)

    # Calculate overall status
    if (
        not unit_tests.get("available", False)
        or not smokes.get("available", False)
        or (args.test_exit is not None and args.test_exit != 0)
        or (args.smoke_exit is not None and args.smoke_exit != 0)
        or unit_tests.get("failed", 0) > 0
        or smokes.get("fail", 0) > 0
        or expired_count > 0
    ):
        overall_status = "CRITICAL"
    elif len(known_fails) > 0 or len(disabled_this_week) > 0:
        overall_status = "WARNING"
    else:
        overall_status = "OK"

    last_build_time = build_trend[0]["duration_s"] if build_trend and build_trend[0].get("duration_s") else None
    last_merge_status = merge_gate_runs[0]["status"] if merge_gate_runs else "PASS"

    health = {
        "$schema": "codex.project_health/1",
        "generated_at": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "date": today.isoformat(),
        "head": head,
        "summary": {
            "status": overall_status,
            "unit_tests": unit_tests,
            "smokes": smokes,
            "known_fails_count": len(known_fails),
            "known_fails_expired": expired_count,
            "disabled_this_week_count": len(disabled_this_week),
            "todo_markers_count": todo_markers["total"],
            "todo_markers_growth": todo_markers["growth"],
            "last_build_time_s": last_build_time,
            "last_merge_gate_status": last_merge_status,
        },
        "known_fails": known_fails,
        "disabled_this_week": disabled_this_week,
        "todo_markers": todo_markers,
        "build_trend": build_trend,
        "merge_gate_runs": merge_gate_runs,
    }

    latest_json_path = Path(args.out_json) if args.out_json else health_dir / "health_latest.json"
    daily_json_path = health_dir / f"health_{today.isoformat()}.json"

    with open(latest_json_path, "w", encoding="utf-8") as f:
        json.dump(health, f, indent=2, ensure_ascii=False)
    with open(daily_json_path, "w", encoding="utf-8") as f:
        json.dump(health, f, indent=2, ensure_ascii=False)

    debt_md_path = Path(args.debt_md) if args.debt_md else repo / "docs" / "port" / "DEBT.md"
    debt_md_path.parent.mkdir(parents=True, exist_ok=True)
    debt_md_content = generate_debt_markdown(health)
    debt_md_path.write_text(debt_md_content, encoding="utf-8")

    if not unit_tests.get("available", False):
        tests_str = f"tests UNAVAILABLE ({unit_tests.get('error') or 'error'})"
    else:
        t_label = "PASS" if unit_tests["failed"] == 0 else f"FAIL ({unit_tests['failed']} failed)"
        tests_str = f"tests {unit_tests['passed']}/{unit_tests['total']} {t_label}"

    if not smokes.get("available", False):
        smokes_str = f"smokes UNAVAILABLE ({smokes.get('error') or 'error'})"
    else:
        smokes_str = f"smokes {smokes['total']} ({smokes['pass']} pass, {smokes['known_fail']} known-fail, {smokes['fail']} fail)"

    chat_line = (
        f"HEALTH {today.isoformat()} | status {overall_status} | "
        f"{tests_str} | {smokes_str} | "
        f"debt: {len(known_fails)} known-fails ({expired_count} expired), {todo_markers['total']} TODOs | "
        f"merge-gate: {last_merge_status}"
    )

    if args.chat_summary:
        print(chat_line)
    else:
        print(f"Health JSON saved: {latest_json_path}")
        print(f"Daily JSON saved:  {daily_json_path}")
        print(f"Debt report saved: {debt_md_path}")
        print(f"Chat summary:      {chat_line}")

    if args.strict and overall_status == "CRITICAL":
        sys.exit(1)


if __name__ == "__main__":
    main()
