"""Design-time check of the Rifle_2 locomotion choices (ABP_Operative_Rifle2) against TypeSafe Jev's common sense.

The game never calls Jev at run time; the rules are C++ (Source/CodexTactics/Private/Characters/RifleLocomotionRules.cpp,
mirrored below). Each scenario states where the soldier must turn / walk relative to his facing as a clock position
(Jev is weak at numbers), asks which clip fits and compares with the code — including the 180° flip turn.

Usage: python Scripts/Tools/jev_validate_locomotion.py      (needs the TypeSafe key, see jev_client.py)
Exit code 0 when the agreement is at least 75 %, 1 below, 2 when Jev could not be asked.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import jev_client  # noqa: E402

# --- Mirror of RifleLocomotionRules -----------------------------------------------------------------------------
SECTORS = ("F", "FR", "RR", "BR", "B", "BL", "LL", "FL")
TURN_TRIGGER = 40.0


def sector(direction):
    wrapped = (direction + 180.0) % 360.0 - 180.0
    positive = wrapped + 360.0 if wrapped < 0 else wrapped
    return SECTORS[int(math.floor((positive + 22.5) / 45.0)) % 8]


def turn(target_deg):
    """The soldier must face target_deg (clockwise from his facing). Code: offset = -target; returns (clip, side) or None."""
    offset = -target_deg
    if abs(offset) < TURN_TRIGGER:
        return None
    right = offset <= 0 or abs(offset) >= 180
    bucket = min(max(round(min(abs(offset), 180) / 45.0) - 1, 0), 3)
    return ("045", "090", "135", "180")[bucket], "R" if right else "L"


def clock(deg):
    hours = round(((deg % 360) / 30.0)) % 12
    return "%d o'clock" % (12 if hours == 0 else hours)


TURNS = [
    ("flip_180", 180, "directly behind him (6 o'clock)"),
    ("right_100", 100, "to his right, a bit behind the shoulder (about 3-4 o'clock)"),
    ("left_95", -95, "to his left (about 9 o'clock)"),
    ("right_50", 50, "ahead and to the right (about 1-2 o'clock)"),
    ("left_135", -135, "behind him on the left (about 7-8 o'clock)"),
    ("left_170", -170, "almost directly behind, slightly to the left (about 6-7 o'clock)"),
]
NO_TURN = [("small_25", 25, "slightly to the right (about 1 o'clock)")]
STARTS = [
    ("start_strafe_right", 95, "to his right (3 o'clock) while he keeps facing ahead"),
    ("start_back_left", -140, "backwards and to the left (about 7-8 o'clock) while he keeps facing ahead"),
    ("start_forward", 10, "straight ahead (12 o'clock)"),
    ("start_back", 165, "backwards (about 6 o'clock) while he keeps facing ahead"),
]
TURN_OPTIONS = {
    "045": "an eighth of a turn (one small pivot step)",
    "090": "a quarter turn",
    "135": "three eighths of a turn",
    "180": "a half turn, about-face",
}
SIDE_OPTIONS = {"L": "turning to his left (counter-clockwise)", "R": "turning to his right (clockwise)"}
START_OPTIONS = {
    "F": "a forward start", "FR": "a forward-right diagonal start", "RR": "a sideways start to the right", "BR": "a backward-right diagonal start",
    "B": "a backward start", "BL": "a backward-left diagonal start", "LL": "a sideways start to the left", "FL": "a forward-left diagonal start",
}


def main():
    setting = ("A rifleman standing still with the rifle at the ready; his body must turn on the spot to a new facing, or he "
               "starts walking in a direction relative to where he faces (he may strafe or back up while facing ahead).")
    state = {"setting": setting, "turns": {}, "starts": {}}
    questions = {}
    for tid, _, where in TURNS + NO_TURN:
        state["turns"][tid] = "He must now face %s." % where
    for tid, _, where in TURNS:
        questions[tid + "_size"] = {"type": "choice", "instructions": "Look at `turns.%s`. Which stand-turn clip fits best?" % tid,
                                    "criteria": TURN_OPTIONS}
        questions[tid + "_side"] = {"type": "choice", "instructions": "Look at `turns.%s`. Which way should he turn?" % tid,
                                    "criteria": SIDE_OPTIONS}
    for tid, _, _ in NO_TURN:
        questions[tid] = {"type": "noul", "instructions": "Look at `turns.%s`. Does this need a separate turn-in-place step "
                                                          "(rather than just a slight lean of the upper body)?" % tid}
    for sid, _, where in STARTS:
        state["starts"][sid] = "He starts walking %s." % where
        questions[sid] = {"type": "choice", "instructions": "Look at `starts.%s`. Which walk-start clip fits?" % sid, "criteria": START_OPTIONS}
    answers, error = jev_client.ask(state, questions)
    if answers is None:
        print("Jev not asked: %s" % error)
        return 2
    agree = total = 0
    for tid, deg, _ in TURNS:
        size, side = turn(deg)
        jev_size, c1 = jev_client.choice(answers, tid + "_size")
        jev_side, c2 = jev_client.choice(answers, tid + "_side")
        ok_size = jev_size == size
        # A full flip may go either way: only the size counts there.
        ok_side = jev_side == side or deg in (180, -180)
        agree += ok_size + ok_side
        total += 2
        print("%-16s code %s %s  Jev %s (%.2f) %s (%.2f) %s" % (tid, size, side, jev_size, c1 or 0.0, jev_side, c2 or 0.0,
              "" if ok_size and ok_side else "<-- DISAGREE"))
    for tid, deg, _ in NO_TURN:
        jev = jev_client.noul(answers, tid, -1)
        code = turn(deg) is not None
        ok = jev >= 0 and (jev >= 0.5) == code
        agree += ok
        total += 1
        print("%-16s code %-6s Jev %.2f %s" % (tid, "turn" if code else "lean", jev, "" if ok else "<-- DISAGREE"))
    for sid, deg, _ in STARTS:
        code = sector(deg)
        jev, confidence = jev_client.choice(answers, sid)
        ok = jev == code
        agree += ok
        total += 1
        print("%-16s code %-6s Jev %-3s (%.2f) %s" % (sid, code, jev, confidence or 0.0, "" if ok else "<-- DISAGREE"))
    rate = agree / float(total)
    print("Agreement: %d/%d (%.0f%%)" % (agree, total, rate * 100))
    return 0 if rate >= 0.75 else 1


if __name__ == "__main__":
    sys.exit(main())
