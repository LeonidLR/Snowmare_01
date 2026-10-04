"""Jev AI coach: trains the playtest bot's and the enemies' AI tunables with TypeSafe Jev as the diagnostician.

Loop (code owns the workflow, Jev supplies typed judgments, measured outcomes decide):
  1. run a bot batch (Scripts/bot_run.ps1) with the current Codex.* console variables (-dpcvars=);
  2. extract hard facts from the game logs (results, failed waves, death causes, marksman shots / hits / distances,
     bot assaults, marksman retreats) - computed in code, never guessed;
  3. ask Jev in one request (speculative fan-out): which bot behaviour failed, which enemy behaviour is unfair, how fair
     the fight is (score), whether the defeats were avoidable (noul);
  4. map the answers to one bounded step of one whitelisted knob (confidence-gated; low confidence -> the next untried
     knob in a fixed order), run the next batch, keep the step only if the measured score improved.

Bot knobs are AI quality and are tuned freely. Enemy knobs (marksman damage, accuracy, ...) are balance - tuned only
with --tune-enemies, and the report lists them as proposals for the user / Wave Editor either way.

Usage:
  python Scripts/Tools/jev_ai_coach.py [--iterations 6] [--runs 8] [--profile VETERAN] [--parallel 4]
                                        [--level-json <path>] [--tune-enemies] [--dry-run]
Output: Saved/Telemetry/ai_coach/<timestamp>/report.json and report.md (best -dpcvars line, every step, Jev answers);
the best bot knobs (and enemy knobs with --tune-enemies) go to Content/Data/AI/ai_tuning.json, which the game applies at
start (Data/AITuning.h) - only when they beat the defaults by more than the noise margin. Batches run with -NoAITuning.
The API key comes from TYPESAFE_API_KEY, the Windows user environment (registry) or Saved/Config/typesafe.key (never printed). No key / API error -> the step
falls back to the fixed knob order and the report says so (no invented answers).
"""

import argparse
import datetime
import glob
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
# A step is kept only when the measured score rises by more than this (8 runs: one win = 0.125 of win rate).
NOISE_MARGIN = 0.1
TUNING_FILE = os.path.join(ROOT, "Content", "Data", "AI", "ai_tuning.json")

# name -> (console variable, default, min, max, step, owner)
KNOBS = {
    "bot_clear_radius": ("Codex.Bot.AssaultClearRadius", 1500.0, 600.0, 3000.0, 300.0, "bot"),
    "bot_stop_distance": ("Codex.Bot.AssaultStopDistance", 900.0, 500.0, 1300.0, 150.0, "bot"),
    "marksman_damage": ("Codex.Marksman.ShotDamage", 45.0, 20.0, 45.0, 5.0, "enemy"),
    "marksman_accuracy": ("Codex.Marksman.BaseAccuracy", 0.6, 0.3, 0.6, 0.05, "enemy"),
    "marksman_aim": ("Codex.Marksman.AimDuration", 2.0, 2.0, 3.5, 0.25, "enemy"),
    "marksman_cooldown": ("Codex.Marksman.ShotCooldown", 2.5, 2.5, 5.0, 0.5, "enemy"),
    "marksman_kite_cooldown": ("Codex.Marksman.RetreatCooldown", 10.0, 4.0, 20.0, 2.0, "enemy"),
}

# Jev option -> (knob, direction). "none" options map to no step.
BOT_OPTIONS = {
    "storms_marksmen_too_rarely": ("The squad lets marksmen shoot from beyond rifle range; it should storm them earlier even with some melee enemies around", ("bot_clear_radius", +1)),
    "storms_into_melee": ("The squad leaves the melee fight to storm marksmen and gets caught by hounds / cutters", ("bot_clear_radius", -1)),
    "stops_too_far_from_marksman": ("Storming operatives stop where their rifles still miss or barely reach the marksman", ("bot_stop_distance", -1)),
    "stops_too_close_to_marksman": ("Storming operatives run right up to the marksman and are shot on the way", ("bot_stop_distance", +1)),
    "no_clear_bot_weakness": ("The bot's choices look sound; the losses come from elsewhere", None),
}
ENEMY_OPTIONS = {
    "marksman_too_lethal_per_shot": ("A single marksman hit takes too large a share of an operative's health", ("marksman_damage", -1)),
    "marksman_too_accurate": ("Marksmen hit far too often at long range for a fair fight", ("marksman_accuracy", -1)),
    "marksman_no_reaction_window": ("The telegraphed aim is too short to react (take cover / move) before the shot", ("marksman_aim", +1)),
    "marksman_fires_too_often": ("Marksmen fire again too soon; the squad gets no breathing room", ("marksman_cooldown", +1)),
    "marksman_too_evasive": ("Marksmen keep running away so the squad can never close in", ("marksman_kite_cooldown", +1)),
    "enemies_fair": ("The enemies behave fairly; no enemy change is needed", None),
}
FALLBACK_ORDER = [("bot_clear_radius", +1), ("marksman_aim", +1), ("bot_stop_distance", -1), ("marksman_damage", -1),
                  ("marksman_accuracy", -1), ("marksman_cooldown", +1), ("bot_clear_radius", -1)]


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


def ask_jev(state, questions):
    """One typed request; returns (answers, error)."""
    key = api_key()
    if not key:
        return None, "no TypeSafe API key"
    body = json.dumps({"state": state, "model": MODEL, "questions": questions}).encode("utf-8")
    req = urllib.request.Request(API_URL, data=body, method="POST",
                                 headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return json.loads(resp.read().decode("utf-8")).get("answers", {}), None
    except urllib.error.HTTPError as e:
        return None, "HTTP %d: %s" % (e.code, e.read().decode("utf-8", "replace")[:300])
    except Exception as e:  # network, timeout
        return None, str(e)


def dpcvars(values):
    return ",".join("%s=%g" % (KNOBS[k][0], v) for k, v in sorted(values.items()))


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


def log_path(args, n):
    return os.path.join(ROOT, "Saved", "Logs", "Bot-%s-%d.log" % (args.profile.upper(), n))


def collect_facts(args):
    runs = []
    for n in range(1, args.runs + 1):
        path = log_path(args, n)
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        result = re.findall(r"\[Bot\] RESULT (\w+)", text)
        cleared = re.findall(r"\[Bot\] Wave (\d+) cleared", text)
        failed = re.findall(r"Mission failed: (.*)", text)
        shots = re.findall(r"\[Marksman\] \S+ fires at .*?: (\d+) m, chance ([\d.]+), (hit|miss)", text)
        hits = [s for s in shots if s[2] == "hit"]
        runs.append({
            "result": result[-1] if result else "ERROR",
            "waves_cleared": int(cleared[-1]) if cleared else 0,
            "death": failed[-1].strip()[:120] if failed else "",
            "death_by_freezing": bool(failed) and ("переохлажд" in failed[-1]),
            "marksman_shots": len(shots),
            "marksman_hits": len(hits),
            "marksman_avg_shot_m": round(sum(int(s[0]) for s in shots) / len(shots), 1) if shots else 0,
            "marksman_retreats": len(re.findall(r"\[Marksman\] \S+ retreats", text)),
            "bot_assaults": len(re.findall(r"\[Bot\] Assault marksman", text)),
            "marksmen_seen": len(set(re.findall(r"\[Marksman\] (\S+) ", text))),
            "marksmen_killed": len(re.findall(r"\[Marksman\] \S+ dies at", text)),
            "marksman_kill_ranges_m": [int(d) for d in re.findall(r"\[Marksman\] \S+ dies at (\d+) m", text)],
            "bot_cover_reactions": len(re.findall(r"\[Bot\] Marksman \S+ aims at", text)),
        })
    wins = sum(1 for r in runs if r["result"] == "VICTORY")
    shots = sum(r["marksman_shots"] for r in runs)
    summary = {
        "runs": len(runs), "victories": wins, "win_rate": round(wins / max(len(runs), 1), 3),
        "avg_waves_cleared": round(sum(r["waves_cleared"] for r in runs) / max(len(runs), 1), 2),
        "deaths_by_freezing": sum(1 for r in runs if r["death_by_freezing"]),
        "marksman_hit_rate": round(sum(r["marksman_hits"] for r in runs) / max(shots, 1), 3),
        "marksman_shots_per_run": round(shots / max(len(runs), 1), 1),
        "bot_assaults_per_run": round(sum(r["bot_assaults"] for r in runs) / max(len(runs), 1), 1),
        "marksman_retreats_per_run": round(sum(r["marksman_retreats"] for r in runs) / max(len(runs), 1), 1),
        "marksman_kill_share": round(sum(r["marksmen_killed"] for r in runs) / max(sum(r["marksmen_seen"] for r in runs), 1), 3),
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
    ]


def score(facts):
    """Measured objective: win rate first, then waves cleared."""
    s = facts["summary"]
    return s["win_rate"] + 0.05 * s["avg_waves_cleared"]


def build_questions():
    return {
        "bot_weakness": {"type": "choice",
                         "instructions": "The `fights` were played by a scripted bot commanding a 3-operative squad against waves "
                                         "with marksmen (long-range snipers, 20-35 m band) and melee enemies. The squad's rifles "
                                         "reach 14 m (19 m prone). Which bot behaviour most explains the defeats?",
                         "criteria": {k: v[0] for k, v in BOT_OPTIONS.items()}},
        "enemy_issue": {"type": "choice",
                        "instructions": "Judging only the enemies in the `fights`: which marksman behaviour makes the fight most unfair "
                                        "for a competent squad?",
                        "criteria": {k: v[0] for k, v in ENEMY_OPTIONS.items()}},
        "fairness": {"type": "score",
                     "instructions": "How fair and tactically interesting is this encounter for a skilled player?",
                     "criteria": ["Hopeless: the squad dies whatever it does", "Very hard: wins are rare flukes",
                                  "Hard but fair: good tactics win", "Comfortable: most attempts win", "Trivial: no threat"]},
        "avoidable": {"type": "noul",
                      "instructions": "Could better squad tactics (cover, storming, focus fire) have avoided most of these defeats?"},
    }


def choose_step(answers, values, tried, tune_enemies):
    """(knob, direction, reason) from the answers, confidence-gated; else the fixed order."""
    candidates = []
    if answers:
        for qid, options in (("bot_weakness", BOT_OPTIONS), ("enemy_issue", ENEMY_OPTIONS)):
            ans = answers.get(qid) or {}
            probs = ans.get("probabilities") or {}
            for option, p in sorted(probs.items(), key=lambda kv: -kv[1]):
                step = options.get(option, (None, None))[1]
                if step and ans.get("confidence", 0) >= CONFIDENCE_GATE:
                    candidates.append((p, step[0], step[1], "jev:%s=%s (p %.2f, conf %.2f)" % (qid, option, p, ans.get("confidence", 0))))
        candidates.sort(key=lambda c: -c[0])
    candidates += [(0, k, d, "fallback order") for k, d in FALLBACK_ORDER]
    for _, knob, direction, reason in candidates:
        owner = KNOBS[knob][5]
        if owner == "enemy" and not tune_enemies:
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
    ans = (answers or {}).get("enemy_issue") or {}
    for option, p in sorted((ans.get("probabilities") or {}).items(), key=lambda kv: -kv[1])[:3]:
        step = ENEMY_OPTIONS.get(option, (None, None))[1]
        if step:
            out.append({"issue": option, "probability": round(p, 3), "knob": KNOBS[step[0]][0],
                        "direction": "raise" if step[1] > 0 else "lower"})
    return out


def write_tuning(best_values, best_score, baseline, args):
    """Only knobs that differ from their defaults; enemy knobs only when they were tuned on purpose."""
    cvars = {KNOBS[k][0]: "%g" % v for k, v in sorted(best_values.items())
             if v != KNOBS[k][1] and (KNOBS[k][5] == "bot" or args.tune_enemies)}
    if not cvars:
        return
    os.makedirs(os.path.dirname(TUNING_FILE), exist_ok=True)
    data = {"comment": "Written by Scripts/Tools/jev_ai_coach.py; applied at game start (Data/AITuning.h), a -dpcvars= "
                       "value wins, -NoAITuning skips it. Review before committing.",
            "updated": datetime.datetime.now().isoformat(timespec="seconds"), "profile": args.profile,
            "runs_per_batch": args.runs, "baseline_score": round(baseline, 3), "best_score": round(best_score, 3), "cvars": cvars}
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
    ap.add_argument("--tune-enemies", action="store_true")
    ap.add_argument("--dry-run", action="store_true", help="analyse the last batch's logs once, no new runs")
    args = ap.parse_args()

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = os.path.join(ROOT, "Saved", "Telemetry", "ai_coach", stamp)
    os.makedirs(out_dir, exist_ok=True)
    values = {k: v[1] for k, v in KNOBS.items()}
    history, tried = [], set()

    facts = collect_facts(args) if args.dry_run else run_batch(args, values)
    best_values, best_score = dict(values), score(facts)
    for it in range(1, (1 if args.dry_run else args.iterations) + 1):
        state = {"knobs": {KNOBS[k][0]: v for k, v in values.items()}, "fights": facts["words"], "summary": facts["summary"],
                 "tried_without_gain": ["%s %s" % (k, "+" if d > 0 else "-") for k, d in sorted(tried)]}
        answers, error = ask_jev(state, build_questions())
        entry = {"iteration": it, "values": dict(values), "summary": facts["summary"], "score": round(score(facts), 3),
                 "jev": answers, "jev_error": error}
        print("[coach] it %d: win %.0f%%, waves %.2f, score %.3f%s" % (
            it, 100 * facts["summary"]["win_rate"], facts["summary"]["avg_waves_cleared"], score(facts),
            "" if answers else " (Jev unavailable: %s)" % error), flush=True)
        if answers:
            for qid in ("bot_weakness", "enemy_issue"):
                a = answers.get(qid, {})
                print("        %s -> %s (conf %.2f)" % (qid, a.get("choice"), a.get("confidence", 0)), flush=True)
            print("        fairness %.2f / 4, avoidable %.2f" % ((answers.get("fairness") or {}).get("score", -1),
                                                               (answers.get("avoidable") or {}).get("noul", -1)), flush=True)
        entry["enemy_proposals"] = enemy_proposals(answers)
        if args.dry_run:
            history.append(entry)
            break
        step = choose_step(answers, values, tried, args.tune_enemies)
        if not step:
            entry["step"] = "no untried step left"
            history.append(entry)
            break
        knob, direction, new, reason = step
        trial = dict(values)
        trial[knob] = new
        trial_facts = run_batch(args, trial)
        gained = score(trial_facts) > score(facts) + NOISE_MARGIN
        entry["step"] = {"knob": KNOBS[knob][0], "from": values[knob], "to": new, "reason": reason,
                         "trial_score": round(score(trial_facts), 3), "kept": gained}
        print("[coach]   %s %g -> %g (%s): score %.3f -> %.3f, %s" % (KNOBS[knob][0], values[knob], new, reason,
              score(facts), score(trial_facts), "KEPT" if gained else "reverted"), flush=True)
        history.append(entry)
        if gained:
            values, facts = trial, trial_facts
            tried = {(k, d) for k, d in tried if k != knob}
            if score(facts) > best_score:
                best_values, best_score = dict(values), score(facts)
        else:
            tried.add((knob, direction))

    report = {"profile": args.profile, "runs_per_batch": args.runs, "tune_enemies": args.tune_enemies,
              "best_score": round(best_score, 3), "best_dpcvars": dpcvars(best_values),
              "best_values": {KNOBS[k][0]: v for k, v in best_values.items()}, "history": history}
    baseline = history[0]["score"] if history else best_score
    if not args.dry_run and best_score > baseline + NOISE_MARGIN - 1e-9:
        write_tuning(best_values, best_score, baseline, args)
    with open(os.path.join(out_dir, "report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=2)
    lines = ["# Jev AI coach - %s" % stamp, "", "Profile %s, %d runs per batch, enemies tuned: %s." % (
        args.profile, args.runs, args.tune_enemies), "", "Best score %.3f: `-dpcvars=%s`" % (best_score, dpcvars(best_values)), "",
        "| It | Win | Waves | Step | Kept | Jev bot / enemy |", "|---|---|---|---|---|---|"]
    for h in history:
        s, st, j = h["summary"], h.get("step"), h.get("jev") or {}
        step_text = "%s %g->%g (%s)" % (st["knob"], st["from"], st["to"], st["reason"]) if isinstance(st, dict) else str(st)
        lines.append("| %d | %.0f%% | %.2f | %s | %s | %s / %s |" % (
            h["iteration"], 100 * s["win_rate"], s["avg_waves_cleared"], step_text,
            st.get("kept") if isinstance(st, dict) else "-", (j.get("bot_weakness") or {}).get("choice", "-"),
            (j.get("enemy_issue") or {}).get("choice", "-")))
    with open(os.path.join(out_dir, "report.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("[coach] report: " + os.path.join(out_dir, "report.md"))
    print("[coach] best: -dpcvars=" + dpcvars(best_values))


if __name__ == "__main__":
    main()
