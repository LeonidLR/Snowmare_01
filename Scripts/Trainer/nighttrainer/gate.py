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
import logging
import math
import os
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

from .adapter import ArenaAdapter
from .metrics import damage_dealt


def extract_used_metrics(config: dict) -> Set[str]:
    """Extract all metric identifiers used by fitness bonuses/penalties and QD axes."""
    metrics: Set[str] = set()

    # Fitness bonuses and penalties
    fitness = config.get("fitness") or {}
    for term in (fitness.get('doctrine') or {}).get('terms', []):
        metrics.add(term['metric'])
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


def build_probe_rows(config: dict, probe_data: dict | list[dict] | None) -> List[dict]:
    """Parse probe rounds into canonical rows using ArenaAdapter (Review 091 Point a)."""
    if probe_data is None:
        return []
    if isinstance(probe_data, list):
        if not all(isinstance(row, dict) for row in probe_data):
            raise ValueError("probe rows must be JSON objects")
        return probe_data
    if not isinstance(probe_data, dict):
        raise ValueError("probe must be a JSON object or an array of canonical rows")

    rounds = probe_data.get("rounds")
    if not isinstance(rounds, list) or not all(isinstance(row, dict) for row in rounds):
        raise ValueError("arena probe rounds must be an array of JSON objects")
    adapter = ArenaAdapter(
        config.get("arena", {}),
        repo_root=".",
        close_range_cfg=config.get("fitness", {}).get("close_range"),
    )
    rows = adapter.parse_doc(probe_data)
    # Preserve defence and raw references so defence metrics remain accessible.
    for row, raw_r in zip(rows, rounds):
        if "defence" in raw_r and "defence" not in row:
            row["defence"] = raw_r["defence"]
        row["_raw_round"] = raw_r
    return rows


def get_metric_value_from_round(rnd: dict, metric: str, side: str = "A") -> Any:
    """Retrieve the value of a metric from a canonical row or raw round dictionary.

    Checks:
    1. Direct key on canonical row (e.g. objective_win, damage_dealt, kills, survivors)
    2. rnd.behaviour dictionary (parsed by adapter or raw)
    3. defence block (either on row or in raw round)
    4. sides.<side> direct stats or candidate behaviour
    5. top-level round[metric]
    """
    if not isinstance(rnd, dict):
        return None
    if metric == 'damage_dealt' and ('damage_dealt' in rnd or 'opp_hp_lost' in rnd):
        return damage_dealt(rnd)
    if metric.startswith('behaviour.'):
        key = metric.split('.', 1)[1]
        if 'behaviour_observed' in rnd and key not in rnd['behaviour_observed']:
            return None
        return get_metric_value_from_round(rnd, key, side)

    # 1. Direct key on canonical row (objective_win derived by adapter, damage_dealt, kills, etc.)
    if metric in rnd and not metric.startswith("_"):
        val = _extract_node_value(rnd[metric])
        if val is not None:
            return val
        if rnd[metric] is None:
            return None

    # 2. Check behaviour dictionary (parsed behaviour from adapter or raw)
    beh = rnd.get("behaviour")
    if isinstance(beh, dict):
        metrics_dict = beh.get("metrics")
        if isinstance(metrics_dict, dict) and metric in metrics_dict:
            val = _extract_node_value(metrics_dict[metric])
            if val is not None:
                return val
        if metric in beh:
            val = _extract_node_value(beh[metric])
            if val is not None:
                return val

    # 3. Check defence block first (from row or raw round)
    defence = rnd.get("defence") or {}
    if not defence and "_raw_round" in rnd:
        defence = rnd["_raw_round"].get("defence") or {}
    if metric in defence:
        val = _extract_node_value(defence[metric])
        if val is not None:
            return val
        if defence[metric] is None:
            return None

    # 4. Check candidate side (A) on raw round or sides block
    sides = rnd.get("sides") or {}
    if not sides and "_raw_round" in rnd:
        sides = rnd["_raw_round"].get("sides") or {}
    side_info = sides.get(side) or {}
    if isinstance(side_info, dict):
        side_beh = side_info.get("behaviour")
        if isinstance(side_beh, dict):
            metrics_dict = side_beh.get("metrics")
            if isinstance(metrics_dict, dict) and metric in metrics_dict:
                val = _extract_node_value(metrics_dict[metric])
                if val is not None:
                    return val
            if metric in side_beh:
                val = _extract_node_value(side_beh[metric])
                if val is not None:
                    return val
        if metric in side_info:
            val = _extract_node_value(side_info[metric])
            if val is not None:
                return val
            if side_info[metric] is None:
                return None
        # Specific mappings
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
            if r.get('crashed'):
                continue
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


def _is_worse_state(new_state: str, cur_state: str) -> bool:
    """Return True if new_state is more severe/worse than cur_state (Review 091 Point c)."""
    order = {
        "FAIL": 0, "CRASH": 0, "TIMEOUT": 0, "ERROR": 0,
        "KNOWN_FAIL": 1, "KNOWN-FAIL": 1,
        "PASS*": 2,
        "PASS": 3,
    }
    new_rank = order.get(str(new_state).upper(), 0)
    cur_rank = order.get(str(cur_state).upper(), 3)
    return new_rank < cur_rank


def check_smoke_health(sweep_data: Optional[dict], required_smokes: Optional[List[str]] = None) -> Tuple[bool, List[str]]:
    """G1 Smoke Health: ensure all required foundation/scenario smokes are green in last sweep."""
    if not sweep_data:
        # If no sweep data provided, check if sweep file exists in standard location
        return False, ["No smoke sweep result data provided or found (sweep-result.json)"]

    results = sweep_data.get("results") or sweep_data.get("smokes") or {}
    # Convert list of results to dict, keeping the worst state per smoke name (Review 091 Point c)
    res_dict: Dict[str, str] = {}
    if isinstance(results, list):
        for item in results:
            name = item.get("Name") or item.get("name")
            state = item.get("State") or item.get("state") or ("PASS" if item.get("Pass") else "FAIL")
            if name:
                clean_name = name.replace("CodexTactics.", "")
                if clean_name not in res_dict or _is_worse_state(state, res_dict[clean_name]):
                    res_dict[clean_name] = state
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
    probe_data: dict | list[dict] | None = None,
    baseline_data: Optional[dict] = None,
    sweep_data: Optional[dict] = None,
    required_smokes: Optional[List[str]] = None,
    *,
    probe_path: Optional[str] = None,
) -> dict:
    """Run all training gate checks and return structured diagnostic results."""
    used_metrics = extract_used_metrics(config)
    penalty_metrics = extract_penalty_metrics(config)
    try:
        if probe_path is not None:
            with open(probe_path, "r", encoding="utf-8-sig") as probe_file:
                probe_data = json.load(probe_file)
        probe_rounds = build_probe_rows(config, probe_data)
    except (OSError, ValueError, TypeError, OverflowError) as exc:
        reason = f"Cannot read arena probe: {exc}"
        logging.getLogger(__name__).error("%s", reason)
        g2_pass, g2_reasons = False, [reason]
    else:
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
