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

Runs are deterministic (fixed step): equal knobs give equal results, so a kept step is a real effect on this level.

Usage:
  python Scripts/Tools/jev_ai_coach.py [--iterations 6] [--runs 8] [--profile VETERAN] [--parallel 4]
                                        [--level-json <path>] [--tune-enemies] [--no-enemy-ai] [--dry-run]
Output: Saved/Telemetry/ai_coach/<timestamp>/report.json and report.md; the best bot + enemy-intelligence knobs (and
enemy power knobs with --tune-enemies) go to Content/Data/AI/ai_tuning.json, applied at game start (Data/AITuning.h),
only when they beat the defaults. The API key: TYPESAFE_API_KEY, the Windows user environment (registry) or
Saved/Config/typesafe.key (never printed). No key / API error -> the fixed knob order, and the report says so.
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
FALLBACK_ORDER = [("bot_clear_radius", +1), ("enemy_flank_share", +1), ("bot_stop_distance", -1), ("enemy_morale_deaths", +1),
                  ("enemy_focus_cap", -1), ("enemy_preference", +1), ("marksman_aim", +1), ("marksman_damage", -1),
                  ("marksman_accuracy", -1), ("marksman_cooldown", +1), ("bot_clear_radius", -1), ("enemy_flank_share", -1)]


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
def dpcvars(values):
    return ",".join("%s=%g" % (KNOBS[k][0], v) for k, v in sorted(values.items()))


def log_path(args, n):
    return os.path.join(ROOT, "Saved", "Logs", "Bot-%s-%d.log" % (args.profile.upper(), n))


def run_batch(args, values):
    # -NoAITuning: every batch is measured on these knobs alone, not on top of the last written winner.
    extra = "-NoAITuning -dpcvars=" + dpcvars(values)
    if args.level_json:
        extra += " -LevelJson=" + args.level_json
    # -Command, not -File: -File passes "-EarlyStop:$false" as a string and the script refuses to start.
    script = os.path.join(ROOT, "Scripts", "bot_run.ps1").replace("'", "''")
    command = "& '%s' -Runs %d -Profile %s -Parallel %d -EarlyStop:$false -Extra '%s'" % (
        script, args.runs, args.profile, args.parallel, extra.replace("'", "''"))
    print("[coach] batch: " + extra, flush=True)
    started = time.time()
    result = subprocess.run(["powershell", "-ExecutionPolicy", "Bypass", "-Command", command], cwd=ROOT, check=False,
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    fresh = [n for n in range(1, args.runs + 1) if os.path.exists(log_path(args, n)) and os.path.getmtime(log_path(args, n)) >= started]
    if result.returncode != 0 or len(fresh) < args.runs:
        raise SystemExit("[coach] the bot batch did not run (%d of %d fresh logs, exit %d): %s" % (
            len(fresh), args.runs, result.returncode, (result.stderr or result.stdout)[-1500:]))
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
    return {"summary": summary, "runs": runs, "words": describe(summary)}


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
    ]


def build_questions():
    intro = ("The `fights` were played by a scripted bot commanding a 3-operative squad (rifles reach 14 m) against "
             "waves of melee enemies (hounds, cutters, frostbitten, a brute) and marksmen (long-range snipers, 20-35 m).")
    return {
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
        "avoidable": {"type": "noul",
                      "instructions": intro + " Could better squad tactics (cover, storming, focus fire) have avoided most of these defeats?"},
    }


def judge(facts):
    state = {"fights": facts["words"]}
    answers, error = ask_jev(state, build_questions())
    facts["jev"], facts["jev_error"] = answers, error
    return answers


def engagement(answers):
    """Jev's engagement score as 0..1, peaking at «Gripping» (3) and penalising «Exhausting» (4)."""
    if not answers or "engagement" not in answers:
        return 0.5
    value = answers["engagement"]["score"]
    return max(0.0, 1.0 - abs(value - 3.0) / 3.0)


def objective(facts, owner):
    s = facts["summary"]
    if owner == "bot":
        return s["win_rate"] + 0.05 * s["avg_waves_cleared"]
    lo, hi = WIN_BAND
    distance = 0.0 if lo <= s["win_rate"] <= hi else min(abs(s["win_rate"] - lo), abs(s["win_rate"] - hi))
    return 1.0 - 2.0 * distance + 0.3 * engagement(facts.get("jev"))


def choose_step(answers, facts, values, tried, args):
    """(knob, direction, new value, reason): Jev's confident picks first, else the fixed order."""
    candidates = []
    if answers:
        for qid, options in (("bot_weakness", BOT_OPTIONS), ("enemy_ai_issue", ENEMY_AI_OPTIONS), ("enemy_issue", ENEMY_OPTIONS)):
            ans = answers.get(qid) or {}
            for option, p in sorted((ans.get("probabilities") or {}).items(), key=lambda kv: -kv[1]):
                step = (options.get(option) or (None, None))[1]
                if step and ans.get("confidence", 0) >= CONFIDENCE_GATE:
                    candidates.append((p, step[0], step[1], "jev:%s=%s (p %.2f, conf %.2f)" % (qid, option, p, ans.get("confidence", 0))))
        candidates.sort(key=lambda c: -c[0])
    candidates += [(0, k, d, "fallback order") for k, d in FALLBACK_ORDER]
    for _, knob, direction, reason in candidates:
        owner = KNOBS[knob][5]
        if (owner == "enemy" and not args.tune_enemies) or (owner == "enemy_ai" and args.no_enemy_ai):
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


def write_tuning(best_values, summary, args):
    """Only knobs that differ from their defaults; enemy power knobs only when they were tuned on purpose."""
    cvars = {KNOBS[k][0]: "%g" % v for k, v in sorted(best_values.items())
             if v != KNOBS[k][1] and (KNOBS[k][5] != "enemy" or args.tune_enemies)}
    if not cvars:
        return
    os.makedirs(os.path.dirname(TUNING_FILE), exist_ok=True)
    data = {"comment": "Written by Scripts/Tools/jev_ai_coach.py; applied at game start (Data/AITuning.h), a -dpcvars= "
                       "value wins, -NoAITuning skips it. Review before committing.",
            "updated": datetime.datetime.now().isoformat(timespec="seconds"), "profile": args.profile,
            "runs_per_batch": args.runs, "win_band": list(WIN_BAND), "result": summary, "cvars": cvars}
    with open(TUNING_FILE, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    print("[coach] wrote " + TUNING_FILE)


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
    args = ap.parse_args()

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = os.path.join(ROOT, "Saved", "Telemetry", "ai_coach", stamp)
    os.makedirs(out_dir, exist_ok=True)
    values = {k: v[1] for k, v in KNOBS.items()}
    history, tried = [], set()

    facts = collect_facts(args) if args.dry_run else run_batch(args, values)
    judge(facts)
    baseline = facts
    for it in range(1, (1 if args.dry_run else args.iterations) + 1):
        s, answers = facts["summary"], facts.get("jev")
        entry = {"iteration": it, "values": dict(values), "summary": s, "words": facts["words"], "jev": answers,
                 "jev_error": facts.get("jev_error")}
        print("[coach] it %d: win %.0f%%, waves %.2f, flanks %.0f / fallbacks %.0f / backstabs %.0f per run%s" % (
            it, 100 * s["win_rate"], s["avg_waves_cleared"], s["flank_orders_per_run"], s["fallbacks_per_run"],
            s["backstabs_per_run"], "" if answers else " (Jev unavailable: %s)" % facts.get("jev_error")), flush=True)
        if answers:
            for qid in ("bot_weakness", "enemy_ai_issue", "enemy_issue"):
                a = answers.get(qid, {})
                print("        %s -> %s (conf %.2f)" % (qid, a.get("choice"), a.get("confidence", 0)), flush=True)
            print("        fairness %.2f / 4, engagement %.2f / 4, avoidable %.2f" % (
                (answers.get("fairness") or {}).get("score", -1), (answers.get("engagement") or {}).get("score", -1),
                (answers.get("avoidable") or {}).get("noul", -1)), flush=True)
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
        judge(trial_facts)
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

    changed = any(values[k] != KNOBS[k][1] for k in values)
    if not args.dry_run and changed:
        write_tuning(values, facts["summary"], args)
    report = {"profile": args.profile, "runs_per_batch": args.runs, "win_band": WIN_BAND, "tune_enemies": args.tune_enemies,
              "baseline": baseline["summary"], "final": facts["summary"], "final_dpcvars": dpcvars(values),
              "final_values": {KNOBS[k][0]: v for k, v in values.items()}, "history": history}
    with open(os.path.join(out_dir, "report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=2)
    lines = ["# Jev AI coach - %s" % stamp, "",
             "Profile %s, %d runs per batch, win band %d-%d %%, enemy power tuned: %s." % (
                 args.profile, args.runs, WIN_BAND[0] * 100, WIN_BAND[1] * 100, args.tune_enemies), "",
             "Baseline win %.0f %% -> final %.0f %%: `-dpcvars=%s`" % (
                 100 * baseline["summary"]["win_rate"], 100 * facts["summary"]["win_rate"], dpcvars(values)), "",
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
    print("[coach] final: -dpcvars=" + dpcvars(values))


if __name__ == "__main__":
    main()
