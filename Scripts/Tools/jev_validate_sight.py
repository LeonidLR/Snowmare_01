"""Design-time check of the Sprint 08 senses (hearing, memory, demask, blind fire) against TypeSafe Jev's common sense.

The game never calls Jev at run time: the rules are C++ (Source/CodexTactics/Private/Combat/SightRules.cpp,
UTacticalSightSubsystem). This script mirrors the non-geometric ones on worded scenarios (distances and times as
words: Jev is weak at numbers), asks Jev in one request and prints where the code and Jev disagree.

Usage: python Scripts/Tools/jev_validate_sight.py      (needs the TypeSafe key, see jev_client.py)
Exit code 0 when the agreement is at least 75 %, 1 below, 2 when Jev could not be asked.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import jev_client  # noqa: E402

# --- Mirror of SightRules (keep in step with the C++) ---------------------------------------------------------------
ENEMY_HEARING_M = 12.0
PRONE_HEARING_M = 5.0
SQUAD_HEARING_M = 12.0
DEMASK_S = 2.0
FORGET_S = 20.0
FORGET_ARRIVAL_M = 2.5
SEARCH_S = 5.0  # an arrived enemy searches the spot this long before it gives up


def dist_words(m):
    if m <= 3:
        return "within a couple of steps (about %d m)" % round(m)
    if m <= 7:
        return "a few strides away (about %d m)" % round(m)
    if m <= 12:
        return "a short sprint away (about %d m)" % round(m)
    return "far across the snowfield (about %d m)" % round(m)


def time_words(s):
    if s < 2:
        return "a moment ago (about %d second)" % max(1, round(s))
    if s < 10:
        return "a few seconds ago (about %d s)" % round(s)
    return "a long while ago (about %d s)" % round(s)


SCENARIOS = [
    # Enemy hears an operative it cannot see (barricade between them).
    {"id": "hear_walk_10", "kind": "enemy_hears", "stance": "walking crouched", "m": 10,
     "code": 10 <= ENEMY_HEARING_M},
    {"id": "hear_walk_18", "kind": "enemy_hears", "stance": "walking crouched", "m": 18,
     "code": 18 <= ENEMY_HEARING_M},
    {"id": "hear_crawl_6", "kind": "enemy_hears", "stance": "crawling flat on his belly", "m": 6,
     "code": 6 <= PRONE_HEARING_M},
    {"id": "hear_crawl_2", "kind": "enemy_hears", "stance": "crawling flat on his belly", "m": 2,
     "code": 2 <= PRONE_HEARING_M},
    # Squad senses an unseen enemy (crunching snow, growls).
    {"id": "squad_hears_9", "kind": "squad_hears", "m": 9, "code": 9 <= SQUAD_HEARING_M},
    {"id": "squad_hears_25", "kind": "squad_hears", "m": 25, "code": 25 <= SQUAD_HEARING_M},
    # Enemy memory of a vanished operative.
    {"id": "memory_fresh_far", "kind": "memory", "s": 6, "m": 15, "code": 6 < FORGET_S},
    {"id": "memory_arrived", "kind": "memory", "s": 6, "m": 1, "code": True},  # just arrived: it searches SEARCH_S first
    {"id": "memory_old", "kind": "memory", "s": 30, "m": 15, "code": 30 < FORGET_S},
    # Muzzle flash demask.
    {"id": "demask_1s", "kind": "demask", "s": 1, "code": 1 < DEMASK_S},
    {"id": "demask_4s", "kind": "demask", "s": 4, "code": 4 < DEMASK_S},
]

BLIND_FIRE_OPTIONS = {
    "same": "About as accurate as aimed fire at a visible target",
    "minus_40": "Clearly worse: roughly six hits where aimed fire would land ten",
    "minus_80": "Almost hopeless: one or two hits where aimed fire would land ten",
    "never": "It can never hit anything",
}
BLIND_FIRE_CODE = "minus_80"


def build():
    state = {"setting": "Arctic tactical shooter. Soldiers hide behind knee-high 60 cm barricades in fresh snow; mutant "
                        "hounds and frozen corpses hunt them by sight and sound. A soldier lying flat behind such a "
                        "barricade cannot be seen over it.",
             "scenarios": {}}
    questions = {}
    for s in SCENARIOS:
        if s["kind"] == "enemy_hears":
            state["scenarios"][s["id"]] = "A soldier hidden behind a barricade is %s, %s from a hunting mutant that cannot see him." % (
                s["stance"], dist_words(s["m"]))
            q = "Would the mutant notice him by sound (snow crunching, gear rattling)?"
        elif s["kind"] == "squad_hears":
            state["scenarios"][s["id"]] = "A mutant moves behind cover %s from an alert soldier who cannot see it." % dist_words(s["m"])
            q = "Would the soldier hear it and know roughly where it is?"
        elif s["kind"] == "memory":
            state["scenarios"][s["id"]] = ("A mutant saw a soldier, then lost sight of him %s. It is now %s from the spot "
                                           "where it last saw him and nobody is there to be seen." % (time_words(s["s"]), dist_words(s["m"])))
            q = ("Should the mutant keep chasing this soldier — still moving to or searching at that spot — rather than "
                 "giving up on him and turning to other prey?")
        else:
            state["scenarios"][s["id"]] = "A mutant spitter behind cover fired at the squad %s, with a bright flash." % time_words(s["s"])
            q = ("Should the soldiers still treat it as seen — able to aim straight at it — because of that shot, even "
                 "though it is behind cover?")
        questions[s["id"]] = {"type": "noul", "instructions": "Look at `scenarios.%s`. %s" % (s["id"], q)}
    state["blind_fire"] = ("A soldier fires at the spot where a mutant was last seen behind a barricade, without seeing it now; "
                           "the mutant may still be crouching there.")
    questions["blind_fire"] = {"type": "choice", "instructions": "Look at `blind_fire`. How accurate should such blind fire be?",
                               "criteria": BLIND_FIRE_OPTIONS}
    return state, questions


def main():
    state, questions = build()
    answers, error = jev_client.ask(state, questions)
    if answers is None:
        print("Jev not asked: %s" % error)
        return 2
    agree = total = 0
    for s in SCENARIOS:
        jev = jev_client.noul(answers, s["id"], -1)
        ok = jev >= 0 and (jev >= 0.5) == s["code"]
        agree += ok
        total += 1
        print("%-18s code %-5s Jev %.2f %s" % (s["id"], "yes" if s["code"] else "no", jev, "" if ok else "<-- DISAGREE"))
    choice, confidence = jev_client.choice(answers, "blind_fire")
    ok = choice == BLIND_FIRE_CODE
    agree += ok
    total += 1
    print("%-18s code %-8s Jev %-8s (%.2f) %s" % ("blind_fire", BLIND_FIRE_CODE, choice, confidence or 0.0, "" if ok else "<-- DISAGREE"))
    rate = agree / float(total)
    print("Agreement: %d/%d (%.0f%%)" % (agree, total, rate * 100))
    return 0 if rate >= 0.75 else 1


if __name__ == "__main__":
    sys.exit(main())
