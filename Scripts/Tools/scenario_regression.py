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
    """Parse seed specifications like '20' (1..20), '20..50', '1-20', or '1,2,5,10'."""
    spec = spec.strip()
    if not spec:
        return list(range(1, 21))

    # Single integer -> 1..N
    if spec.isdigit():
        n = int(spec)
        return list(range(1, n + 1))

    # Handle '20..50' range notation
    spec = spec.replace("..", "-")
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


def parse_failed_log(log_path: Optional[str]) -> Tuple[Optional[str], List[dict]]:
    """Extract failure reason and AI decisions from a failed smoke log."""
    fail_reason = None
    decisions = []
    if log_path and Path(log_path).is_file():
        try:
            log_lines = Path(log_path).read_text(encoding="utf-8-sig", errors="replace").splitlines()
            for line in log_lines:
                if "Smoke RESULT: FAIL" in line:
                    fail_reason = line.strip()
                if "[PmcAI]" in line and "->" in line:
                    m = re.search(r"Operator\s+([A-Za-z0-9_-]+)\s+->\s+([A-Za-z0-9_-]+)\s+\(score\s+([0-9.]+)", line)
                    if m:
                        decisions.append({
                            "t": 0.0,
                            "who": f"Operator {m.group(1)}",
                            "order": m.group(2),
                            "score": float(m.group(3)),
                        })
        except Exception:
            pass
    return fail_reason, decisions


def run_scenario_regression(
    scenario: str,
    seeds: List[int],
    parallel: int = 2,
    remote: str = "auto",
    mock: bool = False,
    mock_fail_seeds: Optional[Set[int]] = None,
    timeout_s: int = 180,
) -> dict:
    """Run regression sweep for a scenario across a list of seeds in batch (Review 091 Point 2)."""
    smoke = resolve_smoke_command(scenario)
    results: List[dict] = []

    if mock:
        for seed in seeds:
            is_fail = bool(mock_fail_seeds and seed in mock_fail_seeds)
            results.append({
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
            })
    else:
        # Batch smoke execution: invoke smoke_parallel.ps1 ONCE per scenario
        script_path = REPO_ROOT / "Scripts" / "smoke_parallel.ps1"
        temp_json = LOG_DIR / f"SmokeParallel-batch-{smoke}-{os.getpid()}.json"

        cmd = [
            "powershell",
            "-ExecutionPolicy", "Bypass",
            "-File", str(script_path),
            "-Commands", smoke,
            "-Repeat", str(len(seeds)),
            "-SmokeArgs", "Seed={n}",
            "-NoRetry",
            "-Jobs", str(parallel),
            "-ResultJson", str(temp_json),
            "-Remote", remote,
            "-TimeoutSeconds", str(timeout_s),
        ]

        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

        batch_results = []
        if temp_json.is_file():
            try:
                # UTF-8-SIG to cleanly handle PowerShell UTF-8 BOM (Review 091 Point 1)
                data = json.loads(temp_json.read_text(encoding="utf-8-sig"))
                batch_results = data.get("results") or []
            except Exception as e:
                print(f"[scenario_regression] Warning: failed to parse {temp_json}: {e}", file=sys.stderr)
            finally:
                try:
                    temp_json.unlink()
                except OSError:
                    pass

        # Parse each result row
        for i, seed in enumerate(seeds):
            row = None
            if i < len(batch_results):
                row = batch_results[i]
                for r in batch_results:
                    if r.get("Repeat") == (i + 1):
                        row = r
                        break

            if row:
                state = row.get("State", "")
                passed = (state in ("PASS", "PASS*"))
                duration_s = float(row.get("Seconds", 0.0))
                log_path = row.get("Log")
                fail_reason = row.get("Reason")
            else:
                passed = (proc.returncode == 0)
                duration_s = 0.0
                log_path = str(LOG_DIR / f"Smoke-{smoke}-r{i+1}.log")
                fail_reason = "No result row found in batch output" if not passed else None

            decisions = []
            if not passed:
                extracted_reason, decisions = parse_failed_log(log_path)
                if extracted_reason:
                    fail_reason = extracted_reason
                elif not fail_reason:
                    fail_reason = "Smoke test failed verification"

            results.append({
                "smoke": smoke,
                "seed": seed,
                "passed": passed,
                "duration_s": duration_s,
                "log": str(log_path) if log_path else None,
                "failure_reason": fail_reason,
                "decisions": decisions,
            })

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


def extract_seed_results(data: dict, scenario: Optional[str] = None) -> Dict[int, bool]:
    """Extract a mapping of seed -> passed (bool) from single or multi-scenario regression JSON."""
    res: Dict[int, bool] = {}

    # 1. Multi-scenario structure: {"scenarios": [{"scenario": "...", "smoke": "...", "results": [...]}, ...]}
    if "scenarios" in data and isinstance(data["scenarios"], list):
        for sc_data in data["scenarios"]:
            sc_name = sc_data.get("scenario")
            sm_name = sc_data.get("smoke")
            if scenario:
                target_smoke = resolve_smoke_command(scenario)
                if sc_name != scenario and sm_name != target_smoke and sm_name != scenario:
                    continue
            for r in sc_data.get("results", []):
                if "seed" in r and "passed" in r:
                    res[int(r["seed"])] = bool(r["passed"])
        if res:
            return res

    # 2. Single scenario results list: {"results": [{"seed": 1, "passed": True}, ...]}
    if "results" in data and isinstance(data["results"], list):
        for r in data["results"]:
            if "seed" in r and "passed" in r:
                res[int(r["seed"])] = bool(r["passed"])
        if res:
            return res

    # 3. Direct/legacy seed mapping: {"seeds": {1: True, 2: False}}
    if "seeds" in data and isinstance(data["seeds"], dict):
        return {int(k): bool(v) for k, v in data["seeds"].items()}

    return res


def compare_with_yesterday(today_data: dict, yesterday_data: dict, scenario: Optional[str] = None) -> dict:
    """Compare today's sweep with yesterday's to find regressions and flaky seeds (Review 091 Point 3)."""
    t_results = extract_seed_results(today_data, scenario)
    y_results = extract_seed_results(yesterday_data, scenario)

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
        "regressions": sorted(regressions),
        "fixes": sorted(fixes),
        "flaky_seeds": sorted(flaky),
        "stable_seeds": sorted(stable_pass),
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

    if args.scenario == "all" or args.night or "," in args.scenario:
        if args.scenario == "all" or (args.night and args.scenario == "crossroads"):
            scenarios = list(SCENARIO_MAP.keys())
        elif "," in args.scenario:
            scenarios = [s.strip() for s in args.scenario.split(",") if s.strip()]
        else:
            scenarios = [args.scenario]

        all_results = []
        for sc in scenarios:
            res = run_scenario_regression(sc, seeds, parallel=args.parallel, remote=args.remote, mock=args.mock)
            all_results.append(res)

        comparison = None
        if args.compare_yesterday and os.path.isfile(args.compare_yesterday):
            try:
                yest = json.loads(Path(args.compare_yesterday).read_text(encoding="utf-8-sig"))
                comparison = compare_with_yesterday({"scenarios": all_results}, yest)
            except Exception as e:
                print(f"[scenario_regression] Warning: failed to compare with yesterday: {e}", file=sys.stderr)

        md_output = format_night_report(all_results, comparison)
        json_output = {"date": dt.date.today().isoformat(), "scenarios": all_results, "comparison": comparison}
    else:
        res = run_scenario_regression(args.scenario, seeds, parallel=args.parallel, remote=args.remote, mock=args.mock)
        comparison = None
        if args.compare_yesterday and os.path.isfile(args.compare_yesterday):
            try:
                yest = json.loads(Path(args.compare_yesterday).read_text(encoding="utf-8-sig"))
                comparison = compare_with_yesterday(res, yest, scenario=args.scenario)
            except Exception as e:
                print(f"[scenario_regression] Warning: failed to compare with yesterday: {e}", file=sys.stderr)

        md_output = format_day_digest(res)
        if comparison:
            reg = comparison.get("regressions") or []
            fix = comparison.get("fixes") or []
            flaky = comparison.get("flaky_seeds") or []
            if reg or fix or flaky:
                md_output += f"\n\n### Yesterday Comparison:"
                md_output += f"\n- **Regressions**: {', '.join(str(s) for s in reg) if reg else 'None'}"
                md_output += f"\n- **Fixes**: {', '.join(str(s) for s in fix) if fix else 'None'}"
                md_output += f"\n- **Flaky**: {', '.join(str(s) for s in flaky) if flaky else 'None'}"

        json_output = dict(res, comparison=comparison)

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
