#!/usr/bin/env python3
"""scenario_regression.py - Scenario smoke regression runner (smart enemies T-10).

Runs scenario smokes across multiple seeds, outputting:
1. Quick Day Mode (--scenario X --seeds 20..50, target < 15 min):
   Short digest for the day loop: pass rate, the worst 3 seeds, decision trace of one failed seed.
2. Night Mode (nightly sweep 16:30-08:00):
   Regressions since yesterday, stable seeds, flaky seeds, markdown and JSON summary.

Reuses smoke_parallel.ps1 with -SmokeArgs "Seed=S" for seed-specific execution.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

REPO_ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = REPO_ROOT / "Saved" / "Logs"
REPORTS_DIR = REPO_ROOT / "Saved" / "Reports"

SCENARIO_MAP = {
    "crossroads": "PmcCrossroadsSmoke",
    "alley": "PmcAlleySmoke",
    "roofs": "PmcRoofDefenceSmoke",
    "chokepoint": "PmcArenaTacticalSmoke",
    "stalemate": "PmcStalemateSmoke",
    "encounter": "PmcEncounterSmoke",
}


def parse_seed_spec(spec: str) -> List[int]:
    """Parse seed specifications like '20' (1..20), '1-20', or '1,2,5,10'."""
    spec = spec.strip()
    if not spec:
        return list(range(1, 21))

    # Single integer -> 1..N
    if spec.isdigit():
        n = int(spec)
        return list(range(1, n + 1))

    seeds: List[int] = []
    parts = [p.strip() for p in spec.split(",") if p.strip()]
    for p in parts:
        if "-" in p:
            sub = p.split("-", 1)
            if sub[0].strip().isdigit() and sub[1].strip().isdigit():
                start, end = int(sub[0].strip()), int(sub[1].strip())
                seeds.extend(range(start, end + 1))
        elif p.isdigit():
            seeds.append(int(p))

    return sorted(list(set(seeds))) if seeds else list(range(1, 21))


def resolve_smoke_command(scenario_or_smoke: str) -> str:
    """Map scenario alias to smoke command name."""
    s = scenario_or_smoke.strip().lower()
    if s in SCENARIO_MAP:
        return SCENARIO_MAP[s]
    # Direct smoke name
    for k, v in SCENARIO_MAP.items():
        if v.lower() == s:
            return v
    if scenario_or_smoke.endswith("Smoke"):
        return scenario_or_smoke
    return f"{scenario_or_smoke}Smoke"


def run_single_smoke_seed(
    smoke: str,
    seed: int,
    timeout_s: int = 180,
    remote: str = "auto",
    mock: bool = False,
    mock_fail_seeds: Optional[Set[int]] = None,
) -> dict:
    """Run a single smoke on a specific seed and parse outcome."""
    if mock:
        is_fail = bool(mock_fail_seeds and seed in mock_fail_seeds)
        return {
            "smoke": smoke,
            "seed": seed,
            "passed": not is_fail,
            "duration_s": 12.5,
            "log": f"Mock-Smoke-{smoke}-{seed}.log",
            "failure_reason": "Mock failure: objective captured by attackers" if is_fail else None,
            "decisions": [
                {"t": 0.0, "who": "Operator Lind", "order": "Ambush", "score": 1.5},
                {"t": 3.5, "who": "Operator Lind", "order": "FallBack", "score": 1.2},
            ] if is_fail else [],
        }

    script_path = REPO_ROOT / "Scripts" / "smoke_parallel.ps1"
    temp_json = LOG_DIR / f"SmokeParallel-seed-{seed}-{os.getpid()}.json"

    cmd = [
        "powershell",
        "-ExecutionPolicy", "Bypass",
        "-File", str(script_path),
        "-Commands", smoke,
        "-SmokeArgs", f"Seed={seed}",
        "-ResultJson", str(temp_json),
        "-Remote", remote,
        "-TimeoutSeconds", str(timeout_s),
        "-Serial",
    ]

    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    passed = False
    duration_s = 0.0
    log_path = None
    fail_reason = None

    if temp_json.is_file():
        try:
            data = json.loads(temp_json.read_text(encoding="utf-8"))
            results = data.get("results") or []
            if results:
                r0 = results[0]
                state = r0.get("State", "")
                passed = (state in ("PASS", "PASS*"))
                duration_s = float(r0.get("Seconds", 0.0))
                log_path = r0.get("Log")
                if not passed:
                    fail_reason = r0.get("Reason") or "Smoke test failed verification"
        except Exception:
            pass
        finally:
            try:
                temp_json.unlink()
            except OSError:
                pass
    else:
        passed = (proc.returncode == 0)

    # Extract log details if failed
    decisions = []
    if not passed and log_path and Path(log_path).is_file():
        try:
            log_lines = Path(log_path).read_text(encoding="utf-8", errors="replace").splitlines()
            for line in log_lines:
                if "Smoke RESULT: FAIL" in line:
                    fail_reason = line.strip()
                if "[PmcAI]" in line and "->" in line:
                    m = re.search(r"Operator\s+([A-Za-z0-9_-]+)\s+->\s+([A-Za-z0-9_-]+)\s+\(score\s+([0-9.]+)", line)
                    if m:
                        decisions.append({"t": 0.0, "who": f"Operator {m.group(1)}", "order": m.group(2), "score": float(m.group(3))})
        except Exception:
            pass

    return {
        "smoke": smoke,
        "seed": seed,
        "passed": passed,
        "duration_s": duration_s,
        "log": str(log_path) if log_path else None,
        "failure_reason": fail_reason,
        "decisions": decisions,
    }


def run_scenario_regression(
    scenario: str,
    seeds: List[int],
    parallel: int = 2,
    remote: str = "auto",
    mock: bool = False,
    mock_fail_seeds: Optional[Set[int]] = None,
) -> dict:
    """Run regression sweep for a scenario across a list of seeds."""
    smoke = resolve_smoke_command(scenario)
    results: List[dict] = []

    for seed in seeds:
        res = run_single_smoke_seed(smoke, seed, remote=remote, mock=mock, mock_fail_seeds=mock_fail_seeds)
        results.append(res)

    passed_count = sum(1 for r in results if r["passed"])
    failed_results = [r for r in results if not r["passed"]]
    failed_seeds = [r["seed"] for r in failed_results]

    pass_rate = (passed_count / len(results)) if results else 0.0
    worst_seeds = failed_seeds[:3] if failed_seeds else [r["seed"] for r in sorted(results, key=lambda x: -x["duration_s"])[:3]]

    return {
        "scenario": scenario,
        "smoke": smoke,
        "total_seeds": len(results),
        "passed_count": passed_count,
        "failed_count": len(failed_results),
        "pass_rate": round(pass_rate * 100.0, 1),
        "failed_seeds": failed_seeds,
        "worst_seeds": worst_seeds,
        "results": results,
        "sample_failed_trace": failed_results[0] if failed_results else None,
    }


def compare_with_yesterday(today_data: dict, yesterday_data: dict) -> dict:
    """Compare today's sweep with yesterday's to find regressions and flaky seeds."""
    y_results = yesterday_data.get("seeds") or {}
    t_results = {r["seed"]: r["passed"] for r in today_data.get("results", [])}

    regressions = []  # passed yesterday, failed today
    fixes = []        # failed yesterday, passed today
    stable_pass = []
    flaky = []

    for seed, passed_today in t_results.items():
        if seed in y_results:
            passed_yesterday = y_results[seed]
            if passed_yesterday and not passed_today:
                regressions.append(seed)
            elif not passed_yesterday and passed_today:
                fixes.append(seed)
            elif passed_today:
                stable_pass.append(seed)
            else:
                flaky.append(seed)

    return {
        "regressions": regressions,
        "fixes": fixes,
        "flaky_seeds": flaky,
        "stable_seeds": stable_pass,
    }


def format_day_digest(res: dict) -> str:
    """Format short day-loop digest (< 25 lines) per section 5a."""
    lines = []
    lines.append(f"# Day Loop Digest: {res['scenario']} ({res['smoke']})")
    lines.append(f"- **Pass Rate**: {res['passed_count']} / {res['total_seeds']} ({res['pass_rate']}%)")
    lines.append(f"- **Worst Seeds**: {', '.join(str(s) for s in res['worst_seeds']) if res['worst_seeds'] else 'None'}")
    if res["failed_seeds"]:
        lines.append(f"- **Failed Seeds**: {', '.join(str(s) for s in res['failed_seeds'])}")
    else:
        lines.append("- **Status**: ALL SEEDS GREEN")

    # Trace of one failed seed
    failed_sample = res.get("sample_failed_trace")
    if failed_sample:
        lines.append("")
        lines.append(f"### Decision Trace (Failed Seed {failed_sample['seed']}):")
        if failed_sample.get("failure_reason"):
            lines.append(f"- **Failure Reason**: `{failed_sample['failure_reason']}`")
        decisions = failed_sample.get("decisions") or []
        if decisions:
            lines.append("| Time | Fighter | Decision | Score |")
            lines.append("|---|---|---|---|")
            for d in decisions[:8]:
                lines.append(f"| {d['t']:.1f}s | {d['who']} | **{d['order']}** | {d['score']:.2f} |")
        else:
            lines.append(f"- Log path: `{failed_sample.get('log', 'none')}`")
    return "\n".join(lines)


def format_night_report(all_scenarios: List[dict], comparison: Optional[dict] = None) -> str:
    """Format comprehensive nightly regression report."""
    now_str = dt.datetime.now().strftime("%Y-%m-%d %H:%M")
    lines = []
    lines.append(f"# Nightly Scenario Regression Report ({now_str})")
    lines.append("")
    lines.append("## 1. Summary by Scenario")
    lines.append("| Scenario | Smoke | Seeds | Pass Rate | Failed Seeds | Worst Seeds |")
    lines.append("|---|---|---|---|---|---|")
    for s in all_scenarios:
        f_seeds = ", ".join(str(x) for x in s["failed_seeds"][:5]) if s["failed_seeds"] else "none"
        w_seeds = ", ".join(str(x) for x in s["worst_seeds"][:3]) if s["worst_seeds"] else "-"
        lines.append(f"| **{s['scenario']}** | `{s['smoke']}` | {s['total_seeds']} | **{s['pass_rate']}%** ({s['passed_count']}/{s['total_seeds']}) | {f_seeds} | {w_seeds} |")
    lines.append("")

    if comparison:
        lines.append("## 2. Changes Since Yesterday")
        reg = comparison.get("regressions") or []
        fix = comparison.get("fixes") or []
        flaky = comparison.get("flaky_seeds") or []
        lines.append(f"- **Regressions (passed yesterday, failed today)**: {', '.join(str(s) for s in reg) if reg else 'None'}")
        lines.append(f"- **Fixes (failed yesterday, passed today)**: {', '.join(str(s) for s in fix) if fix else 'None'}")
        lines.append(f"- **Flaky Seeds (repeating failures)**: {', '.join(str(s) for s in flaky) if flaky else 'None'}")
        lines.append("")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--scenario", default="crossroads", help="Scenario alias (crossroads, alley, roofs) or smoke name")
    parser.add_argument("--seeds", default="20", help="Seed count (e.g. '20') or range ('1-20') or list ('1,2,5')")
    parser.add_argument("--day", action="store_true", help="Force quick day mode (compact digest)")
    parser.add_argument("--night", action="store_true", help="Night mode (multi-scenario sweep report)")
    parser.add_argument("--parallel", type=int, default=2, help="Number of jobs side by side")
    parser.add_argument("--remote", default="auto", choices=["auto", "on", "off"], help="Remote smoke execution")
    parser.add_argument("--mock", action="store_true", help="Mock execution for fast testing")
    parser.add_argument("--out-md", help="Path to write markdown output")
    parser.add_argument("--out-json", help="Path to write JSON summary")
    parser.add_argument("--compare-yesterday", help="Path to yesterday's regression JSON file")
    args = parser.parse_args()

    seeds = parse_seed_spec(args.seeds)
    is_day_mode = args.day or (not args.night and len(seeds) <= 50 and args.scenario != "all")

    if args.scenario == "all" or args.night:
        scenarios = ["crossroads", "alley", "roofs"]
        all_results = []
        for sc in scenarios:
            res = run_scenario_regression(sc, seeds, parallel=args.parallel, remote=args.remote, mock=args.mock)
            all_results.append(res)

        comparison = None
        if args.compare_yesterday and os.path.isfile(args.compare_yesterday):
            try:
                yest = json.loads(Path(args.compare_yesterday).read_text(encoding="utf-8"))
                # Compare first scenario as sample
                if all_results:
                    comparison = compare_with_yesterday(all_results[0], yest)
            except Exception:
                pass

        md_output = format_night_report(all_results, comparison)
        json_output = {"date": dt.date.today().isoformat(), "scenarios": all_results, "comparison": comparison}
    else:
        res = run_scenario_regression(args.scenario, seeds, parallel=args.parallel, remote=args.remote, mock=args.mock)
        md_output = format_day_digest(res)
        json_output = res

    if args.out_md:
        p = Path(args.out_md)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(md_output, encoding="utf-8")
        print(f"Markdown report written to {p}")
    else:
        print(md_output)

    if args.out_json:
        p = Path(args.out_json)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(json.dumps(json_output, indent=2), encoding="utf-8")
        print(f"JSON summary written to {p}")


if __name__ == "__main__":
    main()
