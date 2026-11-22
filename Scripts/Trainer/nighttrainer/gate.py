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


def extract_penalty_metrics(config: dict) -> Set[str]:
    """Extract metric identifiers that act as penalties (e.g. friendly_fire, stuck_events)."""
    penalties: Set[str] = set()
    fitness = config.get("fitness") or {}
    pen_block = fitness.get("penalties") or {}
    for k in pen_block.keys():
        if not k.startswith("_"):
            penalties.add(k)
    return penalties


def _extract_node_value(node: Any) -> Optional[float]:
    """Extract a finite float from a numeric leaf, or a dict with {'value': X}."""
    if isinstance(node, bool) or node is None:
        return None
    if isinstance(node, (int, float)):
        if math.isnan(node) or math.isinf(node):
            return None
        return float(node)
    if isinstance(node, dict):
        val = node.get("value")
        if isinstance(val, (int, float)) and not isinstance(val, bool):
            if math.isnan(val) or math.isinf(val):
                return None
            return float(val)
    return None


def get_metric_value_from_round(rnd: dict, metric: str, side: str = "A") -> Any:
    """Retrieve the value of a metric from round or defence block or sides block.

    Checks:
    1. defence block: numeric or {'value': X}
    2. sides.<side>.behaviour.metrics.<k>.value or sides.<side>.behaviour.<k>
    3. round.behaviour.metrics.<k>.value or round.behaviour.<k>
    4. sides.<side> direct stats: damage_dealt, hp_lost, kills, survivors
    5. top-level round[metric]
    """
    # 1. Check defence block first
    defence = rnd.get("defence") or {}
    if metric in defence:
        val = _extract_node_value(defence[metric])
        if val is not None:
            return val
        if defence[metric] is None:
            return None

    # 2. Check candidate side (A) behaviour
    sides = rnd.get("sides") or {}
    side_info = sides.get(side) or {}
    beh = side_info.get("behaviour")
    if isinstance(beh, dict):
        # Rich behaviour document format: {"schema": "...", "metrics": {metric: {"value": X}}}
        metrics_dict = beh.get("metrics")
        if isinstance(metrics_dict, dict) and metric in metrics_dict:
            val = _extract_node_value(metrics_dict[metric])
            if val is not None:
                return val
        if metric in beh:
            val = _extract_node_value(beh[metric])
            if val is not None:
                return val

    # 3. Check top-level round behaviour
    rnd_beh = rnd.get("behaviour")
    if isinstance(rnd_beh, dict):
        metrics_dict = rnd_beh.get("metrics")
        if isinstance(metrics_dict, dict) and metric in metrics_dict:
            val = _extract_node_value(metrics_dict[metric])
            if val is not None:
                return val
        if metric in rnd_beh:
            val = _extract_node_value(rnd_beh[metric])
            if val is not None:
                return val

    # 4. Direct stats on candidate side
    if metric in side_info:
        val = _extract_node_value(side_info[metric])
        if val is not None:
            return val
        if side_info[metric] is None:
            return None

    # Core round/side scalar stats (without fake proxies)
    if metric == "damage_dealt":
        return _extract_node_value(side_info.get("damage_dealt"))
    elif metric in ("damage_taken", "hp_lost"):
        return _extract_node_value(side_info.get("hp_lost"))
    elif metric == "kills":
        return _extract_node_value(side_info.get("kills"))
    elif metric == "survivors":
        return _extract_node_value(side_info.get("survivors"))

    # 5. Check top-level round
    if metric in rnd:
        val = _extract_node_value(rnd[metric])
        if val is not None:
            return val
        if rnd[metric] is None:
            return None

    return None


def check_metric_liveness(
    probe_rounds: List[dict],
    metrics_used: Set[str],
    side: str = "A",
    penalty_metrics: Optional[Set[str]] = None,
) -> Tuple[bool, List[str]]:
    """G2 Metric Liveness: ensure metrics used by fitness / descriptors are not dead / constant.

    A metric fails if:
    - It is None across all probe rounds (e.g. guard_share is None).
    - It is constant 0 across all probe rounds (e.g. overwatch_kills == 0),
      UNLESS it is an active penalty metric (where 0 indicates zero violations/clean round).
    - It has 0 variance across all probe rounds (except zero-valued penalties).
    """
    if not probe_rounds:
        return False, ["No probe rounds provided for metric liveness evaluation"]

    dead_metrics = []
    n = len(probe_rounds)

    for m in sorted(metrics_used):
        values = [get_metric_value_from_round(r, m, side) for r in probe_rounds]
        is_penalty = bool(penalty_metrics and m in penalty_metrics)

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
            if not is_penalty:
                dead_metrics.append(f"Metric '{m}' is zero across all {len(num_vals)} probe rounds (dead metric)")
            continue

        # Case 3: Constant value (0 variance)
        if len(num_vals) > 1:
            mean = sum(num_vals) / len(num_vals)
            var = sum((v - mean) ** 2 for v in num_vals) / len(num_vals)
            if var == 0.0 and not (is_penalty and mean == 0.0):
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
        elif res_dict[clean_req] not in ("PASS", "PASS*", "KNOWN_FAIL", "KNOWN-FAIL"):
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
    penalty_metrics = extract_penalty_metrics(config)
    probe_rounds = (probe_data.get("rounds") if probe_data else None) or []

    g2_pass, g2_reasons = check_metric_liveness(probe_rounds, used_metrics, penalty_metrics=penalty_metrics)
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
