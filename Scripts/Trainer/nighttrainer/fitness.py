"""Fitness of a candidate from its arena rounds (canonical rounds from adapter.py, candidate's point of view).

Per round:  win * won + survivors * survivors/squad + hp_lost * hp_lost/hp_max
            + duration term (won rounds only: 1 at target_s, 0 beyond +-tolerance_s - decisive but not reckless)
            + sum(behaviour weight * min(value, cap) / cap)   (bonuses > 0, penalties < 0)
A crashed / timed-out round scores crash_penalty and nothing else. Fitness = the mean over rounds.
All weights come from trainer_config.json "fitness".
"""
from __future__ import annotations

METRICS = ("kills", "flanks_attempted", "flanks_succeeded", "suppression_s", "ambushes", "retreats",
           "retreats_survived", "heals", "friendly_fire", "stuck_events")


def crash_round() -> dict:
    return {"crashed": True, "won": 0.0}


def _num(r: dict, key: str, default: float = 0.0) -> float:
    v = r.get(key)
    return float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else default


def round_score(r: dict, cfg: dict) -> float:
    if r.get("crashed"):
        return float(cfg.get("crash_penalty", -5.0))
    won = _num(r, "won")
    squad = max(1.0, _num(r, "squad_size", 4.0))
    hp_max = _num(r, "hp_max") or squad * float(cfg.get("hp_per_unit", 100.0))
    score = float(cfg.get("win", 10.0)) * won
    score += float(cfg.get("survivors", 3.0)) * min(1.0, _num(r, "survivors") / squad)
    score += float(cfg.get("hp_lost", -3.0)) * min(1.0, _num(r, "hp_lost") / hp_max)
    dur = cfg.get("duration", {})
    if won >= 1.0 and dur:
        target, tol = float(dur.get("target_s", 150.0)), max(1.0, float(dur.get("tolerance_s", 120.0)))
        score += float(dur.get("weight", 1.0)) * max(0.0, 1.0 - abs(_num(r, "duration_s") - target) / tol)
    for table in ("bonuses", "penalties"):
        for key, spec in cfg.get(table, {}).items():
            cap = max(1e-6, float(spec.get("cap", 1.0)))
            score += float(spec.get("weight", 0.0)) * min(_num(r, key), cap) / cap
    return score


def aggregate(rounds: list, cfg: dict) -> dict:
    """Fitness plus the readable stats the leaderboard / report show."""
    n = len(rounds)
    if n == 0:
        return {"fitness": float(cfg.get("crash_penalty", -5.0)), "rounds": 0, "crashes": 0}
    ok = [r for r in rounds if not r.get("crashed")]
    m = max(1, len(ok))

    def mean(key, pool=ok):
        return sum(_num(r, key) for r in pool) / m

    out = {
        "fitness": round(sum(round_score(r, cfg) for r in rounds) / n, 4),
        "rounds": n,
        "crashes": n - len(ok),
        "win_rate": round(sum(_num(r, "won") for r in rounds) / n, 4),
        "survivors_frac": round(sum(_num(r, "survivors") / max(1.0, _num(r, "squad_size", 4.0)) for r in ok) / m, 4),
        "hp_lost": round(mean("hp_lost"), 2),
        "duration_s": round(mean("duration_s"), 1),
    }
    for key in METRICS:
        out[key] = round(mean(key), 3)
    return out


def aggregate_by_opponent(rounds_by_opponent: dict, cfg: dict) -> dict:
    """{"bot": [...], "champion": [...]} -> {"all": agg, "bot": agg, "champion": agg}."""
    everything = [r for rs in rounds_by_opponent.values() for r in rs]
    out = {"all": aggregate(everything, cfg)}
    for opp, rs in rounds_by_opponent.items():
        out[opp] = aggregate(rs, cfg)
    return out
