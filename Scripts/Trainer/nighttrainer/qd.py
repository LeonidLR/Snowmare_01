"""MAP-Elites archive (G002). Each cell keeps the weights with the best scalar fitness (fitness.py).

Axes, bin counts and ranges come from trainer_config.json "qd". The behaviour descriptor is the mean of the
candidate's non-crashed rounds. A missing metric is 0 (adapter.behaviour_defaults). Parents of the next
population are drawn uniformly from filled cells. An empty archive leaves selection to the scalar GA.
"""
from __future__ import annotations

import os

from . import evolution
from .adapter import behaviour_defaults
from .genome import load_json, write_json

ARCHIVE_SCHEMA = "codex.trainer_qd_archive/1"


def _axes(cfg: dict) -> list:
    return [a for a in (cfg.get("axes") or []) if isinstance(a, dict) and a.get("id")]


def axis_value(behaviour: dict, axis: dict) -> float:
    keys = axis.get("metrics") or ([axis["metric"]] if axis.get("metric") else [])
    return sum(float(behaviour.get(k, 0.0) or 0.0) for k in keys)


def bin_index(value: float, axis: dict) -> int:
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
        if path and os.path.exists(path):
            doc = load_json(path)
            cells = doc.get("cells") or {}
            if isinstance(cells, dict):
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

    def filled(self) -> list:
        return list(self.cells.values())

    def sample_cell(self, rng) -> dict | None:
        cells = self.filled()
        if not cells:
            return None
        return cells[rng.randrange(len(cells))]

    def sample_genes(self, rng, n: int) -> list:
        cells = self.filled()
        if not cells or n <= 0:
            return []
        return [dict(cells[rng.randrange(len(cells))]["genes"]) for _ in range(n)]


def next_population(archive: Archive, specs: dict, rng, evo_cfg: dict, size: int) -> list | None:
    """Offspring of archive parents. None when the archive is empty (caller keeps the scalar GA)."""
    if size <= 0 or not archive.filled():
        return None
    cross = float(evo_cfg.get("crossover_rate", 0.7))
    rate = float(evo_cfg.get("mutation_rate", 0.2))
    sigma = float(evo_cfg.get("sigma_scale", 1.0))
    children = []
    while len(children) < size:
        parents = archive.sample_genes(rng, 2)
        parent = parents[0]
        if len(parents) > 1 and rng.random() < cross:
            parent = evolution.crossover(parent, parents[1], rng)
        children.append(evolution.mutate(parent, specs, rng, rate, sigma))
    return children
