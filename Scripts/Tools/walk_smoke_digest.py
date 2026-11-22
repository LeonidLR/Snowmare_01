#!/usr/bin/env python3
"""Digest of CodexTactics.LevelGenBuildingsWalkSmoke failures (task 098).

Usage:
    python Scripts/Tools/walk_smoke_digest.py <log or json> [more files] [--json]

Inputs (read-only):
  * smoke log: lines ``Smoke leg N/M <story> <kind> <from> -> <to> ...: ok|FAIL ...``,
    the final ``Smoke RESULT:`` line and the engine's ``is stuck and failed to move`` lines;
  * ``Saved/LevelGen/buildings_walk_<Map>.json`` (schema ``codex.buildings_walk/1``).

Output: per map a short digest (header, failures by cause class, 150 cm position clusters,
stuck-mesh count, and a per-class before/after delta when one map is given twice -
the FIRST file of a map is "before", the LAST is "after").

Cause classes (parsed from the FAIL text):
  ratio          squad time > 2 x solo time
  follower_stuck a follower is STUCK although its move order is active
  leader_stopped the leader STOPPED, no move order running
  refused        the leg was refused up front: no complete navmesh path / start or goal off navmesh
  other          everything else (TIMED OUT, follower STOPPED, ...)
"""
from __future__ import annotations

import argparse
import json
import math
import re
import sys
from collections import Counter, OrderedDict
from pathlib import Path

CLASSES = ("ratio", "follower_stuck", "leader_stopped", "refused", "other")
CLUSTER_CM = 150.0

LEG_RE = re.compile(
    r"Smoke leg (\d+)/(\d+) (\S+) (\S+) (\S+) -> (.+?)(?: \(entry [^)]*\))?: (ok|FAIL)\b(.*)")
RESULT_RE = re.compile(
    r"Smoke RESULT: (PASS|FAIL) LevelGenBuildingsWalk on (\S+): (\d+)/(\d+) buildings passed"
    r".*?walk legs (\d+)/(\d+); flat entry legs (\d+)/(\d+) \(squad <= 2 x solo, max ([\d.]+)\)"
    r"(?:.*?(\d+) s wall)?")
STUCK_RE = re.compile(r"is stuck and failed to move!.*?Actor:(\S+)")
POS_RE = re.compile(r"at V\(X=(-?[\d.]+), Y=(-?[\d.]+)(?:, Z=(-?[\d.]+))?\)")
STOREY_RE = re.compile(r"_f(\d+)_|storey_(\d+)|stair_(\d+)|\b(roof)\b")


def classify(text: str) -> str:
    """Map a FAIL reason text to one of CLASSES."""
    if re.search(r"no complete navmesh path|off navmesh|refus", text, re.I):
        return "refused"
    if re.search(r"squad [\d.]+ s > ", text):
        return "ratio"
    if re.search(r"follower .*?STUCK", text):
        return "follower_stuck"
    if re.search(r"leader .*?STOPPED", text):
        return "leader_stopped"
    return "other"


def storey_of(*names: str) -> str:
    """Storey tag of a leg: f<N> (flat), st<N> (stair), roof; '?' if unknown."""
    for name in names:
        m = STOREY_RE.search(name or "")
        if m:
            if m.group(1) is not None:
                return "f" + m.group(1)
            if m.group(2) is not None:
                return "f" + m.group(2)
            if m.group(3) is not None:
                return "st" + m.group(3)
            return "roof"
    return "?"


def _pos(text: str):
    m = POS_RE.search(text)
    if not m:
        return None
    return (float(m.group(1)), float(m.group(2)), float(m.group(3) or 0.0))


def new_run(source: str) -> dict:
    return {"source": source, "map": None, "header": {}, "fails": [], "stuck": Counter(), "legs": 0}


def parse_log(path: Path) -> list[dict]:
    """Split a log into runs; each run ends at its ``Smoke RESULT`` line."""
    runs: list[dict] = []
    run = new_run(path.name)
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = STUCK_RE.search(line)
        if m:
            run["stuck"][m.group(1)] += 1
            continue
        m = RESULT_RE.search(line)
        if m:
            run["map"] = m.group(2)
            run["header"] = {
                "buildings_passed": int(m.group(3)), "buildings": int(m.group(4)),
                "walk_passed": int(m.group(5)), "walk": int(m.group(6)),
                "entry_passed": int(m.group(7)), "entry": int(m.group(8)),
                "max_ratio": float(m.group(9)),
                "wall_s": float(m.group(10)) if m.group(10) else None,
            }
            runs.append(run)
            run = new_run(path.name)
            continue
        m = LEG_RE.search(line)
        if not m:
            continue
        run["legs"] += 1
        if m.group(7) != "FAIL":
            continue
        tail = m.group(8)
        frm, to = m.group(5), m.group(6)
        run["fails"].append({
            "story": m.group(3), "kind": m.group(4), "from": frm, "to": to,
            "storey": storey_of(frm, to), "cause": classify(tail), "pos": _pos(tail),
        })
    if run["legs"] or run["stuck"]:  # log cut before the RESULT line
        run["map"] = run["map"] or "?"
        runs.append(run)
    return runs


def parse_json(path: Path) -> list[dict]:
    d = json.loads(path.read_text(encoding="utf-8", errors="replace"))
    run = new_run(path.name)
    run["map"] = d.get("map", "?")
    run["header"] = {
        "buildings_passed": d.get("passed"), "buildings": d.get("checked"),
        "walk_passed": d.get("walk_legs_passed"), "walk": d.get("walk_legs"),
        "entry_passed": d.get("entry_legs_passed"), "entry": d.get("entry_legs"),
        "max_ratio": d.get("max_squad_ratio"), "wall_s": d.get("wall_seconds"),
    }
    for b in d.get("buildings", []):
        for leg in list(b.get("walk", [])) + list(b.get("entries", [])):
            run["legs"] += 1
            if leg.get("pass", True):
                continue
            pos = leg.get("fail_pos") or leg.get("end_pos")
            run["fails"].append({
                "story": b.get("id", "?"), "kind": leg.get("kind", "?"),
                "from": leg.get("from", ""), "to": leg.get("to", ""),
                "storey": storey_of(leg.get("from", ""), leg.get("to", "")),
                "cause": classify(leg.get("reason", "")),
                "pos": tuple(float(v) for v in pos[:3]) + (0.0,) * (3 - len(pos[:3])) if pos else None,
            })
    return [run]


def load(path: Path) -> list[dict]:
    head = path.read_text(encoding="utf-8", errors="replace")[:200].lstrip()
    if path.suffix.lower() == ".json" or head.startswith("{"):
        return parse_json(path)
    return parse_log(path)


def clusters(fails: list[dict], radius: float = CLUSTER_CM) -> list[dict]:
    """Single-link clusters of failures whose positions are within ``radius`` cm (3D); size >= 2 only."""
    pts = [f for f in fails if f["pos"]]
    parent = list(range(len(pts)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for i in range(len(pts)):
        for j in range(i + 1, len(pts)):
            if math.dist(pts[i]["pos"], pts[j]["pos"]) <= radius:
                parent[find(i)] = find(j)
    groups: dict[int, list[dict]] = {}
    for i, f in enumerate(pts):
        groups.setdefault(find(i), []).append(f)
    out = []
    for g in groups.values():
        if len(g) < 2:
            continue
        n = len(g)
        c = [sum(f["pos"][k] for f in g) / n for k in range(3)]
        out.append({
            "count": n, "pos": [round(v) for v in c],
            "stories": sorted({f["story"] for f in g}),
            "causes": dict(Counter(f["cause"] for f in g)),
        })
    out.sort(key=lambda c: (-c["count"], c["pos"]))
    return out


def by_class(fails: list[dict]) -> dict:
    res = OrderedDict((c, {"count": 0, "stories": OrderedDict()}) for c in CLASSES)
    for f in fails:
        e = res[f["cause"]]
        e["count"] += 1
        st = e["stories"].setdefault(f["story"], [])
        if f["storey"] not in st:
            st.append(f["storey"])
    return res


def digest(run: dict) -> dict:
    return {
        "map": run["map"], "source": run["source"], "header": run["header"],
        "fail_total": len(run["fails"]), "classes": by_class(run["fails"]),
        "clusters": clusters(run["fails"]), "stuck_meshes": dict(run["stuck"].most_common()),
    }


def build(paths: list[Path]) -> list[dict]:
    """Digest every run; a map seen twice gets ``delta`` (first = before, last = after)."""
    digests = [digest(r) for p in paths for r in load(p)]
    by_map: dict[str, list[dict]] = {}
    for d in digests:
        by_map.setdefault(d["map"], []).append(d)
    for group in by_map.values():
        if len(group) >= 2:
            before, after = group[0], group[-1]
            after["delta"] = {
                "before": before["source"], "after": after["source"],
                "fail_total": after["fail_total"] - before["fail_total"],
                "classes": {c: after["classes"][c]["count"] - before["classes"][c]["count"] for c in CLASSES},
            }
    return digests


def _f(v, fmt="{}"):
    return "?" if v is None else fmt.format(v)


def render(d: dict) -> str:
    h = d["header"]
    lines = [
        "== {} [{}]: buildings {}/{}, walk legs {}/{}, flat entry legs {}/{}, max squad/solo {}, wall {} s".format(
            d["map"], d["source"], _f(h.get("buildings_passed")), _f(h.get("buildings")),
            _f(h.get("walk_passed")), _f(h.get("walk")), _f(h.get("entry_passed")), _f(h.get("entry")),
            _f(h.get("max_ratio")), _f(h.get("wall_s"), "{:.0f}"))]
    lines.append("failed legs: {}".format(d["fail_total"]))
    for c in CLASSES:
        e = d["classes"][c]
        if not e["count"]:
            continue
        items = ["{}[{}]".format(s, ",".join(st)) for s, st in e["stories"].items()]
        more = " +{} more".format(len(items) - 6) if len(items) > 6 else ""
        lines.append("  {:<14}{:>3}  {}{}".format(c, e["count"], " ".join(items[:6]), more))
    if d["clusters"]:
        lines.append("clusters (<= {:.0f} cm):".format(CLUSTER_CM))
        for c in d["clusters"][:6]:
            lines.append("  {} fails at {} {} {} (stories {}; causes {})".format(
                c["count"], *c["pos"], ",".join(c["stories"]),
                ", ".join("{} x{}".format(k, v) for k, v in sorted(c["causes"].items()))))
    if d["stuck_meshes"]:
        top = list(d["stuck_meshes"].items())
        lines.append("stuck meshes ({} lines): {}".format(
            sum(d["stuck_meshes"].values()), ", ".join("{} x{}".format(k, v) for k, v in top[:6])))
    if "delta" in d:
        dl = d["delta"]
        lines.append("delta {} -> {}: fails {:+d}; {}".format(
            dl["before"], dl["after"], dl["fail_total"],
            ", ".join("{} {:+d}".format(c, v) for c, v in dl["classes"].items() if v) or "no per-class change"))
    return "\n".join(lines)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Digest of walk-smoke logs / JSON reports")
    ap.add_argument("files", nargs="+", type=Path)
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    args = ap.parse_args(argv)
    missing = [p for p in args.files if not p.is_file()]
    if missing:
        print("not found: " + ", ".join(map(str, missing)), file=sys.stderr)
        return 2
    digests = build(args.files)
    if args.json:
        print(json.dumps(digests, indent=2, ensure_ascii=False))
    else:
        print("\n\n".join(render(d) for d in digests))
    return 0


if __name__ == "__main__":
    sys.exit(main())
