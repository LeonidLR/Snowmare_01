"""Jev review of the roster-by-weather matrix (docs/systems/environment-weather.md §4.2, docs/design/roster-by-weather.html).

For each of the 16 time-of-day x weather cells Jev scores how well each enemy type suits the conditions (0 useless .. 4 ideal)
and picks what a marksman / a spitter should become there. Code compares that with the proposed visibility-band presets.
Usage: python Scripts/Tools/jev_roster_matrix.py [--json out.json]
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import jev_client  # noqa: E402

ENEMIES = {
    "HOUND": "Frost Hound: fast mutant dog pack flanker (5.4 m/s, bite). Sight 15 m. Hunts mostly by smell (scent 6 m, follows trails), "
             "fears fire.",
    "SPITTER": "Frost Spitter: artillery mutant that keeps 12-15 m away and spits freezing acid pools; needs to see its target to aim. "
               "Sight 25 m.",
    "BRUTE": "Frost Brute: slow heavy tank (220 HP, heavy armour) that charges, knocks soldiers back and smashes barricades. Sight 20 m.",
    "FROSTBITTEN": "Frostbitten: slow frozen zombie infantry, immune to cold, fights in hordes and shoves in melee. Sight 20 m.",
    "CUTTER": "Cutter: biomechanical hound that pounces 3.5-9 m (6.2 m/s), never knocked down; closes distance by itself. Sight 25 m.",
    "MARKSMAN": "Marksman: human sniper with a scoped rifle; holds 20-35 m, telegraphs each shot with a 2 s aim beam, flees closer than "
                "12 m. Sight 45 m. Useless when he cannot see far.",
}
PHASES = [("EarlyMorning", "early morning, low cold sun, long blue shadows", 0.70),
          ("Midday", "midday, the brightest time of the day", 1.00),
          ("Afternoon", "afternoon, the sun is getting lower", 0.85),
          ("Night", "night, dark, only moonlight", 0.25)]
WEATHERS = [("Clear", "clear sky, hard frost, no snow falling, you can see far", 1.0),
            ("Overcast", "overcast grey sky, flat light, no snow falling", 0.8),
            ("Snowfall", "steady snowfall, visibility drops to a few dozen metres", 0.5),
            ("Blizzard", "blizzard, wind-driven snow, whiteout, you can barely see a few metres", 0.2)]
SUIT = ["Useless here: it would never be sent out in these conditions",
        "Weak: clearly handicapped by these conditions",
        "Workable: conditions neither help nor hurt it much",
        "Strong: conditions favour it",
        "Ideal: these are its best hunting conditions"]
REPLACE = ["keep", "HOUND", "SPITTER", "BRUTE", "FROSTBITTEN", "CUTTER", "MARKSMAN"]


def band(v):
    return "long" if v >= 0.6 - 1e-9 else "mid" if v >= 0.35 - 1e-9 else "close" if v >= 0.15 - 1e-9 else "blind"


def proposed(b, enemy):
    """What the proposed preset does to this enemy (code side of the comparison)."""
    if enemy == "MARKSMAN":
        return {"long": "keep", "mid": "50% -> FROSTBITTEN", "close": "-> FROSTBITTEN", "blind": "-> FROSTBITTEN"}[b]
    if enemy == "SPITTER":
        return {"long": "keep", "mid": "keep", "close": "50% -> CUTTER", "blind": "-> CUTTER"}[b]
    return "keep"


def ask_cell(phase, weather):
    state = {
        "game": "Turn-based / real-time tactical survival in the frozen Arctic: a squad of 4 soldiers fights mutant waves.",
        "enemies": ENEMIES,
        "conditions": "Time of day: %s. Weather: %s." % (phase[1], weather[1]),
    }
    questions = {}
    for enemy in ENEMIES:
        questions["suit_" + enemy] = {
            "type": "score",
            "instructions": "Read `conditions` and `enemies.%s`. How well do these conditions suit this enemy for hunting the squad?" % enemy,
            "criteria": SUIT}
    for enemy in ("MARKSMAN", "SPITTER"):
        questions["replace_" + enemy] = {
            "type": "choice",
            "instructions": ("Read `conditions` and `enemies`. A wave planned for good weather includes a %s. In these conditions, should "
                             "it stay, or which enemy should take its place so the wave still makes sense?" % enemy),
            "criteria": {c: ("Keep the %s" % enemy if c == "keep" else ENEMIES[c]) for c in REPLACE if c != enemy}}
    return jev_client.ask(state, questions)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--json")
    args = parser.parse_args()
    rows = []
    for weather in WEATHERS:
        for phase in PHASES:
            answers, error = ask_cell(phase, weather)
            if answers is None:
                print("Jev not asked: %s" % error)
                return 2
            v = phase[2] * weather[2]
            row = {"phase": phase[0], "weather": weather[0], "visibility": round(v, 3), "band": band(v),
                   "suit": {e: round(jev_client.score(answers, "suit_" + e, -1), 2) for e in ENEMIES},
                   "replace": {e: jev_client.choice(answers, "replace_" + e) for e in ("MARKSMAN", "SPITTER")}}
            rows.append(row)
            suit = "  ".join("%s %.1f" % (e[:4], s) for e, s in row["suit"].items())
            rep = "  ".join("%s->%s(%.2f) [plan: %s]" % (e[:4], c, conf or 0, proposed(row["band"], e))
                            for e, (c, conf) in row["replace"].items())
            print("%-8s %-12s v=%.2f %-5s | %s | %s" % (weather[0], phase[0], v, row["band"], suit, rep))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(rows, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
