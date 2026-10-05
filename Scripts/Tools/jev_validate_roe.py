"""Design-time check of the Commander Mode rules (Sprint 07) against TypeSafe Jev's tactical common sense.

The game never calls Jev at run time (a network call per decision is too slow and not deterministic): the decisions
are plain C++ rules (Source/CodexTactics/Private/Characters/SquadAutonomyRules.cpp). This script mirrors two of them,
the Safe Aid Check and the ThreatLevel target choice, on worded combat scenarios, asks Jev the same questions in one
request and prints where the code and Jev disagree, so the rules (or the ROE defaults in squad_roe.json) can be
tuned. Numbers go to Jev as words (Jev is weak at numbers).

Usage: python Scripts/Tools/jev_validate_roe.py          (needs the TypeSafe key, see jev_client.py)
Exit code 0 when the agreement is at least 75 %, 1 below, 2 when Jev could not be asked.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import jev_client  # noqa: E402

# --- Mirror of SquadAutonomyRules (keep in step with the C++) ---------------------------------------------------
SAFE_AID_CLEARANCE_M = 6.0
EMERGENCY_M = 3.5
TIERS = {"marksman": 3, "spitter": 3, "frost hound": 2, "cryo drone": 2, "cutter": 1, "brute": 1, "frostbitten": 0}


def code_safe_aid(s):
    return not s["sniper_aiming"] and s["nearest_enemy_to_patient_m"] > SAFE_AID_CLEARANCE_M


def code_target(enemies):
    def score(e):
        value = -e["distance_m"] + 100 * (TIERS[e["type"]] + (1 if e.get("aiming") else 0))
        return value + (10000 if e["distance_m"] < EMERGENCY_M else 0)
    return max(enemies, key=score)["id"]


def distance_words(metres):
    if metres < 3.5:
        return "point-blank, within arm's reach"
    if metres < 7:
        return "close, a few steps away"
    if metres < 15:
        return "at medium range"
    return "far away across the field"


# --- Scenarios ---------------------------------------------------------------------------------------------------
AID = [
    {"id": "aid_quiet", "sniper_aiming": False, "nearest_enemy_to_patient_m": 20, "patient": "badly wounded",
     "note": "the wave is thinning out"},
    {"id": "aid_hound_on_patient", "sniper_aiming": False, "nearest_enemy_to_patient_m": 2, "patient": "badly wounded",
     "note": "a frost hound is biting the wounded mate"},
    {"id": "aid_sniper_beam", "sniper_aiming": True, "nearest_enemy_to_patient_m": 18, "patient": "badly wounded",
     "note": "a marksman's laser sight rests on the wounded mate"},
    {"id": "aid_brute_closing", "sniper_aiming": False, "nearest_enemy_to_patient_m": 5, "patient": "badly wounded",
     "note": "a brute is lumbering towards the wounded mate"},
    {"id": "aid_far_pack", "sniper_aiming": False, "nearest_enemy_to_patient_m": 12, "patient": "badly wounded",
     "note": "frostbitten shamble in slowly at medium range"},
]

TARGETS = [
    {"id": "t_sniper_vs_walkers", "enemies": [
        {"id": "A", "type": "frostbitten", "distance_m": 8}, {"id": "B", "type": "frostbitten", "distance_m": 10},
        {"id": "C", "type": "marksman", "distance_m": 22, "aiming": True}]},
    {"id": "t_hound_at_throat", "enemies": [
        {"id": "A", "type": "frost hound", "distance_m": 2}, {"id": "B", "type": "spitter", "distance_m": 14}]},
    {"id": "t_spitter_vs_brute", "enemies": [
        {"id": "A", "type": "brute", "distance_m": 9}, {"id": "B", "type": "spitter", "distance_m": 13}]},
    {"id": "t_hounds_vs_frostbitten", "enemies": [
        {"id": "A", "type": "frostbitten", "distance_m": 6}, {"id": "B", "type": "frost hound", "distance_m": 11}]},
    {"id": "t_drone_vs_cutter", "enemies": [
        {"id": "A", "type": "cutter", "distance_m": 5}, {"id": "B", "type": "cryo drone", "distance_m": 12}]},
]


# --- Sprint 10: defense line («Рубеж обороны»), mirror of SquadAutonomyRules::PickTarget / CanGiveSafeAid -----------
INTERCEPT_M = 12.0
DEFENSE_LEASH_M = 5.0


def code_defense_aid(s):
    if s["intruders"]:
        return False
    return s["patient_to_generator_m"] <= DEFENSE_LEASH_M  # defense_ignore_distant_aid


def code_defense_target(enemies):
    def score(e):
        value = -e["distance_m"] + 100 * (TIERS[e["type"]] + (1 if e.get("aiming") else 0))
        if e["distance_m"] < EMERGENCY_M:
            value += 10000 + 1000000  # body-block tier (defense_body_block_priority)
        if e.get("attacking") or e["to_generator_m"] <= INTERCEPT_M:
            value += 100000 + 500 * (INTERCEPT_M - min(e["to_generator_m"], INTERCEPT_M))
        return value
    return max(enemies, key=score)["id"]


DEFENSE_AID = [
    {"id": "def_aid_far_under_attack", "intruders": True, "patient_to_generator_m": 15,
     "note": "hounds are tearing at the generator while a badly wounded mate lies far off across the yard"},
    {"id": "def_aid_near_quiet", "intruders": False, "patient_to_generator_m": 3,
     "note": "nothing threatens the generator right now; a badly wounded mate lies right next to it"},
    {"id": "def_aid_far_quiet", "intruders": False, "patient_to_generator_m": 15,
     "note": "nothing threatens the generator right now; a badly wounded mate lies far off across the yard"},
]

DEFENSE_TARGETS = [
    {"id": "def_t_generator_vs_sniper", "enemies": [
        {"id": "A", "type": "marksman", "distance_m": 20, "to_generator_m": 30, "aiming": True},
        {"id": "B", "type": "frost hound", "distance_m": 9, "to_generator_m": 2, "attacking": True}]},
    {"id": "def_t_two_at_generator", "enemies": [
        {"id": "A", "type": "brute", "distance_m": 6, "to_generator_m": 10},
        {"id": "B", "type": "frostbitten", "distance_m": 11, "to_generator_m": 1.5, "attacking": True}]},
    {"id": "def_t_point_blank", "enemies": [
        {"id": "A", "type": "frost hound", "distance_m": 2, "to_generator_m": 6},
        {"id": "B", "type": "cutter", "distance_m": 10, "to_generator_m": 1, "attacking": True}]},
]


def describe_defense_enemy(e):
    text = "%s, %s from the defender, %s from the generator" % (e["type"], distance_words(e["distance_m"]), distance_words(e["to_generator_m"]))
    if e.get("attacking"):
        text += ", attacking the generator"
    if e.get("aiming"):
        text += ", its laser sight aimed at the squad"
    return text


def describe_enemy(e):
    text = "%s, %s" % (e["type"], distance_words(e["distance_m"]))
    return text + (", its laser sight aimed at the squad" if e.get("aiming") else "")


def build():
    state = {"setting": "Turn-of-the-century arctic tactical shooter. A three-man squad holds positions behind barricades "
                        "around the point their commander ordered; each soldier fights on his own while the commander "
                        "watches. Frost hounds sprint and leap, brutes are slow and tough, frostbitten are slow walkers, "
                        "spitters lob acid from range, cryo drones freeze from range, marksmen snipe after a visible aim.",
             "aid": {}, "targets": {}}
    questions = {}
    for s in AID:
        state["aid"][s["id"]] = {"patient": s["patient"] + " squad mate, 5 metres from the rescuer",
                                 "nearest enemy to the patient": distance_words(s["nearest_enemy_to_patient_m"]),
                                 "situation": s["note"]}
        questions[s["id"]] = {"type": "noul", "instructions":
                              "Look at `aid.%s`. Is it sensible for the rescuer to leave his cover now and run to patch up "
                              "the wounded mate (rather than first shooting the threat)?" % s["id"]}
    for s in TARGETS:
        state["targets"][s["id"]] = {e["id"]: describe_enemy(e) for e in s["enemies"]}
        questions[s["id"]] = {"type": "choice", "instructions":
                              "Look at `targets.%s`: the enemies a soldier behind a barricade can shoot right now. Which "
                              "one should he shoot first to protect the squad?" % s["id"],
                              "criteria": {e["id"]: describe_enemy(e) for e in s["enemies"]}}
    state["defense"] = {}
    defender = ("A soldier ordered to hold the squad's generator at all costs (it keeps the squad from freezing); "
                "he stays within a few strides of it.")
    for s in DEFENSE_AID:
        state["defense"][s["id"]] = {"order": defender, "situation": s["note"]}
        questions[s["id"]] = {"type": "noul", "instructions":
                              "Look at `defense.%s`. Should the defender go and patch up the wounded mate now (for a mate "
                              "right at the generator that means a couple of steps without leaving it)?" % s["id"]}
    for s in DEFENSE_TARGETS:
        state["defense"][s["id"]] = {"order": defender, "enemies": {e["id"]: describe_defense_enemy(e) for e in s["enemies"]}}
        questions[s["id"]] = {"type": "choice", "instructions":
                              "Look at `defense.%s`. Which enemy should the defender shoot first to hold the generator?" % s["id"],
                              "criteria": {e["id"]: describe_defense_enemy(e) for e in s["enemies"]}}
    return state, questions


def main():
    state, questions = build()
    answers, error = jev_client.ask(state, questions)
    if answers is None:
        print("Jev not asked: %s" % error)
        return 2
    agree = total = 0
    for s in AID:
        jev = jev_client.noul(answers, s["id"], -1)
        code = code_safe_aid(s)
        ok = jev >= 0 and (jev >= 0.5) == code
        agree += ok
        total += 1
        print("%-26s code %-5s Jev %.2f %s" % (s["id"], "aid" if code else "hold", jev, "" if ok else "<-- DISAGREE"))
    for s in TARGETS:
        jev, confidence = jev_client.choice(answers, s["id"])
        code = code_target(s["enemies"])
        ok = jev == code
        agree += ok
        total += 1
        print("%-26s code %-5s Jev %-3s (%.2f) %s" % (s["id"], code, jev, confidence or 0.0, "" if ok else "<-- DISAGREE"))
    for s in DEFENSE_AID:
        jev = jev_client.noul(answers, s["id"], -1)
        code = code_defense_aid(s)
        ok = jev >= 0 and (jev >= 0.5) == code
        agree += ok
        total += 1
        print("%-26s code %-5s Jev %.2f %s" % (s["id"], "aid" if code else "hold", jev, "" if ok else "<-- DISAGREE"))
    for s in DEFENSE_TARGETS:
        jev, confidence = jev_client.choice(answers, s["id"])
        code = code_defense_target(s["enemies"])
        ok = jev == code
        agree += ok
        total += 1
        print("%-26s code %-5s Jev %-3s (%.2f) %s" % (s["id"], code, jev, confidence or 0.0, "" if ok else "<-- DISAGREE"))
    rate = agree / float(total)
    print("Agreement: %d/%d (%.0f%%)" % (agree, total, rate * 100))
    return 0 if rate >= 0.75 else 1


if __name__ == "__main__":
    sys.exit(main())
