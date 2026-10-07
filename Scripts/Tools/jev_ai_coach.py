"""Jev AI coach: trains the playtest bot's and the enemies' AI tunables with TypeSafe Jev as the diagnostician.

Loop (code owns the workflow, Jev supplies typed judgments, measured outcomes decide):
  1. run a bot batch (Scripts/bot_run.ps1) with the current Codex.* console variables (-dpcvars=, -NoAITuning);
  2. extract hard facts from the game logs (results, waves, death causes, marksman shots / hits / retreats / deaths,
     bot assaults, enemy pack tactics: flank orders / fall-backs / backstabs) - computed in code, never guessed;
  3. ask Jev in ONE request over a worded summary (numbers bucketed in code - jev-1.13 is weak at numbers): the bot's
     weakness, the enemies' AI issue, the marksman issue (choices), fairness and engagement (scores), avoidable (noul);
  4. take one bounded step of one whitelisted knob (Jev's confident pick, else a fixed order), run the next batch and
     keep the step only if that knob family's objective rose by more than the noise margin.

Two objectives (user decisions 2026-10-04):
  * bot knobs ("bot") play the squad: maximise the win rate (+ waves);
  * enemy intelligence knobs ("enemy_ai": pack tactics) make the enemies harder and more interesting: keep the
    VETERAN win rate inside WIN_BAND (40-75 %) and maximise Jev's engagement judgment there.
  * Enemy power knobs ("enemy": marksman damage, accuracy, ...) are balance - tuned only with --tune-enemies; otherwise
    the report lists Jev's top enemy issues as proposals for the user / Wave Editor.
  * Stealth knobs ("stealth": patrol perception / trap search, user request 2026-10-06) - tuned only with --stealth, on a
    patrol map (--map /Game/Maps/L_PatrolTest): keep the win rate inside WIN_BAND and aim for a detection that comes
    neither at once nor never (STEALTH_DETECT_BAND, from the [Stealth] log lines) plus Jev's stealth tension judgment.

    On a patrol map the playtest bot sneaks (Bot/BotStealthRules.h, user plan 2026-10-07) and logs «[Stealth] bot ...»
    lines plus one «[Stealth] outcome <ambush_*|detected|...>» line per run: the stealth objective wants the patrols to
    catch the sneaking bot in a fair share of the runs (STEALTH_DETECTED_SHARE_BAND), neither at once nor never
    (STEALTH_DETECT_BAND for the first sense detection before the fight), plus Jev's stealth tension judgment.

Runs are fixed-step and seeded (bot_run.ps1 passes -BotSeed=<run>): equal knobs give equal results, so a kept step is a
real effect on this level.

The starting point is the CURRENT Content/Data/AI/ai_tuning.json (its cvars are passed to every batch with -NoAITuning
-dpcvars=, so a batch measures exactly the knobs it names). Keys the user set by hand (the Wave Editor «Скрытность и
бой» tab writes them; anything not recorded in the file's "coach_cvars") are never changed and frozen for the search
(--tune-user-keys lets the coach try them; they are still only proposed).

Usage:
  python Scripts/Tools/jev_ai_coach.py [--iterations 6] [--runs 8] [--profile VETERAN] [--parallel 4]
                                        [--level-json <path>] [--tune-enemies] [--no-enemy-ai] [--dry-run]
                                        [--stealth [--map /Game/Maps/L_PatrolTest]] [--tune-user-keys] [--apply]
  python Scripts/Tools/jev_ai_coach.py --apply-proposal Saved/Coach/stealth_proposal.json [--force-keys]
Output: Saved/Telemetry/ai_coach/<timestamp>/report.json and report.md, and the best knob set as a PROPOSAL
(Saved/Coach/stealth_proposal.json + .md with --stealth, else Saved/Coach/ai_proposal.json + .md) — NOT applied: the
user reviews it and applies it with --apply-proposal (a merge into ai_tuning.json that keeps every other key and skips
keys the user changed since the run). --apply merges at the end of the run (the old behaviour, user keys still kept).
The API key: TYPESAFE_API_KEY, the Windows user environment (registry) or Saved/Config/typesafe.key (never printed).
No key / API error -> the fixed knob order, and the report says so.
"""

import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
API_URL = "https://api.typesafe.ai/v1/systemone"
MODEL = "jev-latest"
CONFIDENCE_GATE = 0.45
# A step is kept only when its objective rises by more than this (8 runs: one win = 0.125 of win rate).
NOISE_MARGIN = 0.1
WIN_BAND = (0.40, 0.75)
TUNING_FILE = os.path.join(ROOT, "Content", "Data", "AI", "ai_tuning.json")
PROPOSAL_DIR = os.path.join(ROOT, "Saved", "Coach")
COACH_COMMENT = "Written by Scripts/Tools/jev_ai_coach.py"
# Stealth objective: the first detection of a run should come after this many game seconds (a stealthy approach is
# possible) but before the upper edge (the patrols are not deaf and blind).
STEALTH_DETECT_BAND = (20.0, 120.0)
# ... and the patrols should catch the sneaking bot before it strikes in this share of the runs (sneaking is possible
# but not free).
STEALTH_DETECTED_SHARE_BAND = (0.3, 0.7)
SENSES = ("sight", "hearing", "smell")

# name -> (console variable, default, min, max, step, owner)
KNOBS = {
    "bot_clear_radius": ("Codex.Bot.AssaultClearRadius", 1500.0, 600.0, 3000.0, 300.0, "bot"),
    "bot_stop_distance": ("Codex.Bot.AssaultStopDistance", 900.0, 500.0, 1300.0, 150.0, "bot"),
    "enemy_flank_share": ("Codex.Enemy.FlankShareScale", 1.0, 0.0, 2.0, 0.25, "enemy_ai"),
    "enemy_focus_cap": ("Codex.Enemy.FocusCapBonus", 0.0, -1.0, 3.0, 1.0, "enemy_ai"),
    "enemy_preference": ("Codex.Enemy.PreferenceScale", 1.0, 0.0, 2.0, 0.25, "enemy_ai"),
    "enemy_morale_deaths": ("Codex.Enemy.MoraleDeathsBonus", 0.0, -1.0, 4.0, 1.0, "enemy_ai"),
    "enemy_morale": ("Codex.Enemy.Morale", 1.0, 0.0, 1.0, 1.0, "enemy_ai"),
    "marksman_damage": ("Codex.Marksman.ShotDamage", 45.0, 20.0, 45.0, 5.0, "enemy"),
    "marksman_accuracy": ("Codex.Marksman.BaseAccuracy", 0.6, 0.3, 0.6, 0.05, "enemy"),
    "marksman_aim": ("Codex.Marksman.AimDuration", 2.0, 2.0, 3.5, 0.25, "enemy"),
    "marksman_cooldown": ("Codex.Marksman.ShotCooldown", 2.5, 2.5, 5.0, 0.5, "enemy"),
    "marksman_kite_cooldown": ("Codex.Marksman.RetreatCooldown", 10.0, 4.0, 20.0, 2.0, "enemy"),
    # Patrol perception / trap search (Data/EnemyPerception.h; multipliers of enemy_perception.json, -1 keeps the data).
    "sight_range": ("Codex.Perception.SightRangeScale", 1.0, 0.5, 1.5, 0.1, "stealth"),
    "fov": ("Codex.Perception.FovScale", 1.0, 0.6, 1.6, 0.2, "stealth"),
    "prone_visibility": ("Codex.Perception.ProneVisibilityScale", 1.0, 0.5, 1.5, 0.25, "stealth"),
    "hearing": ("Codex.Perception.HearingScale", 1.0, 0.5, 1.5, 0.1, "stealth"),
    "smell": ("Codex.Perception.SmellScale", 1.0, 0.0, 2.0, 0.25, "stealth"),
    "time_to_detect": ("Codex.Perception.TimeToDetectScale", 1.0, 0.5, 2.0, 0.25, "stealth"),
    "search_seconds": ("Codex.Patrol.SearchSeconds", 60.0, 20.0, 120.0, 10.0, "stealth"),
    "search_radius": ("Codex.Patrol.SearchRadius", 1200.0, 600.0, 2400.0, 300.0, "stealth"),
}

# Jev option -> (description, (knob, direction)); None: no step.
BOT_OPTIONS = {
    "storms_marksmen_too_rarely": ("The squad lets marksmen shoot from beyond rifle range; it should storm them earlier even with some melee enemies around", ("bot_clear_radius", +1)),
    "storms_into_melee": ("The squad leaves the melee fight to storm marksmen and gets caught by hounds / cutters", ("bot_clear_radius", -1)),
    "stops_too_far_from_marksman": ("Storming operatives stop where their rifles still miss or barely reach the marksman", ("bot_stop_distance", -1)),
    "stops_too_close_to_marksman": ("Storming operatives run right up to the marksman and are shot on the way", ("bot_stop_distance", +1)),
    "no_clear_bot_weakness": ("The bot's choices look sound; the losses come from elsewhere", None),
}
# The direction depends on the band: below it the enemies must get less effective, above it more (see enemy_ai_step).
ENEMY_AI_OPTIONS = {
    "pack_predictable": ("The melee enemies run straight at the squad in a predictable stream; they should surround and flank more", ("enemy_flank_share", +1)),
    "pack_flanks_too_much": ("So many enemies circle round that the fight loses its front line and feels chaotic", ("enemy_flank_share", -1)),
    "pack_overwhelms_one": ("Too many enemies pile on one operative at once, he is torn down before the squad can react", ("enemy_focus_cap", -1)),
    "pack_spread_too_thin": ("The enemies spread over every operative and never threaten anyone seriously", ("enemy_focus_cap", +1)),
    "pack_retreats_too_much": ("The enemies keep breaking off and running back, the fights drag on", ("enemy_morale_deaths", +1)),
    "pack_never_breaks": ("The enemies never react to losses; the pack shows no survival instinct", ("enemy_morale_deaths", -1)),
    "pack_ignores_weak_spots": ("The enemies ignore wounded, isolated or turned-away operatives", ("enemy_preference", +1)),
    "enemy_ai_good": ("The enemies fight smartly and fairly; no change is needed", None),
}
ENEMY_OPTIONS = {
    "marksman_too_lethal_per_shot": ("A single marksman hit takes too large a share of an operative's health", ("marksman_damage", -1)),
    "marksman_too_accurate": ("Marksmen hit far too often at long range for a fair fight", ("marksman_accuracy", -1)),
    "marksman_no_reaction_window": ("The telegraphed aim is too short to react (take cover / move) before the shot", ("marksman_aim", +1)),
    "marksman_fires_too_often": ("Marksmen fire again too soon; the squad gets no breathing room", ("marksman_cooldown", +1)),
    "marksman_too_evasive": ("Marksmen keep running away so the squad can never close in", ("marksman_kite_cooldown", +1)),
    "enemies_fair": ("The marksmen behave fairly; no change is needed", None),
}
STEALTH_OPTIONS = {
    "spotted_instantly": ("The patrols spot the squad almost at once from far away; sneaking up is impossible", ("sight_range", -1)),
    "patrols_oblivious": ("The squad walks right past the patrols; they barely notice anything", ("sight_range", +1)),
    "tunnel_vision": ("The patrols only see straight ahead; flanking them is trivial", ("fov", +1)),
    "hear_too_well": ("The patrols hear the squad's footsteps through everything; moving at all gives it away", ("hearing", -1)),
    "deaf_patrols": ("Running and shooting next to a patrol goes unnoticed", ("hearing", +1)),
    "prone_useless": ("Crawling prone does not help: prone operatives are found as easily as standing ones", ("prone_visibility", -1)),
    "hounds_unfair": ("The hounds sniff the squad out wherever it hides", ("smell", -1)),
    "detection_too_sudden": ("Detection comes with no warning; the player cannot react to a patrol's growing suspicion", ("time_to_detect", +1)),
    "search_toothless": ("After a trap goes off the patrols give up the search too soon; traps have no consequence", ("search_seconds", +1)),
    "search_endless": ("After a trap the patrols hunt for so long that the level stalls", ("search_seconds", -1)),
    "stealth_good": ("Sneaking, traps and detections feel tense and fair; no change is needed", None),
}
FALLBACK_ORDER = [("bot_clear_radius", +1), ("enemy_flank_share", +1), ("bot_stop_distance", -1), ("enemy_morale_deaths", +1),
                  ("enemy_focus_cap", -1), ("enemy_preference", +1), ("marksman_aim", +1), ("marksman_damage", -1),
                  ("marksman_accuracy", -1), ("marksman_cooldown", +1), ("bot_clear_radius", -1), ("enemy_flank_share", -1),
                  ("time_to_detect", +1), ("hearing", -1), ("sight_range", -1), ("search_seconds", +1), ("smell", -1)]


# ------------------------------------------------------------------------------------------------- TypeSafe
def api_key():
    key = os.environ.get("TYPESAFE_API_KEY", "").strip()
    if not key and sys.platform == "win32":
        # Set with setx after this shell started: only the registry has it.
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as reg:
                key = str(winreg.QueryValueEx(reg, "TYPESAFE_API_KEY")[0]).strip()
        except OSError:
            pass
    path = os.path.join(ROOT, "Saved", "Config", "typesafe.key")
    if not key and os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            key = f.read().strip()
    return key


def ask_jev(state, questions, attempts=4):
    """One typed request; returns (answers, error). Retries 429 / 529."""
    key = api_key()
    if not key:
        return None, "no TypeSafe API key"
    body = json.dumps({"state": state, "model": MODEL, "questions": questions}).encode("utf-8")
    for attempt in range(attempts):
        req = urllib.request.Request(API_URL, data=body, method="POST",
                                     headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                usage = data.get("usage", {})
                print("[coach] Jev %s: %d questions, %s in / %s out tokens" % (
                    data.get("model"), len(questions), usage.get("input_tokens", "?"), usage.get("output_tokens", "?")), flush=True)
                return data.get("answers", {}), None
        except urllib.error.HTTPError as e:
            if e.code in (429, 529) and attempt + 1 < attempts:
                time.sleep(2 ** attempt)
                continue
            return None, "HTTP %d: %s" % (e.code, e.read().decode("utf-8", "replace")[:300])
        except Exception as e:  # network, timeout
            return None, str(e)
    return None, "retries exhausted"


# ------------------------------------------------------------------------------------------------- batches
def dpcvars(values, file_cvars=None):
    """The batch's console variables: every cvar of the tuning file at the start (the user's hand-set keys included) and
    every knob that differs from its coach default or that the file sets. Knobs at their default that the file does not
    set stay unset (the C++ defaults keep the data / per-level values: -1 / 1)."""
    parts = dict(file_cvars or {})
    for k, v in values.items():
        cvar = KNOBS[k][0]
        if cvar in parts or v != KNOBS[k][1]:
            parts[cvar] = "%g" % v
    return ",".join("%s=%s" % kv for kv in sorted(parts.items()))


def log_path(args, n):
    return os.path.join(ROOT, "Saved", "Logs", "Bot-%s-%d.log" % (args.profile.upper(), n))


def run_batch(args, values):
    # -NoAITuning + the file's cvars spelled out: every batch is measured on exactly these knobs (the file may be edited
    # in the Wave Editor while the coach runs; a batch never picks that up halfway).
    extra = "-NoAITuning -dpcvars=" + dpcvars(values, args.file_cvars)
    if args.level_json:
        extra += " -LevelJson=" + args.level_json
    map_arg = (" -Map '%s'" % args.map.replace("'", "''")) if args.map else ""
    # -Command, not -File: -File passes "-EarlyStop:$false" as a string and the script refuses to start.
    script = os.path.join(ROOT, "Scripts", "bot_run.ps1").replace("'", "''")
    command = "& '%s' -Runs %d -Profile %s -Parallel %d -EarlyStop:$false%s -Extra '%s'" % (
        script, args.runs, args.profile, args.parallel, map_arg, extra.replace("'", "''"))
    print("[coach] batch: " + extra, flush=True)
    started = time.time()
    result = subprocess.run(["powershell", "-ExecutionPolicy", "Bypass", "-Command", command], cwd=ROOT, check=False,
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    fresh = [n for n in range(1, args.runs + 1) if os.path.exists(log_path(args, n)) and os.path.getmtime(log_path(args, n)) >= started]
    if result.returncode != 0 or len(fresh) < args.runs:
        raise SystemExit("[coach] the bot batch did not run (%d of %d fresh logs, exit %d): %s" % (
            len(fresh), args.runs, result.returncode, (result.stderr or result.stdout)[-1500:]))
    args.batch_seconds.append(round(time.time() - started, 1))
    print("[coach] batch done in %.0f s" % args.batch_seconds[-1], flush=True)
    return collect_facts(args)


def collect_facts(args):
    runs = []
    for n in range(1, args.runs + 1):
        path = log_path(args, n)
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        result = re.findall(r"\[Bot\] RESULT (\w+) \(profile \w+, ([\d.]+) s real", text)
        cleared = re.findall(r"\[Bot\] Wave (\d+) cleared", text)
        failed = re.findall(r"Mission failed: (.*)", text)
        shots = re.findall(r"\[Marksman\] \S+ fires at .*?: (\d+) m, chance ([\d.]+), (hit|miss)", text)
        tactics = re.findall(r"\[EnemyTactics\] flank orders (\d+), fallbacks (\d+), backstabs (\d+)", text)
        runs.append({
            "result": result[-1][0] if result else "ERROR",
            "real_seconds": float(result[-1][1]) if result else 0.0,
            "waves_cleared": int(cleared[-1]) if cleared else 0,
            "death": failed[-1].strip()[:120] if failed else "",
            "death_by_freezing": bool(failed) and ("переохлажд" in failed[-1]),
            "marksman_shots": len(shots),
            "marksman_hits": sum(1 for s in shots if s[2] == "hit"),
            "marksman_retreats": len(re.findall(r"\[Marksman\] \S+ retreats", text)),
            "marksmen_seen": len(set(re.findall(r"\[Marksman\] (\S+) ", text))),
            "marksmen_killed": len(re.findall(r"\[Marksman\] \S+ dies at", text)),
            "bot_assaults": len(re.findall(r"\[Bot\] Assault marksman", text)),
            "flank_orders": sum(int(t[0]) for t in tactics),
            "fallbacks": sum(int(t[1]) for t in tactics),
            "backstabs": sum(int(t[2]) for t in tactics),
        })
        runs[-1].update(stealth_facts(text))
    n = max(len(runs), 1)
    wins = sum(1 for r in runs if r["result"] == "VICTORY")
    shots = sum(r["marksman_shots"] for r in runs)
    summary = {
        "runs": len(runs), "victories": wins, "win_rate": round(wins / n, 3),
        "avg_waves_cleared": round(sum(r["waves_cleared"] for r in runs) / n, 2),
        "deaths_by_freezing": sum(1 for r in runs if r["death_by_freezing"]),
        "marksman_hit_rate": round(sum(r["marksman_hits"] for r in runs) / max(shots, 1), 3),
        "marksman_shots_per_run": round(shots / n, 1),
        "marksman_retreats_per_run": round(sum(r["marksman_retreats"] for r in runs) / n, 1),
        "marksman_kill_share": round(sum(r["marksmen_killed"] for r in runs) / max(sum(r["marksmen_seen"] for r in runs), 1), 3),
        "bot_assaults_per_run": round(sum(r["bot_assaults"] for r in runs) / n, 1),
        "flank_orders_per_run": round(sum(r["flank_orders"] for r in runs) / n, 1),
        "fallbacks_per_run": round(sum(r["fallbacks"] for r in runs) / n, 1),
        "backstabs_per_run": round(sum(r["backstabs"] for r in runs) / n, 1),
        "avg_real_seconds": round(sum(r["real_seconds"] for r in runs) / n, 1),
    }
    summary.update(stealth_summary(runs))
    return {"summary": summary, "runs": runs, "words": describe(summary)}


def stealth_facts(text):
    """[Stealth] lines of one run log: EnemyCharacter.cpp (detections by sense, searches) and the sneaking playtest bot
    (PlaytestBotSubsystem: ambush / outcome / trap). Detections count only before the fight started (the outcome time):
    in the fight the bot's own shots and partner alerts are no stealth failures."""
    outcome = re.findall(r"\[Stealth\] outcome (\w+) at ([\d.]+) s \(sneaking ([\d.]+) s", text)
    fight_at = float(outcome[-1][1]) if outcome else None
    ambush = re.findall(r"\[Stealth\] bot ambush at ([\d.]+) s \((in_position|pre_emptive|search_contact|forced)", text)
    detections = re.findall(r"\[Stealth\] detection by (\w+) at ([\d.]+) s \(\S+?(, search found the squad)?\)", text)
    # The bot's own strike: the patrols hear its first shots in the same instant — only earlier detections count.
    if ambush:
        before = [d for d in detections if float(d[1]) < float(ambush[0][0])]
    else:
        before = [d for d in detections if fight_at is None or float(d[1]) <= fight_at + 0.05]
    senses = [d for d in before if d[0] in SENSES]
    searches = re.findall(r"\[Stealth\] search started by \S+ at ([\d.]+) s", text)
    timeouts = re.findall(r"\[Stealth\] search timed out for \S+ at ([\d.]+) s", text)
    return {
        "stealth_bot": bool(re.search(r"\[Stealth\] bot sneaking", text)),
        "stealth_outcome": outcome[-1][0] if outcome else None,
        "fight_start_s": fight_at,
        "sneak_s": float(outcome[-1][2]) if outcome else None,
        "ambush_reason": ambush[0][1] if ambush else None,
        "first_detection_s": min((float(d[1]) for d in senses), default=None),
        "first_detection_sense": min(senses, key=lambda d: float(d[1]))[0] if senses else None,
        "detections_sight": sum(1 for d in senses if d[0] == "sight"),
        "detections_hearing": sum(1 for d in senses if d[0] == "hearing"),
        "detections_smell": sum(1 for d in senses if d[0] == "smell"),
        "searches_started": sum(1 for t in searches if fight_at is None or float(t) <= fight_at),
        "searches_timed_out": sum(1 for t in timeouts if fight_at is None or float(t) <= fight_at),
        "searches_found_squad": sum(1 for d in before if d[2]),
        "traps_laid": len(re.findall(r"\[Stealth\] bot trap laid", text)),
        "bot_stance_changes": len(re.findall(r"\[Stealth\] bot stance", text)),
        "bot_hides": len(re.findall(r"\[Stealth\] bot hide at", text)),
        "bot_cover_moves": len(re.findall(r"\[Stealth\] bot takes cover", text)),
    }


def stealth_summary(runs):
    n = max(len(runs), 1)
    stealth = [r for r in runs if r.get("stealth_bot")]
    m = max(len(stealth), 1)
    detected = [r["first_detection_s"] for r in runs if r["first_detection_s"] is not None]
    outcomes = [r["stealth_outcome"] or "none" for r in stealth]
    sneak = [r["sneak_s"] for r in stealth if r["sneak_s"] is not None]
    return {
        "stealth_runs": len(stealth),
        "stealth_outcomes": {o: outcomes.count(o) for o in sorted(set(outcomes))},
        "stealth_detected_share": round(outcomes.count("detected") / m, 3) if stealth else None,
        "stealth_ambush_share": round(sum(1 for o in outcomes if o.startswith("ambush_") and o != "ambush_forced") / m, 3) if stealth else None,
        "stealth_forced_share": round(outcomes.count("ambush_forced") / m, 3) if stealth else None,
        "avg_sneak_s": round(sum(sneak) / len(sneak), 1) if sneak else None,
        "stealth_runs_detected": len(detected),
        "avg_first_detection_s": round(sum(detected) / len(detected), 1) if detected else None,
        "detections_by_sight_per_run": round(sum(r["detections_sight"] for r in runs) / n, 2),
        "detections_by_hearing_per_run": round(sum(r["detections_hearing"] for r in runs) / n, 2),
        "detections_by_smell_per_run": round(sum(r["detections_smell"] for r in runs) / n, 2),
        "searches_started_per_run": round(sum(r["searches_started"] for r in runs) / n, 2),
        "searches_timed_out_per_run": round(sum(r["searches_timed_out"] for r in runs) / n, 2),
        "searches_found_squad_per_run": round(sum(r["searches_found_squad"] for r in runs) / n, 2),
        "traps_laid_per_run": round(sum(r["traps_laid"] for r in runs) / n, 2),
    }


def bucket(value, edges, words):
    for edge, word in zip(edges, words):
        if value < edge:
            return word
    return words[-1]


def describe(s):
    """The numbers as words: Jev judges words far better than numbers (jev-1.13 jaggedness: keep math in code)."""
    return [
        "The squad won %s (%d of %d runs); on average it got through %s." % (
            bucket(s["win_rate"], [0.05, 0.3, 0.6, 0.9], ["no run", "few runs", "about half the runs", "most runs", "every run"]),
            s["victories"], s["runs"],
            bucket(s["avg_waves_cleared"], [0.5, 1.5, 2.5, 3], ["no wave", "one wave", "two of the three waves", "nearly all waves", "all three waves"])),
        "Defeats by freezing: %s." % bucket(s["deaths_by_freezing"] / max(s["runs"], 1), [0.05, 0.3, 0.6], ["none", "a few", "many", "most"]),
        "Marksmen fired %s per run and %s." % (
            bucket(s["marksman_shots_per_run"], [2, 8, 20, 40], ["almost never", "a few times", "regularly", "very often", "constantly"]),
            bucket(s["marksman_hit_rate"], [0.2, 0.4, 0.6, 0.8], ["rarely hit", "hit sometimes", "hit about half their shots", "hit most shots", "almost never missed"])),
        "Marksmen retreated from the squad %s and the squad killed %s of them." % (
            bucket(s["marksman_retreats_per_run"], [1, 4, 10, 20], ["almost never", "a few times per run", "often", "very often", "constantly"]),
            bucket(s["marksman_kill_share"], [0.1, 0.4, 0.7, 0.95], ["almost none", "under half", "more than half", "most", "all"])),
        "The bot stormed marksmen %s." % bucket(s["bot_assaults_per_run"], [0.5, 2, 6, 15], ["never", "rarely", "a few times per run", "often", "constantly"]),
        "The melee enemies went round the squad's flanks %s, broke off and ran back to their pack %s, and struck operatives "
        "from behind %s." % (
            bucket(s["flank_orders_per_run"], [5, 40, 150, 400], ["almost never", "now and then", "regularly", "very often", "all the time"]),
            bucket(s["fallbacks_per_run"], [1, 5, 15, 35], ["never", "rarely", "now and then", "often", "constantly"]),
            bucket(s["backstabs_per_run"], [0.5, 3, 10, 30], ["never", "rarely", "sometimes", "often", "constantly"])),
        "The fights lasted %s." % bucket(s["avg_real_seconds"], [40, 90, 200, 400], ["very short", "short", "a normal time", "long", "very long"]),
    ] + describe_stealth(s)


def describe_stealth(s):
    """Patrol / stealth facts (only when the map had patrols: [Stealth] lines)."""
    if s.get("avg_first_detection_s") is None and not s.get("searches_started_per_run") and not s.get("stealth_runs"):
        return []
    words = []
    if s.get("stealth_runs"):
        words.append("A sneaking squad crept up on the patrols for %s on average; it struck first from hiding %s, was caught by the "
                     "patrols first %s, and ran out of patience and attacked openly %s." % (
                         bucket(s.get("avg_sneak_s") or 0, [20, 60, 120, 200], ["a few seconds", "under a minute", "a minute or two", "a few minutes", "a long time"]),
                         bucket(s.get("stealth_ambush_share") or 0, [0.05, 0.3, 0.6, 0.9], ["never", "in a few runs", "in about half the runs", "in most runs", "every time"]),
                         bucket(s.get("stealth_detected_share") or 0, [0.05, 0.3, 0.6, 0.9], ["never", "in a few runs", "in about half the runs", "in most runs", "every time"]),
                         bucket(s.get("stealth_forced_share") or 0, [0.05, 0.3, 0.6], ["never", "rarely", "often", "usually"])))
    if s.get("avg_first_detection_s") is not None:
        words.append("Before the fight the patrols first detected the squad %s (in %d of %d runs); detections came by sight %s, by hearing %s, by smell %s." % (
            bucket(s["avg_first_detection_s"], [10, 30, 90, 180], ["almost at once", "quickly", "after a while", "late", "very late"]),
            s["stealth_runs_detected"], s["runs"],
            bucket(s["detections_by_sight_per_run"], [0.2, 1, 3], ["never", "rarely", "sometimes", "often"]),
            bucket(s["detections_by_hearing_per_run"], [0.2, 1, 3], ["never", "rarely", "sometimes", "often"]),
            bucket(s["detections_by_smell_per_run"], [0.2, 1, 3], ["never", "rarely", "sometimes", "often"])))
    else:
        words.append("The patrols never detected the sneaking squad before the fight.")
    words.append("Traps sent patrols searching %s; searches %s found the squad and %s gave up." % (
        bucket(s.get("searches_started_per_run", 0), [0.2, 1, 3], ["never", "rarely", "sometimes", "often"]),
        bucket(s.get("searches_found_squad_per_run", 0), [0.2, 1, 3], ["never", "rarely", "sometimes", "often"]),
        bucket(s.get("searches_timed_out_per_run", 0), [0.2, 1, 3], ["never", "rarely", "sometimes", "often"])))
    return words


def build_questions(stealth=False):
    intro = ("The `fights` were played by a scripted bot commanding a 3-operative squad (rifles reach 14 m) against "
             "waves of melee enemies (hounds, cutters, frostbitten, a brute) and marksmen (long-range snipers, 20-35 m).")
    questions = {
        "bot_weakness": {"type": "choice", "instructions": intro + " Which bot behaviour most explains the defeats?",
                         "criteria": {k: v[0] for k, v in BOT_OPTIONS.items()}},
        "enemy_ai_issue": {"type": "choice",
                           "instructions": intro + " Judging only how the melee enemies move and choose targets: which "
                                                   "change would make them the most interesting opponents?",
                           "criteria": {k: v[0] for k, v in ENEMY_AI_OPTIONS.items()}},
        "enemy_issue": {"type": "choice",
                        "instructions": intro + " Judging only the marksmen: which marksman behaviour makes the fight most unfair?",
                        "criteria": {k: v[0] for k, v in ENEMY_OPTIONS.items()}},
        "fairness": {"type": "score", "instructions": intro + " How fair is this encounter for a skilled player?",
                     "criteria": ["Hopeless: the squad dies whatever it does", "Very hard: wins are rare flukes",
                                  "Hard but fair: good tactics win", "Comfortable: most attempts win", "Trivial: no threat"]},
        "engagement": {"type": "score",
                       "instructions": intro + " How tactically interesting are these enemies to fight for a player?",
                       "criteria": ["Dull: they stream straight in and die, nothing to think about",
                                    "Simple: a little variety, one tactic beats them",
                                    "Interesting: flanks, focus and retreats force the player to adapt",
                                    "Gripping: the enemies feel coordinated and cunning yet beatable",
                                    "Exhausting: so erratic or relentless that it stops being fun"]},
        "stealth_issue": {"type": "choice",
                          "instructions": intro + " On maps with patrols the squad can sneak, lay traps and pick the moment "
                                                  "of the fight. Judging only the patrols' sight / hearing / smell and their "
                                                  "search after a trap: which change would make sneaking most tense and fair?",
                          "criteria": {k: v[0] for k, v in STEALTH_OPTIONS.items()}},
        "avoidable": {"type": "noul",
                      "instructions": intro + " Could better squad tactics (cover, storming, focus fire) have avoided most of these defeats?"},
    }
    if stealth:
        questions["stealth_tension"] = {
            "type": "score",
            "instructions": intro + " Before the fight the squad sneaked past patrols (crouching, crawling, hiding behind walls) "
                                    "and chose when to strike. How does sneaking past these patrols feel for a player?",
            "criteria": ["Pointless: the patrols notice everything at once, sneaking never works",
                         "Frustrating: sneaking rarely works and detection feels random",
                         "Tense and fair: careful sneaking usually works, carelessness gets the squad caught",
                         "Easy: the patrols catch only obvious blunders",
                         "Trivial: the patrols are blind and deaf"]}
    return questions


def judge(facts, stealth=False):
    state = {"fights": facts["words"]}
    answers, error = ask_jev(state, build_questions(stealth))
    facts["jev"], facts["jev_error"] = answers, error
    return answers


def engagement(answers):
    """Jev's engagement score as 0..1, peaking at «Gripping» (3) and penalising «Exhausting» (4)."""
    if not answers or "engagement" not in answers:
        return 0.5
    value = answers["engagement"]["score"]
    return max(0.0, 1.0 - abs(value - 3.0) / 3.0)


def stealth_tension(answers):
    """Jev's stealth tension score as 0..1, peaking at «Tense and fair» (2)."""
    if not answers or "stealth_tension" not in answers:
        return 0.5
    value = answers["stealth_tension"]["score"]
    return max(0.0, 1.0 - abs(value - 2.0) / 2.0)


def band_distance(value, band):
    lo, hi = band
    return 0.0 if lo <= value <= hi else min(abs(value - lo), abs(value - hi))


def objective(facts, owner):
    s = facts["summary"]
    if owner == "bot":
        return s["win_rate"] + 0.05 * s["avg_waves_cleared"]
    distance = band_distance(s["win_rate"], WIN_BAND)
    if owner == "stealth":
        # The fight after the ambush barely depends on the perception knobs: its win rate only weighs a little.
        first = s.get("avg_first_detection_s")
        dlo, dhi = STEALTH_DETECT_BAND
        timing = 0.0 if first is None else max(0.0, dlo - first) / 60.0 + max(0.0, first - dhi) / 60.0
        share = s.get("stealth_detected_share")
        if share is None:  # no sneaking bot in these logs (an old build): the first detection alone, never = far edge
            share_penalty = 0.5 if first is None else 0.0
        else:
            share_penalty = 2.0 * band_distance(share, STEALTH_DETECTED_SHARE_BAND)
        return 1.0 - 0.5 * distance - share_penalty - timing + 0.3 * stealth_tension(facts.get("jev"))
    return 1.0 - 2.0 * distance + 0.3 * engagement(facts.get("jev"))


def choose_step(answers, facts, values, tried, args):
    """(knob, direction, new value, reason): Jev's confident picks first, else the fixed order."""
    candidates = []
    if answers:
        for qid, options in (("bot_weakness", BOT_OPTIONS), ("enemy_ai_issue", ENEMY_AI_OPTIONS), ("enemy_issue", ENEMY_OPTIONS),
                             ("stealth_issue", STEALTH_OPTIONS)):
            ans = answers.get(qid) or {}
            for option, p in sorted((ans.get("probabilities") or {}).items(), key=lambda kv: -kv[1]):
                step = (options.get(option) or (None, None))[1]
                if step and ans.get("confidence", 0) >= CONFIDENCE_GATE:
                    candidates.append((p, step[0], step[1], "jev:%s=%s (p %.2f, conf %.2f)" % (qid, option, p, ans.get("confidence", 0))))
        candidates.sort(key=lambda c: -c[0])
    candidates += [(0, k, d, "fallback order") for k, d in FALLBACK_ORDER]
    for _, knob, direction, reason in candidates:
        owner = KNOBS[knob][5]
        if (owner == "enemy" and not args.tune_enemies) or (owner == "enemy_ai" and args.no_enemy_ai) \
                or (owner == "stealth" and not args.stealth):
            continue
        # A stealth run tunes the patrol knobs only (the fight knobs have their own runs) unless asked otherwise.
        if args.stealth and owner != "stealth" and not args.with_combat_knobs:
            continue
        # No trap search ever happened in these runs: a search knob would only measure noise.
        if knob in ("search_seconds", "search_radius") and not facts["summary"].get("searches_started_per_run"):
            continue
        # Hand-set keys (Wave Editor) stay as the user set them.
        if KNOBS[knob][0] in args.user_keys and not args.tune_user_keys:
            continue
        if (knob, direction) in tried:
            continue
        _, _, lo, hi, step, _ = KNOBS[knob]
        new = round(min(hi, max(lo, values[knob] + direction * step)), 3)
        if new != values[knob]:
            return knob, direction, new, reason
    return None


def enemy_proposals(answers):
    out = []
    for qid, options in (("enemy_issue", ENEMY_OPTIONS), ("enemy_ai_issue", ENEMY_AI_OPTIONS)):
        ans = (answers or {}).get(qid) or {}
        for option, p in sorted((ans.get("probabilities") or {}).items(), key=lambda kv: -kv[1])[:2]:
            step = (options.get(option) or (None, None))[1]
            if step:
                out.append({"issue": option, "probability": round(p, 3), "knob": KNOBS[step[0]][0],
                            "direction": "raise" if step[1] > 0 else "lower"})
    return out


# ------------------------------------------------------------------------------------------------- ai_tuning.json
def load_tuning(path=TUNING_FILE):
    if not os.path.exists(path):
        return {}
    with open(path, encoding="utf-8") as f:
        doc = json.load(f)
    return doc if isinstance(doc, dict) else {}


def tuning_cvars(doc):
    cvars = doc.get("cvars")
    return {str(k): str(v) for k, v in cvars.items()} if isinstance(cvars, dict) else {}


def user_set_keys(doc):
    """cvars the user set by hand: every key not recorded as the coach's own ("coach_cvars", same value). A file from
    the old coach (no "coach_cvars") owned only its non-stealth knobs — the Wave Editor's stealth keys are the user's."""
    cvars = tuning_cvars(doc)
    coach = doc.get("coach_cvars")
    if isinstance(coach, dict):
        return sorted(k for k, v in cvars.items() if str(coach.get(k)) != str(v))
    legacy_coach = str(doc.get("comment", "")).startswith(COACH_COMMENT)
    coach_knobs = {spec[0] for spec in KNOBS.values() if spec[5] != "stealth"}
    return sorted(k for k in cvars if not (legacy_coach and k in coach_knobs))


def knob_values_from(cvars):
    """Knob values in force: the file's value where it sets the knob, else the coach default."""
    values = {}
    for k, spec in KNOBS.items():
        try:
            values[k] = float(cvars[spec[0]]) if spec[0] in cvars else spec[1]
        except ValueError:
            values[k] = spec[1]
    return values


def merge_tuning(doc, changes, user_keys, summary=None, profile="", source=""):
    """ai_tuning.json with the coach's changes ({cvar: value string, or None = remove}) merged in: every other key (the
    Wave Editor's hand-set cvars, unknown keys) is kept, a user key is never touched. Returns (doc, applied, skipped)."""
    out = dict(doc)
    cvars = tuning_cvars(doc)
    coach = dict(doc.get("coach_cvars") or {}) if isinstance(doc.get("coach_cvars"), dict) else {
        k: v for k, v in cvars.items() if k not in user_keys}
    applied, skipped = {}, {}
    for cvar, value in sorted(changes.items()):
        if cvar in user_keys:
            skipped[cvar] = value
            continue
        if value is None:
            cvars.pop(cvar, None)
            coach.pop(cvar, None)
        else:
            cvars[cvar] = value
            coach[cvar] = value
        applied[cvar] = value
    out["comment"] = (COACH_COMMENT + " (merged; the Wave Editor's keys kept) and the Wave Editor «Скрытность и бой» tab; "
                      "applied at game start (Data/AITuning.h), a -dpcvars= value wins, -NoAITuning skips it. "
                      "\"coach_cvars\" = the keys the coach set (any other key is the user's and never overwritten).")
    out["cvars"] = dict(sorted(cvars.items()))
    out["coach_cvars"] = dict(sorted(coach.items()))
    out["coach_updated"] = datetime.datetime.now().isoformat(timespec="seconds")
    if source:
        out["coach_source"] = source
    if profile:
        out["profile"] = profile
    if summary is not None:
        out["result"] = summary
    return out, applied, skipped


def write_tuning_doc(doc, path=TUNING_FILE):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(doc, f, ensure_ascii=False, indent=2)
        f.write("\n")
    print("[coach] wrote " + path)


def proposed_changes(values, start_values, args):
    """{cvar: value} the run proposes: knobs that moved from the start (a knob back at its default with the file not
    setting it is removed)."""
    changes = {}
    for k, v in sorted(values.items()):
        if v == start_values[k]:
            continue
        owner = KNOBS[k][5]
        if (owner == "enemy" and not args.tune_enemies) or (owner == "stealth" and not args.stealth):
            continue
        changes[KNOBS[k][0]] = None if (v == KNOBS[k][1] and KNOBS[k][0] not in args.file_cvars) else "%g" % v
    return changes


def jev_digest(answers):
    """The final Jev judgments, short: choice + confidence + the top probabilities per question."""
    out = {}
    for qid, a in (answers or {}).items():
        if not isinstance(a, dict):
            continue
        entry = {k: a[k] for k in ("choice", "confidence", "score", "noul") if k in a}
        probs = a.get("probabilities") or {}
        if probs:
            entry["top"] = {k: round(v, 3) for k, v in sorted(probs.items(), key=lambda kv: -kv[1])[:3]}
        out[qid] = entry
    return out


def write_proposal(args, stamp, out_dir, baseline, final, start_values, values, history, owner_objective):
    os.makedirs(PROPOSAL_DIR, exist_ok=True)
    mode = "stealth" if args.stealth else "ai"
    path = os.path.join(PROPOSAL_DIR, "%s_proposal.json" % mode)
    changes = proposed_changes(values, start_values, args)
    rows = []
    for k, spec in KNOBS.items():
        if values[k] == start_values[k] and spec[0] not in args.file_cvars:
            continue
        rows.append({"cvar": spec[0], "owner": spec[5], "default": spec[1], "current": args.file_cvars.get(spec[0]),
                     "proposed": changes[spec[0]] if spec[0] in changes else args.file_cvars.get(spec[0]),
                     "changed": spec[0] in changes, "user_set": spec[0] in args.user_keys})
    proposal = {
        "created": datetime.datetime.now().isoformat(timespec="seconds"), "mode": mode, "map": args.map, "profile": args.profile,
        "runs_per_batch": args.runs, "iterations": args.iterations, "parallel": args.parallel, "report": out_dir,
        "batch_seconds": args.batch_seconds, "objective": owner_objective,
        "baseline": baseline["summary"], "final": final["summary"], "words_final": final["words"],
        "tuning_file": os.path.relpath(TUNING_FILE, ROOT), "file_cvars_at_start": args.file_cvars, "user_set_keys": args.user_keys,
        "changes": changes, "knobs": rows,
        "jev_final": jev_digest(final.get("jev")), "jev_error": final.get("jev_error"),
        "steps": [{"iteration": h["iteration"], **h["step"]} if isinstance(h.get("step"), dict) else {"iteration": h["iteration"], "step": h.get("step")}
                  for h in history],
        "apply_with": "python Scripts/Tools/jev_ai_coach.py --apply-proposal %s" % os.path.relpath(path, ROOT).replace("\\", "/"),
        "applied": False,
    }
    for h in proposal["steps"]:
        h.pop("trial_summary", None)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(proposal, f, ensure_ascii=False, indent=2)
    md = [
        "# Jev AI coach proposal (%s) - %s" % (mode, stamp), "",
        "NOT applied. Review, then `%s` (keeps every other key of ai_tuning.json; keys you set by hand are never touched)." % proposal["apply_with"], "",
        "Map %s, profile %s, %d runs x %d iterations, batches %s s." % (args.map or "(default)", args.profile, args.runs, args.iterations,
                                                                       "/".join("%.0f" % b for b in args.batch_seconds)), "",
        "| cvar | default | ai_tuning.json now | proposed | user-set |", "|---|---|---|---|---|"]
    for r in rows:
        md.append("| %s | %g | %s | %s | %s |" % (r["cvar"], r["default"], r["current"] or "-", r["proposed"] or "(removed / default)" if r["changed"] else (r["current"] or "-"),
                                                "yes" if r["user_set"] else "no"))
    if not changes:
        md += ["", "No knob beat the starting values by the noise margin: nothing to change."]
    b, f = baseline["summary"], final["summary"]
    md += ["", "| metric | start | proposed |", "|---|---|---|"]
    for key in ("win_rate", "avg_waves_cleared", "stealth_detected_share", "stealth_ambush_share", "stealth_forced_share", "avg_sneak_s",
                "avg_first_detection_s", "detections_by_sight_per_run", "detections_by_hearing_per_run", "detections_by_smell_per_run",
                "searches_started_per_run", "searches_timed_out_per_run", "searches_found_squad_per_run", "traps_laid_per_run"):
        if key in b or key in f:
            md.append("| %s | %s | %s |" % (key, b.get(key), f.get(key)))
    md += ["", "Outcomes start %s -> proposed %s." % (b.get("stealth_outcomes"), f.get("stealth_outcomes")), "", "Jev (final batch):"]
    for qid, a in jev_digest(final.get("jev")).items():
        md.append("- %s: %s" % (qid, json.dumps(a, ensure_ascii=False)))
    if final.get("jev_error"):
        md.append("- unavailable: %s" % final["jev_error"])
    md += ["", "Steps:"]
    for st in proposal["steps"]:
        if "knob" in st:
            md.append("- it %d: %s %g -> %g (%s): objective %.3f -> %.3f, %s" % (st["iteration"], st["knob"], st["from"], st["to"], st["reason"],
                                                                               st["objective_before"], st["objective_after"], "KEPT" if st["kept"] else "reverted"))
        else:
            md.append("- it %d: %s" % (st["iteration"], st.get("step")))
    with open(path[:-5] + ".md", "w", encoding="utf-8") as f2:
        f2.write("\n".join(md) + "\n")
    print("[coach] proposal (not applied): " + path)
    return proposal


def apply_proposal(path, force_keys=False):
    with open(path, encoding="utf-8") as f:
        proposal = json.load(f)
    doc = load_tuning()
    current = tuning_cvars(doc)
    users = set(user_set_keys(doc))
    # A key the user changed since the run (its value is no longer the one the run started from) is skipped too.
    started = proposal.get("file_cvars_at_start") or {}
    for cvar in proposal.get("changes", {}):
        if current.get(cvar) != started.get(cvar):
            users.add(cvar)
    if force_keys:
        users = set()
    merged, applied, skipped = merge_tuning(doc, proposal.get("changes", {}), users, summary=proposal.get("final"),
                                            profile=proposal.get("profile", ""), source=os.path.relpath(path, ROOT))
    if applied:
        write_tuning_doc(merged)
    proposal["applied"] = datetime.datetime.now().isoformat(timespec="seconds") if applied else False
    with open(path, "w", encoding="utf-8") as f:
        json.dump(proposal, f, ensure_ascii=False, indent=2)
    print("[coach] applied %s; kept the user's %s" % (applied or "nothing", skipped or "-"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iterations", type=int, default=6)
    ap.add_argument("--runs", type=int, default=8)
    ap.add_argument("--profile", default="VETERAN")
    ap.add_argument("--parallel", type=int, default=4)
    ap.add_argument("--level-json", default="")
    ap.add_argument("--tune-enemies", action="store_true", help="also tune enemy power (balance) knobs")
    ap.add_argument("--no-enemy-ai", action="store_true", help="leave the enemy intelligence knobs alone")
    ap.add_argument("--dry-run", action="store_true", help="analyse the last batch's logs once, no new runs")
    ap.add_argument("--stealth", action="store_true", help="also tune the patrol perception / trap search knobs (Codex.Perception.*, Codex.Patrol.*)")
    ap.add_argument("--map", default="", help="map for the bot batches (bot_run.ps1 -Map); --stealth defaults to /Game/Maps/L_PatrolTest")
    ap.add_argument("--with-combat-knobs", action="store_true", help="with --stealth: also step the bot / enemy-AI knobs")
    ap.add_argument("--tune-user-keys", action="store_true", help="also try knobs the user set by hand in ai_tuning.json (still only proposed)")
    ap.add_argument("--apply", action="store_true", help="merge the result into ai_tuning.json at the end (user keys kept); default: proposal only")
    ap.add_argument("--apply-proposal", default="", help="merge a proposal file into ai_tuning.json and exit")
    ap.add_argument("--force-keys", action="store_true", help="with --apply-proposal: also overwrite keys the user changed")
    args = ap.parse_args()
    if args.apply_proposal:
        apply_proposal(os.path.join(ROOT, args.apply_proposal) if not os.path.isabs(args.apply_proposal) else args.apply_proposal, args.force_keys)
        return
    if args.stealth and not args.map:
        args.map = "/Game/Maps/L_PatrolTest"

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = os.path.join(ROOT, "Saved", "Telemetry", "ai_coach", stamp)
    os.makedirs(out_dir, exist_ok=True)
    # The current tuning file is the starting point; the user's hand-set keys are frozen and never overwritten.
    tuning_doc = load_tuning()
    args.file_cvars = tuning_cvars(tuning_doc)
    args.user_keys = user_set_keys(tuning_doc)
    args.batch_seconds = []
    values = knob_values_from(args.file_cvars)
    start_values = dict(values)
    print("[coach] start from %s: %d cvars, user-set (kept) %s" % (os.path.relpath(TUNING_FILE, ROOT), len(args.file_cvars),
                                                                  args.user_keys or "-"), flush=True)
    history, tried = [], set()
    run_started = time.time()

    facts = collect_facts(args) if args.dry_run else run_batch(args, values)
    judge(facts, args.stealth)
    baseline = facts
    for it in range(1, (1 if args.dry_run else args.iterations) + 1):
        s, answers = facts["summary"], facts.get("jev")
        entry = {"iteration": it, "values": dict(values), "summary": s, "words": facts["words"], "jev": answers,
                 "jev_error": facts.get("jev_error")}
        print("[coach] it %d: win %.0f%%, waves %.2f, flanks %.0f / fallbacks %.0f / backstabs %.0f per run%s" % (
            it, 100 * s["win_rate"], s["avg_waves_cleared"], s["flank_orders_per_run"], s["fallbacks_per_run"],
            s["backstabs_per_run"], "" if answers else " (Jev unavailable: %s)" % facts.get("jev_error")), flush=True)
        if args.stealth:
            print("        stealth: outcomes %s, first detection %s s, sneaking %s s, searches %.2f / run, traps %.2f / run" % (
                s.get("stealth_outcomes"), s.get("avg_first_detection_s"), s.get("avg_sneak_s"), s.get("searches_started_per_run", 0),
                s.get("traps_laid_per_run", 0)), flush=True)
        if answers:
            for qid in ("bot_weakness", "enemy_ai_issue", "enemy_issue") + (("stealth_issue",) if args.stealth else ()):
                a = answers.get(qid, {})
                print("        %s -> %s (conf %.2f)" % (qid, a.get("choice"), a.get("confidence", 0)), flush=True)
            print("        fairness %.2f / 4, engagement %.2f / 4, avoidable %.2f%s" % (
                (answers.get("fairness") or {}).get("score", -1), (answers.get("engagement") or {}).get("score", -1),
                (answers.get("avoidable") or {}).get("noul", -1),
                ", stealth tension %.2f / 4" % (answers.get("stealth_tension") or {}).get("score", -1) if args.stealth else ""), flush=True)
        entry["enemy_proposals"] = enemy_proposals(answers)
        if args.dry_run:
            history.append(entry)
            break
        step = choose_step(answers, facts, values, tried, args)
        if not step:
            entry["step"] = "no untried step left"
            history.append(entry)
            break
        knob, direction, new, reason = step
        owner = KNOBS[knob][5]
        trial = dict(values)
        trial[knob] = new
        trial_facts = run_batch(args, trial)
        judge(trial_facts, args.stealth)
        before, after = objective(facts, owner), objective(trial_facts, owner)
        gained = after > before + NOISE_MARGIN
        entry["step"] = {"knob": KNOBS[knob][0], "owner": owner, "from": values[knob], "to": new, "reason": reason,
                         "objective_before": round(before, 3), "objective_after": round(after, 3), "kept": gained,
                         "trial_summary": trial_facts["summary"]}
        print("[coach]   %s %g -> %g (%s): %s objective %.3f -> %.3f, %s" % (
            KNOBS[knob][0], values[knob], new, reason, owner, before, after, "KEPT" if gained else "reverted"), flush=True)
        history.append(entry)
        if gained:
            values, facts = trial, trial_facts
            tried = {(k, d) for k, d in tried if k != knob}
        else:
            tried.add((knob, direction))

    main_owner = "stealth" if args.stealth else "bot"
    owner_objective = {"owner": main_owner, "baseline": round(objective(baseline, main_owner), 3), "final": round(objective(facts, main_owner), 3)}
    if not args.dry_run:
        # The best set is a PROPOSAL (user plan 2026-10-07): the user approves it; --apply merges it now.
        proposal = write_proposal(args, stamp, out_dir, baseline, facts, start_values, values, history, owner_objective)
        if args.apply and proposal["changes"]:
            merged, applied, skipped = merge_tuning(load_tuning(), proposal["changes"], set(args.user_keys), summary=facts["summary"],
                                                    profile=args.profile, source="run " + stamp)
            if applied:
                write_tuning_doc(merged)
            print("[coach] --apply: applied %s; kept the user's %s" % (applied or "nothing", skipped or "-"))
    print("[coach] total %.0f s (batches %s s)" % (time.time() - run_started, args.batch_seconds), flush=True)
    report = {"profile": args.profile, "map": args.map, "runs_per_batch": args.runs, "win_band": WIN_BAND, "tune_enemies": args.tune_enemies,
              "stealth": args.stealth, "objective": owner_objective, "batch_seconds": args.batch_seconds,
              "user_set_keys": args.user_keys, "file_cvars_at_start": args.file_cvars,
              "baseline": baseline["summary"], "final": facts["summary"], "final_dpcvars": dpcvars(values, args.file_cvars),
              "final_values": {KNOBS[k][0]: v for k, v in values.items()}, "history": history}
    with open(os.path.join(out_dir, "report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=2)
    lines = ["# Jev AI coach - %s" % stamp, "",
             "Profile %s, %d runs per batch, win band %d-%d %%, enemy power tuned: %s." % (
                 args.profile, args.runs, WIN_BAND[0] * 100, WIN_BAND[1] * 100, args.tune_enemies), "",
             "Baseline win %.0f %% -> final %.0f %%: `-dpcvars=%s`" % (
                 100 * baseline["summary"]["win_rate"], 100 * facts["summary"]["win_rate"], dpcvars(values, args.file_cvars)), "",
             "| It | Win | Waves | Step | Objective | Kept | Jev bot / enemy AI / marksman |", "|---|---|---|---|---|---|---|"]
    for h in history:
        s, st, j = h["summary"], h.get("step"), h.get("jev") or {}
        step_text = "%s %g->%g (%s)" % (st["knob"], st["from"], st["to"], st["reason"]) if isinstance(st, dict) else str(st)
        objective_text = "%.2f->%.2f" % (st["objective_before"], st["objective_after"]) if isinstance(st, dict) else "-"
        lines.append("| %d | %.0f%% | %.2f | %s | %s | %s | %s / %s / %s |" % (
            h["iteration"], 100 * s["win_rate"], s["avg_waves_cleared"], step_text, objective_text,
            st.get("kept") if isinstance(st, dict) else "-", (j.get("bot_weakness") or {}).get("choice", "-"),
            (j.get("enemy_ai_issue") or {}).get("choice", "-"), (j.get("enemy_issue") or {}).get("choice", "-")))
    proposals = history[-1].get("enemy_proposals") if history else []
    if proposals:
        lines += ["", "Enemy proposals for the user (not applied):"] + [
            "- %s: %s %s (p %.2f)" % (p["issue"], p["direction"], p["knob"], p["probability"]) for p in proposals]
    with open(os.path.join(out_dir, "report.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("[coach] report: " + os.path.join(out_dir, "report.md"))
    print("[coach] final: -dpcvars=" + dpcvars(values, args.file_cvars))


if __name__ == "__main__":
    main()
