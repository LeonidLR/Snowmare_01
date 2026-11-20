"""Paired A/B digest for CodexTactics: honest numbers for enemy-AI changes.

Compares two result folders (e.g. main vs a branch, same seeds) and prints per scenario:
  - Defender wins A / B of N and win rates
  - Attacker objective captures
  - Attackers killed per round
  - Share of rounds with >= 2 attackers down
  - Paired discordant counts (A-only wins / B-only wins) + exact two-sided McNemar / sign-test p-value
  - Medians of on_pos / idle_max / crossfire with 95% bootstrap confidence intervals of the median and of the difference (B - A)
  - The seed set of each side (and a loud WARNING when the seed sets differ)
  - Optional --tune-seeds 1-10 split that prints tune vs held-out subsets separately.

Standard library only (no numpy / scipy).

Usage:
    python Scripts/Tools/paired_ab.py Saved/Logs/Worker/main Saved/Logs/Worker/branch
    python Scripts/Tools/paired_ab.py folderA folderB --tune-seeds 1-10
    python Scripts/Tools/paired_ab.py folderA folderB --rng-seed 42 --resamples 2000 --json out.json
"""
from __future__ import annotations

import argparse
import json
import math
import random
import statistics
import sys
from pathlib import Path
from typing import Any, Iterable

# Re-use parser from dayloop_digest (no copy-pasted parser)
try:
    from dayloop_digest import defender_side, load_folder, med, row
except ImportError:
    from Scripts.Tools.dayloop_digest import defender_side, load_folder, med, row


def exact_mcnemar_p(b: int, c: int) -> float:
    """Exact two-sided McNemar / sign-test p-value for discordant counts b and c.

    b: A-only wins (A won, B lost/drew)
    c: B-only wins (B won, A lost/drew)
    Under H0 (p=0.5), min(b, c) follows Binomial(n=b+c, p=0.5).
    """
    n = b + c
    if n == 0:
        return 1.0
    k = min(b, c)
    tail_prob = sum(math.comb(n, i) for i in range(k + 1)) / (2.0 ** n)
    return min(1.0, 2.0 * tail_prob)


def parse_seed_spec(spec: str) -> set[int]:
    """Parse seed specification string, e.g. '1-10', '1-5,7,9-12', '11,22,33'."""
    res = set()
    parts = [p.strip() for p in spec.split(",") if p.strip()]
    for part in parts:
        if "-" in part:
            lo_str, hi_str = part.split("-", 1)
            res.update(range(int(lo_str.strip()), int(hi_str.strip()) + 1))
        else:
            res.add(int(part))
    return res


def format_seed_ranges(seeds: Iterable[int]) -> str:
    """Format a set or sequence of integer seeds into compact comma-separated ranges."""
    sorted_seeds = sorted(seeds)
    if not sorted_seeds:
        return "(empty)"
    ranges = []
    start = sorted_seeds[0]
    prev = sorted_seeds[0]
    for s in sorted_seeds[1:]:
        if s == prev + 1:
            prev = s
        else:
            if start == prev:
                ranges.append(str(start))
            else:
                ranges.append(f"{start}-{prev}")
            start = s
            prev = s
    if start == prev:
        ranges.append(str(start))
    else:
        ranges.append(f"{start}-{prev}")
    return ", ".join(ranges)


def percentile(sorted_vals: list[float], q: float) -> float:
    """Compute percentile q in [0.0, 1.0] from a pre-sorted float list using linear interpolation."""
    if not sorted_vals:
        return 0.0
    if len(sorted_vals) == 1:
        return sorted_vals[0]
    n = len(sorted_vals)
    pos = q * (n - 1)
    low_idx = int(pos)
    high_idx = min(low_idx + 1, n - 1)
    weight = pos - low_idx
    return sorted_vals[low_idx] * (1.0 - weight) + sorted_vals[high_idx] * weight


def bootstrap_paired_medians(
    pairs: list[tuple[dict, dict]],
    keys: list[str],
    n_resamples: int = 2000,
    rng_seed: int = 42,
) -> dict[str, dict[str, Any]]:
    """Compute point estimates and 95% bootstrap confidence intervals for medians and difference.

    For each metric key:
      - Point estimates: median(A), median(B), diff = median(B) - median(A)
      - Bootstrap: resamples pairs with replacement to maintain seed correlation.
      - 95% CI: 2.5th and 97.5th percentiles of resampled estimates.
    """
    n = len(pairs)
    rng = random.Random(rng_seed)
    # Pre-generate sample index lists for strict determinism
    resample_indices = [[rng.randrange(n) for _ in range(n)] for _ in range(n_resamples)] if n > 0 else []

    out: dict[str, dict[str, Any]] = {}
    for key in keys:
        vals_a = [p[0][key] for p in pairs if isinstance(p[0].get(key), (int, float))]
        vals_b = [p[1][key] for p in pairs if isinstance(p[1].get(key), (int, float))]

        med_a = round(statistics.median(vals_a), 3) if vals_a else None
        med_b = round(statistics.median(vals_b), 3) if vals_b else None
        diff_point = round(med_b - med_a, 3) if (med_a is not None and med_b is not None) else None

        boot_med_a: list[float] = []
        boot_med_b: list[float] = []
        boot_diff: list[float] = []

        if n > 0 and vals_a and vals_b:
            for idx_list in resample_indices:
                sample_a = [pairs[i][0][key] for i in idx_list if isinstance(pairs[i][0].get(key), (int, float))]
                sample_b = [pairs[i][1][key] for i in idx_list if isinstance(pairs[i][1].get(key), (int, float))]

                if sample_a and sample_b:
                    ma = statistics.median(sample_a)
                    mb = statistics.median(sample_b)
                    boot_med_a.append(ma)
                    boot_med_b.append(mb)
                    boot_diff.append(mb - ma)
                elif sample_a:
                    boot_med_a.append(statistics.median(sample_a))
                elif sample_b:
                    boot_med_b.append(statistics.median(sample_b))

        ci_a = (
            round(percentile(sorted(boot_med_a), 0.025), 3),
            round(percentile(sorted(boot_med_a), 0.975), 3),
        ) if boot_med_a else None

        ci_b = (
            round(percentile(sorted(boot_med_b), 0.025), 3),
            round(percentile(sorted(boot_med_b), 0.975), 3),
        ) if boot_med_b else None

        ci_diff = (
            round(percentile(sorted(boot_diff), 0.025), 3),
            round(percentile(sorted(boot_diff), 0.975), 3),
        ) if boot_diff else None

        out[key] = {
            "a": med_a,
            "a_ci": ci_a,
            "b": med_b,
            "b_ci": ci_b,
            "diff": diff_point,
            "diff_ci": ci_diff,
        }
    return out


def evaluate_partition(
    pairs: list[tuple[dict, dict]],
    keys: list[str],
    n_resamples: int = 2000,
    rng_seed: int = 42,
) -> dict[str, Any]:
    """Compute all paired comparison metrics on a list of (row_A, row_B) pairs."""
    n = len(pairs)
    if n == 0:
        return {"n": 0}

    # Defender wins
    a_def_wins = sum(1 for p in pairs if p[0].get("def_win"))
    b_def_wins = sum(1 for p in pairs if p[1].get("def_win"))
    a_win_rate = round(a_def_wins / n, 4)
    b_win_rate = round(b_def_wins / n, 4)

    # Discordant counts (A-only vs B-only)
    a_only_wins = sum(1 for p in pairs if p[0].get("def_win") and not p[1].get("def_win"))
    b_only_wins = sum(1 for p in pairs if not p[0].get("def_win") and p[1].get("def_win"))
    both_wins = sum(1 for p in pairs if p[0].get("def_win") and p[1].get("def_win"))
    neither_wins = sum(1 for p in pairs if not p[0].get("def_win") and not p[1].get("def_win"))
    mcnemar_p = exact_mcnemar_p(a_only_wins, b_only_wins)

    # Attacker objective captures
    a_captures = sum(1 for p in pairs if p[0].get("atk_capture"))
    b_captures = sum(1 for p in pairs if p[1].get("atk_capture"))
    a_capture_rate = round(a_captures / n, 4)
    b_capture_rate = round(b_captures / n, 4)

    # Attackers killed per round
    a_kills = [p[0]["att_killed"] for p in pairs if isinstance(p[0].get("att_killed"), (int, float))]
    b_kills = [p[1]["att_killed"] for p in pairs if isinstance(p[1].get("att_killed"), (int, float))]
    a_kills_mean = round(sum(a_kills) / len(a_kills), 3) if a_kills else 0.0
    b_kills_mean = round(sum(b_kills) / len(b_kills), 3) if b_kills else 0.0
    diff_kills_mean = round(b_kills_mean - a_kills_mean, 3)

    # Share of rounds with >= 2 attackers down
    a_down_ge2 = sum(1 for p in pairs if p[0].get("att_down_ge2"))
    b_down_ge2 = sum(1 for p in pairs if p[1].get("att_down_ge2"))
    a_down_ge2_share = round(a_down_ge2 / n, 4)
    b_down_ge2_share = round(b_down_ge2 / n, 4)
    diff_down_ge2_share = round(b_down_ge2_share - a_down_ge2_share, 4)

    # Behaviour medians + bootstrap CIs
    medians = bootstrap_paired_medians(pairs, keys, n_resamples=n_resamples, rng_seed=rng_seed)

    return {
        "n": n,
        "def_wins": {
            "a": a_def_wins,
            "a_rate": a_win_rate,
            "b": b_def_wins,
            "b_rate": b_win_rate,
            "diff_rate": round(b_win_rate - a_win_rate, 4),
        },
        "discordant": {
            "a_only": a_only_wins,
            "b_only": b_only_wins,
            "both": both_wins,
            "neither": neither_wins,
            "p": mcnemar_p,
        },
        "atk_captures": {
            "a": a_captures,
            "a_rate": a_capture_rate,
            "b": b_captures,
            "b_rate": b_capture_rate,
            "diff_count": b_captures - a_captures,
            "diff_rate": round(b_capture_rate - a_capture_rate, 4),
        },
        "att_killed_per_round": {
            "a_mean": a_kills_mean,
            "b_mean": b_kills_mean,
            "diff": diff_kills_mean,
        },
        "att_down_ge2": {
            "a_count": a_down_ge2,
            "a_share": a_down_ge2_share,
            "b_count": b_down_ge2,
            "b_share": b_down_ge2_share,
            "diff_share": diff_down_ge2_share,
        },
        "medians": medians,
    }


def compare_paired_runs(
    runs_a: list[dict],
    runs_b: list[dict],
    tune_seeds: set[int] | None = None,
    rng_seed: int = 42,
    n_resamples: int = 2000,
) -> dict[str, Any]:
    """Perform full paired comparison grouped by scenario, handling seed set differences."""
    # Group runs by scenario and seed
    scenarios: set[str] = set()
    by_scen_a: dict[str, dict[Any, dict]] = {}
    by_scen_b: dict[str, dict[Any, dict]] = {}

    for r in runs_a:
        sc = r.get("scenario") or "default"
        scenarios.add(sc)
        by_scen_a.setdefault(sc, {})[r["seed"]] = r

    for r in runs_b:
        sc = r.get("scenario") or "default"
        scenarios.add(sc)
        by_scen_b.setdefault(sc, {})[r["seed"]] = r

    keys = ["on_pos", "idle_max", "crossfire", "gap", "exposed_m", "spread"]
    scenarios_result: dict[str, Any] = {}
    global_warnings: list[str] = []

    for sc in sorted(scenarios):
        dict_a = by_scen_a.get(sc, {})
        dict_b = by_scen_b.get(sc, {})

        seeds_a = set(dict_a.keys())
        seeds_b = set(dict_b.keys())
        common_seeds = seeds_a & seeds_b

        warning: str | None = None
        if seeds_a != seeds_b:
            missing_in_b = seeds_a - seeds_b
            missing_in_a = seeds_b - seeds_a
            warning = (
                f"WARNING: Seed sets differ for scenario '{sc}'!\n"
                f"  Folder A seeds ({len(seeds_a)}): {format_seed_ranges(seeds_a)}\n"
                f"  Folder B seeds ({len(seeds_b)}): {format_seed_ranges(seeds_b)}\n"
                f"  Common paired seeds ({len(common_seeds)}): {format_seed_ranges(common_seeds)}\n"
                f"  Missing in B ({len(missing_in_b)}): {format_seed_ranges(missing_in_b)}\n"
                f"  Missing in A ({len(missing_in_a)}): {format_seed_ranges(missing_in_a)}"
            )
            global_warnings.append(warning)

        all_pairs = [(dict_a[s], dict_b[s]) for s in sorted(common_seeds)]

        partitions: dict[str, Any] = {}
        partitions["all"] = evaluate_partition(all_pairs, keys, n_resamples=n_resamples, rng_seed=rng_seed)

        if tune_seeds is not None:
            tune_pairs = [(dict_a[s], dict_b[s]) for s in sorted(common_seeds) if s in tune_seeds]
            held_pairs = [(dict_a[s], dict_b[s]) for s in sorted(common_seeds) if s not in tune_seeds]

            partitions["tune"] = evaluate_partition(tune_pairs, keys, n_resamples=n_resamples, rng_seed=rng_seed)
            partitions["held_out"] = evaluate_partition(held_pairs, keys, n_resamples=n_resamples, rng_seed=rng_seed)

        scenarios_result[sc] = {
            "seeds_a": sorted(seeds_a),
            "seeds_b": sorted(seeds_b),
            "common_seeds": sorted(common_seeds),
            "seed_warning": warning,
            "partitions": partitions,
        }

    return {
        "scenarios": scenarios_result,
        "warnings": global_warnings,
        "tune_seeds": sorted(tune_seeds) if tune_seeds is not None else None,
        "rng_seed": rng_seed,
        "n_resamples": n_resamples,
    }


def format_partition_table(name: str, p: dict[str, Any], label_a: str, label_b: str) -> str:
    """Format one partition's outcome and behaviour medians table."""
    n = p["n"]
    if n == 0:
        return f"\n=== {name.upper()} (N=0) ===\n(no runs in this partition)\n"

    dw = p["def_wins"]
    disc = p["discordant"]
    cap = p["atk_captures"]
    kills = p["att_killed_per_round"]
    down = p["att_down_ge2"]
    meds = p["medians"]

    lines = []
    lines.append(f"\n--- {name.upper()} (N={n} paired seeds) ---")

    # Outcomes
    lines.append("Outcome Metrics:")
    hdr = f"  {'Metric':<26} {' ' + label_a:<22} {' ' + label_b:<22} {'Diff (B - A) / Discordant'}"
    lines.append(hdr)
    lines.append("  " + "-" * 26 + "  " + "-" * 22 + "  " + "-" * 22 + "  " + "-" * 32)

    # Defender wins
    a_dw_str = f"{dw['a']} / {n} ({dw['a_rate']*100:.1f}%)"
    b_dw_str = f"{dw['b']} / {n} ({dw['b_rate']*100:.1f}%)"
    p_val_str = f"{disc['p']:.4e}" if disc['p'] < 0.001 else f"{disc['p']:.4f}"
    diff_dw_str = f"A-only {disc['a_only']} / B-only {disc['b_only']} (McNemar p={p_val_str})"
    lines.append(f"  {'Defender wins':<26} {a_dw_str:<22} {b_dw_str:<22} {diff_dw_str}")

    # Attacker captures
    a_cap_str = f"{cap['a']} / {n} ({cap['a_rate']*100:.1f}%)"
    b_cap_str = f"{cap['b']} / {n} ({cap['b_rate']*100:.1f}%)"
    diff_cap_sign = "+" if cap['diff_count'] > 0 else ""
    diff_cap_str = f"{diff_cap_sign}{cap['diff_count']} ({cap['diff_rate']*100:+.1f}%)"
    lines.append(f"  {'Attacker captures':<26} {a_cap_str:<22} {b_cap_str:<22} {diff_cap_str}")

    # Attackers killed per round
    a_k_str = f"{kills['a_mean']:.2f}"
    b_k_str = f"{kills['b_mean']:.2f}"
    diff_k_str = f"{kills['diff']:+.2f}"
    lines.append(f"  {'Attackers killed / round':<26} {a_k_str:<22} {b_k_str:<22} {diff_k_str}")

    # Share >= 2 attackers down
    a_down_str = f"{down['a_share']:.3f} ({down['a_count']}/{n})"
    b_down_str = f"{down['b_share']:.3f} ({down['b_count']}/{n})"
    diff_down_str = f"{down['diff_share']:+.3f} ({down['diff_share']*100:+.1f}%)"
    lines.append(f"  {'Share >= 2 attackers down':<26} {a_down_str:<22} {b_down_str:<22} {diff_down_str}")

    # Behaviour Medians
    lines.append("")
    lines.append("T-04 Behaviour Medians (Bootstrap 95% CI):")
    hdr_m = f"  {'Metric':<18} {label_a + ' Median [95% CI]':<26} {label_b + ' Median [95% CI]':<26} {'Diff (B - A) [95% CI]'}"
    lines.append(hdr_m)
    lines.append("  " + "-" * 18 + "  " + "-" * 26 + "  " + "-" * 26 + "  " + "-" * 26)

    target_keys = ["on_pos", "idle_max", "crossfire", "gap", "exposed_m", "spread"]
    for k in target_keys:
        m = meds.get(k)
        if not m or m["a"] is None:
            continue
        ci_a_str = f"[{m['a_ci'][0]:.2f}, {m['a_ci'][1]:.2f}]" if m['a_ci'] else "[-]"
        ci_b_str = f"[{m['b_ci'][0]:.2f}, {m['b_ci'][1]:.2f}]" if m['b_ci'] else "[-]"
        ci_d_str = f"[{m['diff_ci'][0]:.2f}, {m['diff_ci'][1]:.2f}]" if m['diff_ci'] else "[-]"

        a_col = f"{m['a']:.2f} {ci_a_str}"
        b_col = f"{m['b']:.2f} {ci_b_str}" if m['b'] is not None else "- [-]"
        d_col = f"{m['diff']:+.2f} {ci_d_str}" if m['diff'] is not None else "- [-]"
        lines.append(f"  {k:<18} {a_col:<26} {b_col:<26} {d_col}")

    return "\n".join(lines)


def format_report(result: dict[str, Any], label_a: str = "A (baseline)", label_b: str = "B (branch)") -> str:
    """Format full human-readable report."""
    output = []
    output.append("=" * 80)
    output.append("PAIRED A/B DIGEST")
    output.append("=" * 80)

    for warning in result.get("warnings", []):
        output.append("\n" + "!" * 80)
        output.append(warning)
        output.append("!" * 80)

    for sc_name, sc_data in result.get("scenarios", {}).items():
        output.append(f"\n================================================================================")
        output.append(f"SCENARIO: {sc_name}")
        output.append(f"  Seeds A ({len(sc_data['seeds_a'])}): {format_seed_ranges(sc_data['seeds_a'])}")
        output.append(f"  Seeds B ({len(sc_data['seeds_b'])}): {format_seed_ranges(sc_data['seeds_b'])}")
        output.append(f"  Common paired seeds ({len(sc_data['common_seeds'])}): {format_seed_ranges(sc_data['common_seeds'])}")
        output.append(f"================================================================================")

        parts = sc_data["partitions"]
        if "tune" in parts and "held_out" in parts:
            output.append(format_partition_table("Tune Set", parts["tune"], label_a, label_b))
            output.append(format_partition_table("Held-Out Set", parts["held_out"], label_a, label_b))
            output.append(format_partition_table("All Combined Seeds", parts["all"], label_a, label_b))
        else:
            output.append(format_partition_table("All Paired Seeds", parts["all"], label_a, label_b))

    return "\n".join(output)


def compare_folders_cli(
    folder_a: str | Path,
    folder_b: str | Path,
    tune_seeds: str | None = None,
    rng_seed: int = 42,
    n_resamples: int = 2000,
    json_out: str | None = None,
) -> int:
    """High-level comparison function for CLI invocations."""
    pa = Path(folder_a)
    pb = Path(folder_b)

    rows_a = load_folder(pa)
    rows_b = load_folder(pb)

    if not rows_a:
        print(f"Error: No result JSON files found in {pa}", file=sys.stderr)
        return 1
    if not rows_b:
        print(f"Error: No result JSON files found in {pb}", file=sys.stderr)
        return 1

    parsed_tune = parse_seed_spec(tune_seeds) if tune_seeds else None
    result = compare_paired_runs(
        rows_a,
        rows_b,
        tune_seeds=parsed_tune,
        rng_seed=rng_seed,
        n_resamples=n_resamples,
    )

    label_a = f"A ({pa.name})"
    label_b = f"B ({pb.name})"
    report_text = format_report(result, label_a=label_a, label_b=label_b)
    print(report_text)

    if json_out:
        Path(json_out).write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(f"\nWrote JSON comparison to {json_out}")

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Paired A/B digest comparing two result folders across seeded runs.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""Examples:
  python Scripts/Tools/paired_ab.py Saved/Logs/Worker/main Saved/Logs/Worker/branch
  python Scripts/Tools/paired_ab.py folderA folderB --tune-seeds 1-10
  python Scripts/Tools/paired_ab.py folderA folderB --rng-seed 42 --resamples 2000 --json out.json
""",
    )
    parser.add_argument("folder_a", help="Path to folder A (baseline / main)")
    parser.add_argument("folder_b", help="Path to folder B (candidate / branch)")
    parser.add_argument("--tune-seeds", help="Seed spec for tuning set (e.g. '1-10' or '1-10,25'); reports tune vs held-out")
    parser.add_argument("--rng-seed", type=int, default=42, help="RNG seed for bootstrap resampling (default: 42)")
    parser.add_argument("--resamples", type=int, default=2000, help="Number of bootstrap resamples (default: 2000)")
    parser.add_argument("--json", help="Path to output JSON summary file")

    args = parser.parse_args()
    return compare_folders_cli(
        args.folder_a,
        args.folder_b,
        tune_seeds=args.tune_seeds,
        rng_seed=args.rng_seed,
        n_resamples=args.resamples,
        json_out=args.json,
    )


if __name__ == "__main__":
    sys.exit(main())
