"""Genetic operators: gaussian mutation, uniform crossover, tournament selection, elitism.

Genes are {pointer: value} dicts (see genome.py); every operator takes an explicit random.Random so a generation
is reproducible from (run seed, generation) alone, which is what makes resume deterministic.
"""
from __future__ import annotations

import random

from .genome import GeneSpec, clamp_gene


def generation_rng(run_seed: int, generation: int, stream: str = "evo") -> random.Random:
    return random.Random(f"{run_seed}:{generation}:{stream}")


def mutate(genes: dict, specs: dict, rng: random.Random, rate: float = 0.2, sigma_scale: float = 1.0) -> dict:
    """Each non-frozen gene mutates with probability `rate` by N(0, step * sigma_scale), clamped to its bounds.
    At least one gene always changes (if any is mutable), so a child never silently equals its parent."""
    child = dict(genes)
    mutable = [p for p, s in specs.items() if not s.frozen and p in child]
    if not mutable:
        return child
    changed = False
    for pointer in mutable:
        if rng.random() < rate:
            changed |= _nudge(child, pointer, specs[pointer], rng, sigma_scale)
    tries = 0
    while not changed and tries < 8:
        pointer = rng.choice(mutable)
        changed = _nudge(child, pointer, specs[pointer], rng, sigma_scale)
        tries += 1
    return child


def _nudge(genes: dict, pointer: str, spec: GeneSpec, rng: random.Random, sigma_scale: float) -> bool:
    old = genes[pointer]
    delta = rng.gauss(0.0, spec.step * sigma_scale)
    if spec.is_int and abs(delta) < 0.5:
        delta = 1.0 if delta >= 0 else -1.0
    new = clamp_gene(old + delta, spec)
    if new == old:  # stuck on a bound: bounce the other way
        new = clamp_gene(old - delta, spec)
    genes[pointer] = new
    return new != old


def crossover(a: dict, b: dict, rng: random.Random, swap_prob: float = 0.5) -> dict:
    """Uniform crossover: each gene comes from `b` with probability swap_prob, else from `a`."""
    return {p: (b.get(p, v) if rng.random() < swap_prob else v) for p, v in a.items()}


def tournament(scored: list, rng: random.Random, k: int = 3) -> dict:
    """scored = [(genes, fitness)]; the fittest of k random entrants."""
    entrants = [scored[rng.randrange(len(scored))] for _ in range(max(1, k))]
    return max(entrants, key=lambda e: e[1])[0]


def initial_population(baseline: dict, specs: dict, size: int, rng: random.Random, cfg: dict) -> list:
    """The baseline itself plus (size - 1) mutants of it (a wider first-generation sigma explores more)."""
    pop = [dict(baseline)]
    sigma = float(cfg.get("initial_sigma_scale", 1.5))
    while len(pop) < size:
        pop.append(mutate(baseline, specs, rng, float(cfg.get("initial_mutation_rate", 0.5)), sigma))
    return pop


def next_generation(scored: list, specs: dict, rng: random.Random, cfg: dict) -> list:
    """scored = [(genes, fitness)] of the finished generation -> the next population (same size)."""
    size = len(scored)
    ranked = sorted(scored, key=lambda e: e[1], reverse=True)
    elites = [dict(g) for g, _ in ranked[: max(0, min(int(cfg.get("elitism", 2)), size))]]
    children = []
    while len(elites) + len(children) < size:
        parent = tournament(ranked, rng, int(cfg.get("tournament_k", 3)))
        if rng.random() < float(cfg.get("crossover_rate", 0.7)):
            parent = crossover(parent, tournament(ranked, rng, int(cfg.get("tournament_k", 3))), rng)
        children.append(mutate(parent, specs, rng, float(cfg.get("mutation_rate", 0.2)),
                               float(cfg.get("sigma_scale", 1.0))))
    return elites + children
