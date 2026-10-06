"""Design-time check of the enemy footstep hearing radii (user report 2026-10-06: enemies heard running operatives from
very far away) against TypeSafe Jev's common sense.

The game never calls Jev at run time: hearing is the deterministic C++ rule PerceptionRules::HearsMovementThroughWalls
(Source/CodexTactics/Private/AI/PerceptionRules.cpp) with the radii of Content/Data/AI/enemy_perception.json. This script
mirrors the rule (radius by archetype and gait, x HEARING_OCCLUSION_PER_WALL per wall in between), asks Jev about worded
scenarios (each archetype x gait x distance, a wall in between or not, a snowy arctic outpost) in one request and prints
where the code and Jev disagree, so the radii can be tuned (they stay data: enemy_perception.json, Codex.Perception.*).

Usage: python Scripts/Tools/jev_validate_hearing.py [--dump-scenarios]   (needs the TypeSafe key, see jev_client.py)
Exit code 0 when the agreement is at least 75 %, 1 below, 2 when Jev could not be asked.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import jev_client  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DATA = os.path.join(ROOT, "Content", "Data", "AI", "enemy_perception.json")
GAIT_KEYS = {"walk": "hear_walk_m", "run": "hear_run_m", "crouch": "hear_crouch_walk_m", "crawl": "hear_crawl_m"}
DEFAULT_OCCLUSION = 0.5  # PerceptionRules: FEnemyPerceptionParams::HearingOcclusionPerWall
MAX_WALLS = 3


def load_radii():
    with open(DATA, encoding="utf-8") as f:
        data = json.load(f)
    return data["archetypes"]


RADII = load_radii()


def code_hears(s):
    """Mirror of PerceptionRules::HearsMovementThroughWalls."""
    entry = RADII[s["enemy"]]
    radius = float(entry[GAIT_KEYS[s["gait"]]])
    factor = min(max(float(entry.get("hearing_occlusion_per_wall", DEFAULT_OCCLUSION)), 0.0), 1.0)
    radius *= factor ** min(max(s["walls"], 0), MAX_WALLS)
    return "hears" if s["distance_m"] <= radius else "unheard"


ENEMY_WORDS = {
    "FROST_HOUND": "a frost hound (a mutated wolf-like beast with keen ears)",
    "MARKSMAN": "a mutant marksman (a sniper relying on his eyes, ordinary hearing)",
    "SPITTER": "a spitter mutant (ordinary hearing)",
    "BRUTE": "a brute (a huge, dull-witted mutant with poor senses)",
    "CUTTER": "a cutter (an agile predator mutant with sharp senses)",
    "FROSTBITTEN": "a frostbitten (a shambling frozen ghoul with ordinary hearing)",
}
GAIT_WORDS = {
    "walk": "walking normally through the snow",
    "run": "sprinting through the snow, gear rattling",
    "crouch": "sneaking crouched, placing his boots carefully",
    "crawl": "crawling prone through the snow",
}

SCENARIOS = [
    # id, enemy, gait, distance in metres, walls between the two
    {"id": "hound_run_13", "enemy": "FROST_HOUND", "gait": "run", "distance_m": 13, "walls": 0},
    {"id": "hound_run_20", "enemy": "FROST_HOUND", "gait": "run", "distance_m": 20, "walls": 0},
    {"id": "hound_walk_6", "enemy": "FROST_HOUND", "gait": "walk", "distance_m": 6, "walls": 0},
    {"id": "hound_walk_10", "enemy": "FROST_HOUND", "gait": "walk", "distance_m": 10, "walls": 0},
    {"id": "hound_crouch_3", "enemy": "FROST_HOUND", "gait": "crouch", "distance_m": 3, "walls": 0},
    {"id": "hound_crawl_4", "enemy": "FROST_HOUND", "gait": "crawl", "distance_m": 4, "walls": 0},
    {"id": "hound_run_wall_10", "enemy": "FROST_HOUND", "gait": "run", "distance_m": 10, "walls": 1},
    {"id": "marksman_run_8", "enemy": "MARKSMAN", "gait": "run", "distance_m": 8, "walls": 0},
    {"id": "marksman_run_18", "enemy": "MARKSMAN", "gait": "run", "distance_m": 18, "walls": 0},
    {"id": "marksman_walk_9", "enemy": "MARKSMAN", "gait": "walk", "distance_m": 9, "walls": 0},
    {"id": "spitter_walk_4", "enemy": "SPITTER", "gait": "walk", "distance_m": 4, "walls": 0},
    {"id": "spitter_run_wall_8", "enemy": "SPITTER", "gait": "run", "distance_m": 8, "walls": 1},
    {"id": "spitter_crouch_6", "enemy": "SPITTER", "gait": "crouch", "distance_m": 6, "walls": 0},
    {"id": "brute_run_9", "enemy": "BRUTE", "gait": "run", "distance_m": 9, "walls": 0},
    {"id": "brute_walk_8", "enemy": "BRUTE", "gait": "walk", "distance_m": 8, "walls": 0},
    {"id": "brute_crawl_3", "enemy": "BRUTE", "gait": "crawl", "distance_m": 3, "walls": 0},
    {"id": "cutter_run_11", "enemy": "CUTTER", "gait": "run", "distance_m": 11, "walls": 0},
    {"id": "cutter_walk_wall_5", "enemy": "CUTTER", "gait": "walk", "distance_m": 5, "walls": 1},
    {"id": "cutter_crouch_2", "enemy": "CUTTER", "gait": "crouch", "distance_m": 2, "walls": 0},
    {"id": "frost_run_25", "enemy": "FROSTBITTEN", "gait": "run", "distance_m": 25, "walls": 0},
    {"id": "frost_run_two_walls_4", "enemy": "FROSTBITTEN", "gait": "run", "distance_m": 4, "walls": 2},
    {"id": "frost_walk_3", "enemy": "FROSTBITTEN", "gait": "walk", "distance_m": 3, "walls": 0},
    {"id": "frost_crawl_1", "enemy": "FROSTBITTEN", "gait": "crawl", "distance_m": 1, "walls": 0},
    {"id": "marksman_crouch_wall_2", "enemy": "MARKSMAN", "gait": "crouch", "distance_m": 2, "walls": 1},
]

OPTIONS = {
    "hears": "It plausibly hears the operative's footsteps and turns to investigate",
    "unheard": "It does not notice the footsteps at all",
}


def wall_words(walls):
    if walls <= 0:
        return "open ground between them, nothing in the way"
    if walls == 1:
        return "a solid concrete wall stands between them"
    return "two solid walls (a building) stand between them"


def build():
    state = {"setting": "A snowy arctic outpost at night, light wind, deep crunchy snow muffles some sound. A mutant enemy on "
                        "patrol, not yet alerted, listens while it walks its route. A soldier of the squad moves nearby; the "
                        "enemy cannot see him. Gunshots are a separate matter - only footsteps count here.",
             "cases": {}}
    questions = {}
    for s in SCENARIOS:
        state["cases"][s["id"]] = {
            "enemy": ENEMY_WORDS[s["enemy"]],
            "soldier": GAIT_WORDS[s["gait"]],
            "distance": "about %d metres away" % s["distance_m"],
            "between them": wall_words(s["walls"]),
        }
        questions[s["id"]] = {"type": "choice", "instructions": "Look at `cases.%s`. Does the enemy hear the soldier's movement?" % s["id"],
                              "criteria": OPTIONS}
    return state, questions


def main():
    if "--dump-scenarios" in sys.argv:
        for s in SCENARIOS:
            print("%-24s -> %s" % (s["id"], code_hears(s)))
        return 0
    state, questions = build()
    answers, error = jev_client.ask(state, questions)
    if answers is None:
        print("Jev not asked: %s" % error)
        return 2
    agree = 0
    for s in SCENARIOS:
        code = code_hears(s)
        jev, confidence = jev_client.choice(answers, s["id"])
        ok = jev == code
        agree += ok
        print("%-24s code %-8s Jev %-8s (%.2f) %s" % (s["id"], code, jev, confidence or 0.0, "" if ok else "<-- DISAGREE"))
    rate = agree / float(len(SCENARIOS))
    print("Agreement: %d/%d (%.0f%%)" % (agree, len(SCENARIOS), rate * 100))
    return 0 if rate >= 0.75 else 1


if __name__ == "__main__":
    sys.exit(main())
