"""MAP-Elites archive (G002). Each cell keeps the weights with the best scalar fitness (fitness.py).

Axes, bin counts and ranges come from trainer_config.json "qd". The behaviour descriptor is the mean of the
candidate's non-crashed rounds. A missing metric is 0 (adapter.behaviour_defaults). Parents of the next
population are drawn uniformly from filled cells; the top evolution.elitism genes are copied unmutated.
An empty archive leaves selection to the scalar GA. A file that is not an object, or whose axes or bins
do not match the config, is discarded so an old grid cannot be reused under new keys.
"""
from __future__ import annotations

import os

from . import evolution
from .adapter import behaviour_defaults
from .genome import load_json, write_json

ARCHIVE_SCHEMA = "codex.trainer_qd_archive/1"
DEFAULT_SEARCH_MODE = "ga"


def search_mode_of(cfg: dict | None) -> str:
    """ga unless search.mode names another mode. qd is a config switch, not the default."""
    raw = ((cfg or {}).get("search") or {}).get("mode", DEFAULT_SEARCH_MODE)
    mode = str("" if raw is None else raw).strip().lower()
    return mode or DEFAULT_SEARCH_MODE


def _metric_names(axis: dict) -> tuple:
    metrics = axis.get("metrics")
    if isinstance(metrics, list) and metrics:
        return tuple(str(item) for item in metrics)
    if axis.get("metric"):
        return (str(axis["metric"]),)
    return ()


def _axis_signature(axes) -> tuple | None:
    """id, bin count, range, metrics, bin_edges. None when `axes` is not a list."""
    if not isinstance(axes, list):
        return None
    out = []
    for axis in axes:
        if not isinstance(axis, dict) or not axis.get("id"):
            continue
        span = axis.get("range") or [0.0, 1.0]
        try:
            lo, hi = float(span[0]), float(span[1])
            bins = int(axis.get("bins", 1))
        except (TypeError, ValueError, IndexError):
            return None
        edges = tuple(float(e) for e in (axis.get("bin_edges") or []))
        out.append((str(axis["id"]), bins, lo, hi, _metric_names(axis), edges))
    return tuple(out)


def _log_archive(msg: str) -> None:
    print(f"[qd] {msg}", flush=True)


def _axes(cfg: dict) -> list:
    return [a for a in (cfg.get("axes") or []) if isinstance(a, dict) and a.get("id")]


def axis_value(behaviour: dict, axis: dict) -> float:
    keys = axis.get("metrics") or ([axis["metric"]] if axis.get("metric") else [])
    return sum(float(behaviour.get(k, 0.0) or 0.0) for k in keys)


def bin_index(value: float, axis: dict) -> int:
    edges = axis.get("bin_edges")
    if edges and isinstance(edges, (list, tuple)):
        v = float(value)
        for i, edge in enumerate(edges):
            if v < float(edge):
                return i
        return len(edges)
    bins = max(1, int(axis.get("bins", 1)))
    span_src = axis.get("range") or [0.0, 1.0]
    lo, hi = float(span_src[0]), float(span_src[1])
    if bins == 1 or hi <= lo:
        return 0
    t = (float(value) - lo) / (hi - lo)
    if t < 0.0:
        return 0
    if t >= 1.0:
        return bins - 1
    return min(bins - 1, int(t * bins))


def _band(index: int, bins: int) -> str:
    if bins <= 1:
        return "mid"
    if index <= 0:
        return "low"
    if index >= bins - 1:
        return "high"
    return "mid"


def highlight_for(key: tuple, descriptor: dict, behaviour: dict, axes: list) -> str:
    """One line per cell for the morning report. Names the week-plan tactics when the bins match."""
    bands = {axis["id"]: _band(key[i] if i < len(key) else 0, max(1, int(axis.get("bins", 1))))
             for i, axis in enumerate(axes)}
    agg, mob, pat = bands.get("aggression", "mid"), bands.get("mobility", "mid"), bands.get("patience", "mid")
    suppress = float(behaviour.get("suppress_then_move", 0.0) or 0.0)
    if pat in ("mid", "high") and agg == "low":
        name = "patient ambusher"
    elif agg == "high" and mob in ("mid", "high"):
        name = "aggressive flanker"
    elif suppress >= 1.0 and mob in ("mid", "high"):
        name = "suppress-and-push"
    elif agg == "low" and mob == "low":
        name = "holder"
    else:
        name = "mixed"
    bits = [f"{bands[a['id']]} {a['id']} {float(descriptor.get(a['id'], 0.0)):.2f}" for a in axes]
    return name + (": " + ", ".join(bits) if bits else "")


def mean_behaviour(rounds: list) -> dict | None:
    """Mean of canonical metrics over rounds that actually ran. None when every round crashed."""
    ok = [r for r in rounds if not r.get("crashed") and isinstance(r.get("behaviour"), dict)]
    if not ok:
        return None
    keys = behaviour_defaults()
    return {key: sum(float(r["behaviour"].get(key, 0.0) or 0.0) for r in ok) / len(ok) for key in keys}


class Archive:
    def __init__(self, cfg: dict | None = None):
        self.axes = _axes(cfg or {})
        self.cells: dict = {}

    @classmethod
    def load(cls, path: str, cfg: dict | None = None) -> "Archive":
        arch = cls(cfg)
        if not path or not os.path.exists(path):
            return arch
        doc = load_json(path)
        if not isinstance(doc, dict):
            _log_archive(f"{path} is not an object; starting an empty archive")
            return arch
        if _axis_signature(doc.get("axes")) != _axis_signature(arch.axes):
            _log_archive(f"{path} axes or bins differ from the config; starting an empty archive")
            return arch
        cells = doc.get("cells") or {}
        if not isinstance(cells, dict):
            _log_archive(f"{path} cells are not an object; starting an empty archive")
            return arch
        arch.cells = cells
        return arch

    def save(self, path: str) -> None:
        write_json(path, {"schema": ARCHIVE_SCHEMA, "axes": self.axes, "filled": len(self.cells),
                          "capacity": self.capacity(), "cells": self.cells})

    def capacity(self) -> int:
        n = 1
        for axis in self.axes:
            n *= max(1, int(axis.get("bins", 1)))
        return n if self.axes else 0

    def key_for(self, behaviour: dict) -> tuple:
        return tuple(bin_index(axis_value(behaviour, axis), axis) for axis in self.axes)

    def descriptor(self, behaviour: dict) -> dict:
        return {axis["id"]: round(axis_value(behaviour, axis), 4) for axis in self.axes}

    def consider(self, candidate_id: str, genes: dict, fitness: float, behaviour: dict, generation: int) -> str:
        """insert / replace when this scalar is strictly better, else reject. Ties keep the incumbent."""
        if not self.axes:
            return "reject"
        key = self.key_for(behaviour)
        skey = ",".join(str(i) for i in key)
        prev = self.cells.get(skey)
        if prev is not None and float(fitness) <= float(prev["fitness"]):
            return "reject"
        action = "replace" if prev is not None else "insert"
        desc = self.descriptor(behaviour)
        self.cells[skey] = {
            "key": list(key),
            "fitness": round(float(fitness), 4),
            "behaviour": {k: round(float(v), 4) for k, v in behaviour.items()},
            "descriptor": desc,
            "id": candidate_id,
            "generation": int(generation),
            "genes": dict(genes),
            "highlight": highlight_for(key, desc, behaviour, self.axes),
        }
        return action

    def empty_neighbors(self, key: tuple) -> int:
        """Count how many immediately adjacent grid cells in behaviour space are currently empty."""
        if not self.axes:
            return 0
        dims = [max(1, int(a.get("bins", 1))) for a in self.axes]
        count = 0

        def _walk(axis_idx, cur):
            nonlocal count
            if axis_idx == len(dims):
                if cur != key:
                    skey = ",".join(str(i) for i in cur)
                    if skey not in self.cells:
                        count += 1
                return
            k = key[axis_idx] if axis_idx < len(key) else 0
            for step in (-1, 0, 1):
                nk = k + step
                if 0 <= nk < dims[axis_idx]:
                    _walk(axis_idx + 1, cur + (nk,))

        _walk(0, ())
        return count

    def filled(self) -> list:
        return list(self.cells.values())

    def sample_cell(self, rng, weighted_by_empty: bool = False) -> dict | None:
        cells = self.filled()
        if not cells:
            return None
        if not weighted_by_empty or len(cells) <= 1:
            return cells[rng.randrange(len(cells))]
        weights = [1.0 + float(self.empty_neighbors(tuple(c.get("key", ())))) for c in cells]
        return rng.choices(cells, weights=weights, k=1)[0]

    def sample_genes(self, rng, n: int, weighted_by_empty: bool = False) -> list:
        cells = self.filled()
        if not cells or n <= 0:
            return []
        if not weighted_by_empty or len(cells) <= 1:
            return [dict(cells[rng.randrange(len(cells))]["genes"]) for _ in range(n)]
        weights = [1.0 + float(self.empty_neighbors(tuple(c.get("key", ())))) for c in cells]
        chosen = rng.choices(cells, weights=weights, k=n)
        return [dict(c["genes"]) for c in chosen]


def next_population(archive: Archive, specs: dict, rng, evo_cfg: dict, size: int) -> list | None:
    """Offspring of archive parents. The best `evolution.elitism` genes are copied unmutated, then mutated
    children fill the rest (same shape as evolution.next_generation). None when the archive is empty."""
    if size <= 0 or not archive.filled():
        return None
    n_elite = max(0, min(int(evo_cfg.get("elitism", 2)), size))
    ranked = sorted(archive.filled(), key=lambda cell: float(cell.get("fitness", 0.0)), reverse=True)
    elites = [dict(cell["genes"]) for cell in ranked[:n_elite]]
    cross = float(evo_cfg.get("crossover_rate", 0.7))
    rate = float(evo_cfg.get("mutation_rate", 0.2))
    sigma = float(evo_cfg.get("sigma_scale", 1.0))

    # Mutation pressure into empty cells:
    empty_pressure = bool(evo_cfg.get("empty_cell_pressure", True))
    cap = archive.capacity()
    empty_ratio = (1.0 - (len(archive.cells) / cap)) if cap > 0 else 0.0

    children = []
    while len(elites) + len(children) < size:
        parents = archive.sample_genes(rng, 2, weighted_by_empty=empty_pressure)
        parent = parents[0]
        if len(parents) > 1 and rng.random() < cross:
            parent = evolution.crossover(parent, parents[1], rng)

        cur_rate = rate
        cur_sigma = sigma
        if empty_pressure and empty_ratio > 0.0:
            if rng.random() < empty_ratio:
                cur_rate = min(1.0, rate * (1.0 + 0.5 * empty_ratio))
                cur_sigma = sigma * (1.0 + 1.0 * empty_ratio)

        children.append(evolution.mutate(parent, specs, rng, cur_rate, cur_sigma))
    return elites + children
