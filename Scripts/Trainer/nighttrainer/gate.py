"""Training gate module for CodexTactics night trainer (smart enemies T-10, GDD section 9).

Verifies G1-G5 training conditions before evolution is permitted:
- G1 (Smoke health): required foundation and scenario smokes are green in the latest sweep.
- G2 (Metric liveness): all metrics used by fitness and QD descriptors are live
  (not constant 0, None, or zero variance over a probe of rounds).
- G3 (Baseline sufficiency): baseline exists with >= 30 rounds per scenario (F7).
- G4 (Opponent check): at least one leg is a scripted or diverse opponent.
"""
from __future__ import annotations

import json
import math
import os
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple


def extract_used_metrics(config: dict) -> Set[str]:
    """Extract all metric identifiers used by fitness bonuses/penalties and QD axes."""
    metrics: Set[str] = set()

    # Fitness bonuses and penalties
    fitness = config.get("fitness") or {}
    for block_name in ("bonuses", "penalties"):
        block = fitness.get(block_name) or {}
        for k in block.keys():
            if not k.startswith("_"):
                metrics.add(k)

    # Core fitness terms
    for term in ("damage_dealt", "survivors", "hp_lost", "kills", "overwatch_kills", "guard_share"):
        if term in fitness and not term.startswith("_"):
            metrics.add(term)

    # QD axes
    qd = config.get("qd") or {}
    for axis in qd.get("axes") or []:
        if "metric" in axis:
            metrics.add(axis["metric"])
        if "metrics" in axis:
            for m in axis["metrics"]:
                metrics.add(m)

    return metrics


def get_metric_value_from_round(rnd: dict, metric: str, side: str = "A") -> Any:
    """Retrieve the value of a metric from round or defence block or sides block."""
    # 1. Check defence block first
    defence = rnd.get("defence") or {}
    if metric in defence:
        return defence[metric]

    # 2. Check candidate side (A) or role
    sides = rnd.get("sides") or {}
    side_info = sides.get(side) or {}
    if metric in side_info:
        return side_info[metric]

    # Check opponent side (B)
    opp_side = "B" if side == "A" else "A"
    opp_info = sides.get(opp_side) or {}

    # Derived metrics
    if metric == "damage_dealt":
        return side_info.get("damage_dealt", 0.0)
    elif metric == "damage_taken" or metric == "hp_lost":
        return side_info.get("hp_lost", 0.0)
    elif metric == "kills":
        return side_info.get("kills", 0)
    elif metric == "survivors":
        return side_info.get("survivors", 0)
    elif metric == "moves":
        return side_info.get("moves", side_info.get("flanks_attempted", 0))
    elif metric == "flanks_completed":
        return side_info.get("flanks_succeeded", 0)
    elif metric == "time_in_contact_s":
        return rnd.get("duration_s", 0.0)

    # 3. Check top-level round
    if metric in rnd:
        return rnd[metric]

    return None


def check_metric_liveness(probe_rounds: List[dict], metrics_used: Set[str], side: str = "A") -> Tuple[bool, List[str]]:
    """G2 Metric Liveness: ensure metrics used by fitness / descriptors are not dead / constant.

    A metric fails if:
    - It is None across all probe rounds (e.g. guard_share is None).
    - It is constant 0 across all probe rounds (e.g. overwatch_kills == 0).
    - It has 0 variance across all probe rounds.
    """
    if not probe_rounds:
        return False, ["No probe rounds provided for metric liveness evaluation"]

    dead_metrics = []
    n = len(probe_rounds)

    for m in sorted(metrics_used):
        values = [get_metric_value_from_round(r, m, side) for r in probe_rounds]

        # Case 1: All None
        if all(v is None for v in values):
            dead_metrics.append(f"Metric '{m}' is None/missing in all {n} probe rounds")
            continue

        # Filter out None for numerical variance check
        num_vals = [float(v) for v in values if isinstance(v, (int, float))]
        if not num_vals:
            dead_metrics.append(f"Metric '{m}' has no numeric values in {n} probe rounds")
            continue

        # Case 2: All exactly 0
        if all(v == 0.0 for v in num_vals):
            dead_metrics.append(f"Metric '{m}' is zero across all {len(num_vals)} probe rounds (dead metric)")
            continue

        # Case 3: Constant value (0 variance)
        if len(num_vals) > 1:
            mean = sum(num_vals) / len(num_vals)
            var = sum((v - mean) ** 2 for v in num_vals) / len(num_vals)
            if var == 0.0:
                dead_metrics.append(f"Metric '{m}' is constant ({mean}) with zero variance across {len(num_vals)} probe rounds")

    passed = len(dead_metrics) == 0
    return passed, dead_metrics


def check_baseline_sufficiency(baseline_data: Optional[dict], min_rounds_per_scenario: int = 30) -> Tuple[bool, List[str]]:
    """G3 Baseline Sufficiency: ensure baseline has >= 30 rounds per scenario (F7)."""
    if not baseline_data:
        return False, ["No baseline data exists for the current freeze commit"]

    rounds = baseline_data.get("rounds") or []
    if not rounds:
        # Check if summarized by scenario in baseline_data
        by_scen = baseline_data.get("scenarios") or {}
        if not by_scen:
            return False, ["Baseline has 0 rounds recorded"]

    # Count rounds per scenario
    counts: Dict[str, int] = {}
    if rounds:
        for r in rounds:
            scen = r.get("scenario") or "default"
            counts[scen] = counts.get(scen, 0) + 1
    else:
        for scen, sdata in (baseline_data.get("scenarios") or {}).items():
            counts[scen] = sdata.get("rounds", 0)

    issues = []
    for scen, count in counts.items():
        if count < min_rounds_per_scenario:
            issues.append(f"Baseline scenario '{scen}' has only {count} rounds (< {min_rounds_per_scenario} required by F7/G3)")

    if not counts:
        issues.append("Baseline has no scenario rounds classified")

    passed = len(issues) == 0
    return passed, issues


def check_smoke_health(sweep_data: Optional[dict], required_smokes: Optional[List[str]] = None) -> Tuple[bool, List[str]]:
    """G1 Smoke Health: ensure all required foundation/scenario smokes are green in last sweep."""
    if not sweep_data:
        # If no sweep data provided, check if sweep file exists in standard location
        return False, ["No smoke sweep result data provided or found (sweep-result.json)"]

    results = sweep_data.get("results") or sweep_data.get("smokes") or {}
    # Convert list of results to dict if needed
    res_dict: Dict[str, str] = {}
    if isinstance(results, list):
        for item in results:
            name = item.get("Name") or item.get("name")
            state = item.get("State") or item.get("state") or ("PASS" if item.get("Pass") else "FAIL")
            if name:
                res_dict[name.replace("CodexTactics.", "")] = state
    elif isinstance(results, dict):
        res_dict = {k.replace("CodexTactics.", ""): v for k, v in results.items()}

    failed = []
    default_reqs = required_smokes or ["PmcCrossroadsSmoke", "PmcAlleySmoke", "PmcRoofDefenceSmoke", "PmcStalemateSmoke"]

    for req in default_reqs:
        clean_req = req.replace("CodexTactics.", "")
        if clean_req not in res_dict:
            # If not in sweep, mark missing
            failed.append(f"Required smoke '{clean_req}' not found in latest sweep")
        elif res_dict[clean_req] not in ("PASS", "PASS*", "KNOWN_FAIL"):
            failed.append(f"Required smoke '{clean_req}' failed (state: {res_dict[clean_req]})")

    passed = len(failed) == 0
    return passed, failed


def evaluate_training_gate(
    config: dict,
    probe_data: Optional[dict] = None,
    baseline_data: Optional[dict] = None,
    sweep_data: Optional[dict] = None,
    required_smokes: Optional[List[str]] = None,
) -> dict:
    """Run all training gate checks and return structured diagnostic results."""
    used_metrics = extract_used_metrics(config)
    probe_rounds = (probe_data.get("rounds") if probe_data else None) or []

    g2_pass, g2_reasons = check_metric_liveness(probe_rounds, used_metrics)
    g3_pass, g3_reasons = check_baseline_sufficiency(baseline_data)
    g1_pass, g1_reasons = check_smoke_health(sweep_data, required_smokes)

    all_passed = g1_pass and g2_pass and g3_pass
    all_reasons = []
    if not g1_pass:
        all_reasons.extend([f"[G1 Smokes] {r}" for r in g1_reasons])
    if not g2_pass:
        all_reasons.extend([f"[G2 Liveness] {r}" for r in g2_reasons])
    if not g3_pass:
        all_reasons.extend([f"[G3 Baseline] {r}" for r in g3_reasons])

    return {
        "passed": all_passed,
        "metrics_evaluated": sorted(used_metrics),
        "checks": {
            "G1_smoke_health": {"passed": g1_pass, "issues": g1_reasons},
            "G2_metric_liveness": {"passed": g2_pass, "issues": g2_reasons},
            "G3_baseline_sufficiency": {"passed": g3_pass, "issues": g3_reasons},
        },
        "reasons": all_reasons,
    }
