#!/usr/bin/env python3
"""Reporting-only breakdown of a LevelGenBuildingsWalkSmoke report (Saved/LevelGen/buildings_walk_<map>.json, schema codex.buildings_walk/1).

The walk smoke's gate is unchanged (its verdict stays in the log). This splits the failures into the KNOWN GAME TASK - a squad entry leg into a
small flat hall where the column does not fit (BGEN_KIT_PLANS_2026-11-21.md question 7, designer decision 2026-11-21: the whole squad must
enter, the formation has to compress in tight rooms - a separate formation task; a follower stops / sticks on the landing, or squad > 2 x solo
on a 1-2 s leg) - and everything else (nav gaps, wrong storeys, solo failures), and recomputes the building share both ways.

    python Scripts/Tools/walk_report_breakdown.py <buildings_walk_<map>.json> <layout.json> [--max-hall-m2 12] [--json]

A leg counts as a "hall question" leg when: kind entry_squad, it failed, its destination room's use is `hall` (or its area <= --max-hall-m2),
and the reason is a follower STOPPED / STUCK, the squad / solo ratio or a time-out with followers left outside (never "off navmesh" / "no path": those are real defects).
"""

import argparse
import json
import sys

HALL_REASONS = ("follower ", "squad ", "TIMED OUT")
DEFECT_MARKERS = ("off navmesh", "no complete", "no path", "no squad leader", "no solo time")


def room_index(layout):
    """{room id: (use, area m2)} over every building of an accepted layout."""
    out = {}
    for b in layout.get("buildings", []):
        for rm in b.get("rooms", []):
            bd = rm.get("bounds")
            area = -1.0
            if bd:
                area = (bd["max"][0] - bd["min"][0]) * (bd["max"][1] - bd["min"][1]) / 1e4
            out[rm["id"]] = (rm.get("use"), area)
    return out


def is_hall_question(leg, rooms, max_hall_m2=12.0):
    """True for a failed squad entry leg into a small hall whose reason is the column / ratio question, not a defect."""
    if leg.get("pass") or leg.get("kind") != "entry_squad":
        return False
    reason = leg.get("reason", "")
    if any(m in reason for m in DEFECT_MARKERS) or not reason.startswith(HALL_REASONS):
        return False
    use, area = rooms.get(leg.get("to"), (None, -1.0))
    return use == "hall" or (0.0 <= area <= max_hall_m2)


def breakdown(report, layout, max_hall_m2=12.0):
    """Per building: pass as reported, pass without the hall-question legs; plus the totals and the other (real) failures."""
    rooms = room_index(layout)
    rows = []
    for b in report.get("buildings", []):
        entries = b.get("entries", [])
        hall = [leg for leg in entries if is_hall_question(leg, rooms, max_hall_m2)]
        other_entries = [leg for leg in entries if not leg.get("pass") and leg not in hall]
        walk_fail = [leg for leg in b.get("walk", []) if not leg.get("pass")]
        path_ok = bool(b.get("path_pass", True))
        role_issues = sum(len(v) for k, v in (b.get("plan_roles") or {}).items() if isinstance(v, list))
        rows.append({
            "id": b.get("id"),
            "pass": bool(b.get("pass")),
            "pass_without_hall_legs": path_ok and not walk_fail and not other_entries and role_issues == 0,
            "hall_legs_failed": len(hall),
            "hall_m2": sorted({round(rooms.get(leg.get("to"), (None, -1.0))[1], 1) for leg in hall}),
            "entry_legs": len(entries),
            "other_failures": (["path: %s" % ((b.get("first_failure") or {}).get("reason", "path check failed"))] if not path_ok else [])
            + ["walk %s -> %s: %s" % (leg.get("from"), leg.get("to"), leg.get("reason")) for leg in walk_fail]
            + ["%s %s -> %s: %s" % (leg.get("kind"), leg.get("from"), leg.get("to"), leg.get("reason")) for leg in other_entries],
        })
    n = len(rows)
    passed = sum(r["pass"] for r in rows)
    passed_wo = sum(r["pass_without_hall_legs"] for r in rows)
    entry_total = sum(r["entry_legs"] for r in rows)
    entry_ok = report.get("entry_legs_passed", 0)
    hall_total = sum(r["hall_legs_failed"] for r in rows)
    return {
        "map": report.get("map"),
        "buildings": n,
        "passed": passed,
        "share": round(passed / n, 3) if n else 0.0,
        "passed_without_hall_legs": passed_wo,
        "share_without_hall_legs": round(passed_wo / n, 3) if n else 0.0,
        "entry_legs": entry_total,
        "entry_legs_passed": entry_ok,
        "hall_question_legs": hall_total,
        "entry_legs_passed_without_hall_legs": "%d/%d" % (entry_ok, entry_total - hall_total),
        "walk_legs": "%s/%s" % (report.get("walk_legs_passed"), report.get("walk_legs")),
        "rows": rows,
    }


def summary_line(res):
    return ("walk breakdown %s: %d/%d buildings pass as gated (share %.2f); without the %d hall-question squad legs %d/%d (share %.2f); "
            "flat entry legs %d/%d, without them %s; walk legs %s") % (
        res["map"], res["passed"], res["buildings"], res["share"], res["hall_question_legs"], res["passed_without_hall_legs"], res["buildings"],
        res["share_without_hall_legs"], res["entry_legs_passed"], res["entry_legs"], res["entry_legs_passed_without_hall_legs"], res["walk_legs"])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("report")
    ap.add_argument("layout")
    ap.add_argument("--max-hall-m2", type=float, default=12.0)
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    with open(a.report, encoding="utf-8") as f:
        report = json.load(f)
    with open(a.layout, encoding="utf-8") as f:
        layout = json.load(f)
    res = breakdown(report, layout, a.max_hall_m2)
    if a.json:
        print(json.dumps(res, indent=1))
        return 0
    print(summary_line(res))
    for r in res["rows"]:
        print("  %-28s gated %-4s without hall legs %-4s hall legs failed %d/%d (halls %s m2)%s" % (
            r["id"], "PASS" if r["pass"] else "FAIL", "PASS" if r["pass_without_hall_legs"] else "FAIL", r["hall_legs_failed"], r["entry_legs"],
            "/".join("%g" % a for a in r["hall_m2"]) or "-",
            "" if not r["other_failures"] else "; other: " + " | ".join(r["other_failures"][:3])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
