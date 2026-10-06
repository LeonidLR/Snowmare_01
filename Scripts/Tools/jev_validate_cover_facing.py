"""Design-time check of the cover facing tie-breaks (user design rule 2026-10-06) against TypeSafe Jev.

The user's core rule is fixed: in cover the operative keeps his back against the wall and faces ALONG it towards the
side of the last known enemy; a shimmy towards that side is forward, away from it backwards. Only the ambiguous
tie-breaks are calibrated here — which threat wins (priority target > seen > heard / ghost silhouette, nearest within a
class), the hysteresis near the wall's centre (the threat must lie THREAT_SIDE_HYSTERESIS_M past the slot on the other
side to turn him) and the default with no threat known (the nearest exposed edge). The game never calls Jev at run
time: the rule is C++ (Source/CodexTactics/Private/Tactics/CoverFacingRules.cpp); this script mirrors it.

Usage: python Scripts/Tools/jev_validate_cover_facing.py [--dump-scenarios]   (needs the TypeSafe key, see jev_client.py)
Exit code 0 when the agreement is at least 75 %, 1 below, 2 when Jev could not be asked.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import jev_client  # noqa: E402

# --- Mirror of CoverFacingRules (keep in step with FCoverFacingConfig) -------------------------------------------------
THREAT_SIDE_HYSTERESIS_M = 0.75
SOURCE_RANK = {"priority": 0, "seen": 1, "heard": 2}
LEFT, RIGHT = "left", "right"


def pick_threat(threats):
    best = None
    for t in threats:
        if best is None or (SOURCE_RANK[t["source"]], t["distance_m"]) < (SOURCE_RANK[best["source"]], best["distance_m"]):
            best = t
    return best


def code_side(s):
    threat = pick_threat(s["threats"])
    if threat is None:
        edges = s.get("edges", {})
        if edges:
            return min(edges, key=edges.get)  # nearest exposed edge
        return s["facing"]
    along = threat["along_m"]
    if s["facing"] == RIGHT:
        return LEFT if along < -THREAT_SIDE_HYSTERESIS_M else RIGHT
    return RIGHT if along > THREAT_SIDE_HYSTERESIS_M else LEFT


def T(source, along_m, distance_m, kind="a mutant"):
    return {"source": source, "along_m": along_m, "distance_m": distance_m, "kind": kind}


SCENARIOS = [
    {"id": "seen_vs_heard_closer", "facing": RIGHT, "threats": [T("seen", -6, 12), T("heard", 4, 5)]},
    {"id": "heard_only_left", "facing": RIGHT, "threats": [T("heard", -5, 8)]},
    {"id": "ghost_vs_fresh_sighting", "facing": LEFT, "threats": [T("heard", -7, 9, "the silhouette where a mutant was last seen"), T("seen", 6, 15)]},
    {"id": "two_seen_near_left", "facing": RIGHT, "threats": [T("seen", -4, 7), T("seen", 9, 14)]},
    {"id": "two_seen_near_right", "facing": LEFT, "threats": [T("seen", 3, 6), T("seen", -10, 20)]},
    {"id": "priority_far_vs_seen_near", "facing": LEFT, "threats": [T("priority", 8, 22, "the enemy he was ordered to shoot"), T("seen", -3, 6)]},
    {"id": "dead_ahead_keeps", "facing": RIGHT, "threats": [T("seen", 0.0, 10)]},
    {"id": "slightly_left_keeps", "facing": RIGHT, "threats": [T("seen", -0.4, 10)]},
    {"id": "slightly_right_keeps", "facing": LEFT, "threats": [T("seen", 0.5, 12)]},
    {"id": "clearly_left_turns", "facing": RIGHT, "threats": [T("seen", -2.5, 10)]},
    {"id": "clearly_right_turns", "facing": LEFT, "threats": [T("seen", 3, 9)]},
    {"id": "flank_from_left", "facing": RIGHT, "threats": [T("seen", -6, 7)]},
    {"id": "no_threat_left_edge_near", "facing": RIGHT, "threats": [], "edges": {LEFT: 0.8}},
    {"id": "no_threat_both_edges", "facing": LEFT, "threats": [], "edges": {LEFT: 1.6, RIGHT: 0.6}},
    {"id": "no_threat_no_edge", "facing": LEFT, "threats": []},
    {"id": "two_heard_opposite", "facing": LEFT, "threats": [T("heard", 5, 6), T("heard", -4, 11)]},
    {"id": "seen_far_vs_ghost_near", "facing": RIGHT, "threats": [T("seen", -9, 25), T("heard", 2, 4, "a fresh silhouette of a hound heard a moment ago")]},
]

OPTIONS = {
    LEFT: "Face left along the wall (towards the left end of the wall)",
    RIGHT: "Face right along the wall (towards the right end of the wall)",
}


def threat_words(t):
    how = {"priority": "in sight, and it is the target he was ordered to shoot",
           "seen": "in plain sight right now",
           "heard": "not visible, only heard / remembered"}[t["source"]]
    if abs(t["along_m"]) < 0.3:
        where = "straight out in front of the wall's middle"
    else:
        where = "%.1f metres to his %s along the wall" % (abs(t["along_m"]), RIGHT if t["along_m"] > 0 else LEFT)
    return "%s, %s, %d metres away, %s" % (t["kind"], where, t["distance_m"], how)


def describe(s):
    case = {"currently facing": "along the wall to his %s" % s["facing"]}
    for i, t in enumerate(s["threats"]):
        case["threat %d" % (i + 1)] = threat_words(t)
    if not s["threats"]:
        case["threats"] = "no enemy seen or heard yet"
        edges = s.get("edges", {})
        case["wall ends"] = ", ".join("the %s end of the wall is %.1f m away" % (k, v) for k, v in edges.items()) if edges else "the wall runs on far to both sides"
    return case


def build():
    state = {"setting": "Arctic tactical shooter. A soldier stands with his back pressed against a long wall, taller than a "
                        "man. He never faces away from the wall; he looks ALONG the wall to one side, so he can lean out "
                        "round the wall's end on that side and shoot, and step along the wall forward towards that side. "
                        "Turning round to the other side takes a moment and he should not flip back and forth for "
                        "enemies that are almost straight in front. Enemies in plain sight matter more than ones only "
                        "heard; an ordered target matters most.",
             "cases": {}}
    questions = {}
    for s in SCENARIOS:
        state["cases"][s["id"]] = describe(s)
        questions[s["id"]] = {"type": "choice", "instructions": "Look at `cases.%s`. Which way along the wall should the soldier face now?" % s["id"],
                              "criteria": OPTIONS}
    return state, questions


def main():
    if "--dump-scenarios" in sys.argv:
        for s in SCENARIOS:
            print("%-26s -> %s" % (s["id"], code_side(s)))
        return 0
    state, questions = build()
    answers, error = jev_client.ask(state, questions)
    if answers is None:
        print("Jev not asked: %s" % error)
        return 2
    agree = 0
    for s in SCENARIOS:
        code = code_side(s)
        jev, confidence = jev_client.choice(answers, s["id"])
        ok = jev == code
        agree += ok
        print("%-26s code %-6s Jev %-6s (%.2f) %s" % (s["id"], code, jev, confidence or 0.0, "" if ok else "<-- DISAGREE"))
    rate = agree / float(len(SCENARIOS))
    print("Agreement: %d/%d (%.0f%%)" % (agree, len(SCENARIOS), rate * 100))
    return 0 if rate >= 0.75 else 1


if __name__ == "__main__":
    sys.exit(main())
