"""Fitness of a candidate from its arena rounds (canonical rounds from adapter.py, candidate's point of view).

Per round:  win * won + survivors * survivors/squad + hp_lost * hp_lost/hp_max
            + duration term (won rounds only: 1 at target_s, 0 beyond +-tolerance_s - decisive but not reckless)
            + sum(behaviour weight * min(value, cap) / cap)   (bonuses > 0, penalties < 0)
A crashed / timed-out round scores crash_penalty and nothing else. Fitness = the mean over rounds.
close_range (optional): weight * share of the side's shots fired closer than close_range_m (track_stats.py).
All weights come from trainer_config.json "fitness".
"""
from __future__ import annotations

METRICS = ("kills", "flanks_attempted", "flanks_succeeded", "suppression_s", "ambushes", "retreats",
           "retreats_survived", "heals", "friendly_fire", "stuck_events", "close_shot_ratio", "close_hits_taken",
           # task #53 (objective rounds): bonuses via fitness.bonuses (damage_from_concealment, first_strike,
           # objective_win), the rest are stats for the report
           "damage_from_concealment", "first_strike", "objective_hold_s", "median_engagement_m", "objective_win",
           "strikes_unobserved")


def crash_round() -> dict:
    return {"crashed": True, "won": 0.0}


def _num(r: dict, key: str, default: float = 0.0) -> float:
    v = r.get(key)
    return float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else default


def round_score(r: dict, cfg: dict) -> float:
    if r.get("crashed"):
        return float(cfg.get("crash_penalty", -5.0))
    won = _num(r, "won")
    # fix/pmc-arena-stalemate (2026-11-10, the designer): a round without contact (no shot between the sides) is a draw
    # with no reward - neither the hiding side nor the "defender who won on time" is paid; a capture of an empty point
    # still counts as the attacker's win.
    if r.get("contact") is False and won < 1.0:
        return float(cfg.get("no_contact_score", 0.0))
    squad = max(1.0, _num(r, "squad_size", 4.0))
    hp_max = _num(r, "hp_max") or squad * float(cfg.get("hp_per_unit", 100.0))
    # 2026-11-06: the pmc-v2 night learnt "don't fight" - a time-limit draw paid ~8 (half a win + all survivors).
    # A draw now earns draw_value of a win, survivors count draw_survivor_factor in a draw, a time-limit end costs
    # timeout_penalty (defaults keep the old scoring when the keys are absent).
    draw = 0.0 < won < 1.0
    limit = _num(r, "time_limit_s")
    timed_out = draw and limit > 0.0 and _num(r, "duration_s") >= limit - 1.0
    win_credit = float(cfg.get("draw_value", won)) if draw else won
    score = float(cfg.get("win", 10.0)) * win_credit
    survivor_factor = float(cfg.get("draw_survivor_factor", 1.0)) if draw else 1.0
    score += float(cfg.get("survivors", 3.0)) * survivor_factor * min(1.0, _num(r, "survivors") / squad)
    # Hiding is fine when it pays off (the user, 2026-11-06: "hide to ambush better, not just to hide"):
    # the time-limit penalty only hits a round where nobody hurt anybody; damage dealt and a kill
    # advantage pay, so a patient ambush that bleeds the enemy outscores both pure hiding and a frontal rush.
    engaged = (_num(r, "kills") + _num(r, "opp_kills") > 0.0 or _num(r, "hp_lost") > 0.0
               or _num(r, "opp_hp_lost") > 0.0)
    if timed_out and not engaged:
        score += float(cfg.get("timeout_penalty", 0.0))
    if "opp_hp_lost" in r:
        score += float(cfg.get("damage_dealt", 0.0)) * min(1.0, _num(r, "opp_hp_lost") / hp_max)
    score += float(cfg.get("kill_advantage", 0.0)) * max(-3.0, min(3.0, _num(r, "kills") - _num(r, "opp_kills")))
    hp_factor = float(cfg.get("draw_hp_factor", 1.0)) if draw else 1.0
    score += float(cfg.get("hp_lost", -3.0)) * hp_factor * min(1.0, _num(r, "hp_lost") / hp_max)
    dur = cfg.get("duration", {})
    if won >= 1.0 and dur:
        target, tol = float(dur.get("target_s", 150.0)), max(1.0, float(dur.get("tolerance_s", 120.0)))
        score += float(dur.get("weight", 1.0)) * max(0.0, 1.0 - abs(_num(r, "duration_s") - target) / tol)
    for table in ("bonuses", "penalties"):
        for key, spec in cfg.get(table, {}).items():
            if not isinstance(spec, dict):  # "_comment" keys
                continue
            cap = max(1e-6, float(spec.get("cap", 1.0)))
            score += float(spec.get("weight", 0.0)) * min(_num(r, key), cap) / cap
    cr = cfg.get("close_range")
    if cr and "close_shot_ratio" in r:  # no track / no shots = no penalty
        score += float(cr.get("weight", -4.0)) * min(1.0, max(0.0, _num(r, "close_shot_ratio")))
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
