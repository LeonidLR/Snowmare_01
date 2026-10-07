"""Design-time check of the sustained corner aim rule (user request 2026-10-07) against TypeSafe Jev's common sense.

In a corner fight the operative stays OUT in the corner fire stance (cvr_*_fire_idle, firing cvr_*_fire from it) while
he has targets behind the wall / round the corner; he only breaks the aim and ducks back behind the corner for a
reason. The game never calls Jev at run time: the rule is deterministic C++
(CoverDecisionRules::DecideCornerAim, FCoverDecisionConfig). This script mirrors it on worded scenarios (Jev is weak at
numbers, so every quantity goes in as words), asks Jev the same question in one request and prints where code and Jev
disagree, so the thresholds can be tuned before they are frozen in C++.

Decisions:
  stay          keep aiming round the corner and keep firing from that pose
  duck_reload   duck back behind the corner and reload there (magazine empty, or low during a lull)
  duck_safety   duck back behind the corner for safety (heavy fire, big hit, badly wounded, sniper laser, grenade,
                an enemy flanking on his open side)
  return        no target left for a while: lower the rifle and relax back against the wall

Usage: python Scripts/Tools/jev_validate_corner_aim.py [--dump-scenarios]   (needs the TypeSafe key, see jev_client.py)
Exit code 0 when the agreement is at least 75 %, 1 below, 2 when Jev could not be asked.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import jev_client  # noqa: E402

# --- Mirror of CoverDecisionRules::DecideCornerAim (keep in step with the C++ FCoverDecisionConfig defaults) ----------
PEEK_MIN_HEALTH = 0.35           # below: badly wounded -> duck for safety
AIM_BIG_HIT_DAMAGE = 30.0        # damage taken in the last seconds that counts as a big hit -> duck
AIM_BREAK_SUPPRESSION = 0.7      # incoming fire pressure (two or more shooters on him) at or above -> duck
AIM_NO_TARGET_GRACE_S = 2.0      # no target this long -> relax back to the wall
EARLY_RELOAD_FRACTION = 0.34     # magazine at or below this share during a lull -> duck and reload early
EARLY_RELOAD_LULL_S = 0.5        # "lull": no target in sight for at least this long
AIM_WOUNDED_HEALTH = 0.5         # below this AND hit again in the last seconds -> duck (Jev 2026-10-07)

STAY, RELOAD, SAFETY, RETURN, TURN = "stay", "duck_reload", "duck_safety", "return", "turn_shoot"


def code_decision(s):
    low = s["reserve"] and s["clip"] <= EARLY_RELOAD_FRACTION
    if s["clip"] <= 0:
        return RELOAD if s["reserve"] else SAFETY  # nothing to fire: reload behind the corner (or switch weapons there)
    ranged = s.get("ranged", True)
    if s["flank"] and not s.get("flank_ranged", True) and not s["laser"] and not s["grenade"]:
        # A mutant on his side of the wall: not a corner-aim decision in the game — it is no corner-shot target, so the
        # targeting turns him to it for an open shot off the wall (AOperativeCharacter::BeginOpenShotFromCover).
        return TURN
    if s["laser"] or s["grenade"] or (s["flank"] and s.get("flank_ranged", True)):
        return SAFETY  # 2026-10-07 horde fix: only a RANGED enemy on his open side is a flank (melee rushers are targets)
    if ranged and (s["health"] < PEEK_MIN_HEALTH or s["recent_damage"] >= AIM_BIG_HIT_DAMAGE or s["suppression"] >= AIM_BREAK_SUPPRESSION
                   or (s["health"] < AIM_WOUNDED_HEALTH and s["recent_damage"] > 0)):
        return RELOAD if low else SAFETY  # forced back anyway: a low magazine is reloaded there (Jev 2026-10-07)
    if low and s["no_target_s"] >= EARLY_RELOAD_LULL_S:
        return RELOAD  # a lull (or the fight over) with a low magazine: reload behind the corner first
    if s["no_target_s"] >= AIM_NO_TARGET_GRACE_S:
        return RETURN
    return STAY


# --- Words for Jev --------------------------------------------------------------------------------------------------
def clip_words(s):
    c = s["clip"]
    if c <= 0:
        text = "his magazine is empty"
    elif c <= EARLY_RELOAD_FRACTION:
        text = "his magazine is almost empty, a few rounds left"
    elif c < 0.7:
        text = "his magazine is about half full"
    else:
        text = "his magazine is nearly full"
    return text + ("; he has spare magazines" if s["reserve"] else "; no spare magazines left")


def melee_words(s):
    n = s.get("melee", 0)
    if n <= 0:
        return "no mutant is rushing him"
    if n == 1:
        return "one clawed mutant (no gun, it only bites and claws) is a couple of steps away and closing in on him"
    return "a pack of %d clawed mutants (no guns, they only bite and claw) is pouring round the corner and rushing at him, the nearest a few steps away" % n


def target_words(t):
    if t <= 0.0:
        return "an enemy is in his sights round the corner right now"
    if t < EARLY_RELOAD_LULL_S + 0.5:
        return "the last enemy he was firing at just dropped out of sight a moment ago (under a second); others may reappear"
    if t < AIM_NO_TARGET_GRACE_S:
        return "no enemy in sight for a second or so; the fight may resume any moment"
    return "no enemy has been in sight round the corner for several seconds"


def health_words(f):
    if f >= 0.7:
        return "barely scratched"
    if f >= PEEK_MIN_HEALTH:
        return "wounded but steady"
    return "badly wounded, one more good hit could drop him"


def fire_words(p):
    if p <= 0.1:
        return "nobody is shooting at him"
    if p < AIM_BREAK_SUPPRESSION:
        return "the one enemy he is dueling fires back at him now and then"
    return "several enemies hammer his corner with steady fire, rounds crack past his head"


def damage_words(d):
    if d <= 0:
        return "he took no hit in the last seconds"
    if d < AIM_BIG_HIT_DAMAGE:
        return "a round grazed him a moment ago"
    return "he took a heavy hit a moment ago"


SCENARIOS = [
    # id, clip share, reserve, no_target_s, health, suppression, recent_damage, laser, grenade, flank
    {"id": "a_duel_full", "clip": 0.9, "reserve": True, "no_target_s": 0.0, "health": 0.9, "suppression": 0.35, "recent_damage": 0, "laser": False, "grenade": False, "flank": False},
    {"id": "a_duel_half", "clip": 0.5, "reserve": True, "no_target_s": 0.0, "health": 0.8, "suppression": 0.35, "recent_damage": 0, "laser": False, "grenade": False, "flank": False},
    {"id": "a_two_targets_calm", "clip": 0.7, "reserve": True, "no_target_s": 0.0, "health": 0.9, "suppression": 0.0, "recent_damage": 0, "laser": False, "grenade": False, "flank": False},
    {"id": "a_grazed_keeps", "clip": 0.6, "reserve": True, "no_target_s": 0.0, "health": 0.7, "suppression": 0.35, "recent_damage": 12, "laser": False, "grenade": False, "flank": False},
    {"id": "a_wounded_steady", "clip": 0.6, "reserve": True, "no_target_s": 0.0, "health": 0.5, "suppression": 0.35, "recent_damage": 0, "laser": False, "grenade": False, "flank": False},
    {"id": "a_low_but_target", "clip": 0.2, "reserve": True, "no_target_s": 0.0, "health": 0.9, "suppression": 0.35, "recent_damage": 0, "laser": False, "grenade": False, "flank": False},
    {"id": "a_empty", "clip": 0.0, "reserve": True, "no_target_s": 0.0, "health": 0.9, "suppression": 0.35, "recent_damage": 0, "laser": False, "grenade": False, "flank": False},
    {"id": "a_empty_heavy_fire", "clip": 0.0, "reserve": True, "no_target_s": 0.0, "health": 0.9, "suppression": 0.8, "recent_damage": 0, "laser": False, "grenade": False, "flank": False},
    {"id": "a_low_lull", "clip": 0.2, "reserve": True, "no_target_s": 1.0, "health": 0.9, "suppression": 0.0, "recent_damage": 0, "laser": False, "grenade": False, "flank": False},
    {"id": "a_half_lull", "clip": 0.5, "reserve": True, "no_target_s": 1.0, "health": 0.9, "suppression": 0.0, "recent_damage": 0, "laser": False, "grenade": False, "flank": False},
    {"id": "a_target_just_lost", "clip": 0.8, "reserve": True, "no_target_s": 0.4, "health": 0.9, "suppression": 0.0, "recent_damage": 0, "laser": False, "grenade": False, "flank": False},
    {"id": "a_no_targets_long", "clip": 0.8, "reserve": True, "no_target_s": 5.0, "health": 0.9, "suppression": 0.0, "recent_damage": 0, "laser": False, "grenade": False, "flank": False},
    {"id": "a_no_targets_low_clip", "clip": 0.2, "reserve": True, "no_target_s": 5.0, "health": 0.9, "suppression": 0.0, "recent_damage": 0, "laser": False, "grenade": False, "flank": False},
    {"id": "a_heavy_fire", "clip": 0.7, "reserve": True, "no_target_s": 0.0, "health": 0.8, "suppression": 0.9, "recent_damage": 10, "laser": False, "grenade": False, "flank": False},
    {"id": "a_big_hit", "clip": 0.7, "reserve": True, "no_target_s": 0.0, "health": 0.6, "suppression": 0.35, "recent_damage": 40, "laser": False, "grenade": False, "flank": False},
    {"id": "a_badly_wounded", "clip": 0.7, "reserve": True, "no_target_s": 0.0, "health": 0.2, "suppression": 0.35, "recent_damage": 0, "laser": False, "grenade": False, "flank": False},
    {"id": "a_laser", "clip": 0.7, "reserve": True, "no_target_s": 0.0, "health": 0.9, "suppression": 0.0, "recent_damage": 0, "laser": True, "grenade": False, "flank": False},
    {"id": "a_grenade", "clip": 0.7, "reserve": True, "no_target_s": 0.0, "health": 0.9, "suppression": 0.35, "recent_damage": 0, "laser": False, "grenade": True, "flank": False},
    {"id": "a_flank", "clip": 0.7, "reserve": True, "no_target_s": 0.0, "health": 0.9, "suppression": 0.35, "recent_damage": 0, "laser": False, "grenade": False, "flank": True},
    {"id": "a_empty_no_reserve", "clip": 0.0, "reserve": False, "no_target_s": 1.0, "health": 0.9, "suppression": 0.0, "recent_damage": 0, "laser": False, "grenade": False, "flank": False},
    {"id": "a_wounded_grazed_duel", "clip": 0.6, "reserve": True, "no_target_s": 0.0, "health": 0.45, "suppression": 0.35, "recent_damage": 15, "laser": False, "grenade": False, "flank": False},
    {"id": "a_heavy_fire_no_target", "clip": 0.7, "reserve": True, "no_target_s": 3.0, "health": 0.9, "suppression": 0.8, "recent_damage": 0, "laser": False, "grenade": False, "flank": False},
    {"id": "a_full_lull_short", "clip": 0.9, "reserve": True, "no_target_s": 1.0, "health": 0.9, "suppression": 0.0, "recent_damage": 0, "laser": False, "grenade": False, "flank": False},
    {"id": "a_low_target_heavy", "clip": 0.2, "reserve": True, "no_target_s": 0.0, "health": 0.9, "suppression": 0.8, "recent_damage": 0, "laser": False, "grenade": False, "flank": False},
    # 2026-10-07 horde regression: melee mutants rushing round the corner are targets, not suppression / flank.
    {"id": "h_melee_horde", "clip": 0.7, "reserve": True, "no_target_s": 0.0, "health": 0.9, "suppression": 0.0, "recent_damage": 0, "laser": False, "grenade": False, "flank": False, "ranged": False, "melee": 6},
    {"id": "h_melee_horde_on_his_side", "clip": 0.6, "reserve": True, "no_target_s": 0.0, "health": 0.8, "suppression": 0.0, "recent_damage": 0, "laser": False, "grenade": False, "flank": True, "flank_ranged": False, "ranged": False, "melee": 5},
    {"id": "h_melee_biting_him", "clip": 0.6, "reserve": True, "no_target_s": 0.0, "health": 0.6, "suppression": 0.0, "recent_damage": 0, "laser": False, "grenade": False, "flank": True, "flank_ranged": False, "ranged": False, "melee": 1},
    {"id": "h_melee_horde_wounded", "clip": 0.6, "reserve": True, "no_target_s": 0.0, "health": 0.3, "suppression": 0.0, "recent_damage": 0, "laser": False, "grenade": False, "flank": False, "ranged": False, "melee": 4},
    {"id": "h_shooters_many", "clip": 0.7, "reserve": True, "no_target_s": 0.0, "health": 0.8, "suppression": 0.9, "recent_damage": 15, "laser": False, "grenade": False, "flank": False, "ranged": True, "melee": 0},
    {"id": "h_horde_one_spitter", "clip": 0.7, "reserve": True, "no_target_s": 0.0, "health": 0.8, "suppression": 0.35, "recent_damage": 0, "laser": False, "grenade": False, "flank": False, "ranged": True, "melee": 5},
    {"id": "h_gunman_flank", "clip": 0.7, "reserve": True, "no_target_s": 0.0, "health": 0.9, "suppression": 0.35, "recent_damage": 0, "laser": False, "grenade": False, "flank": True, "flank_ranged": True, "ranged": True, "melee": 0},
]

OPTIONS = {
    STAY: "Keep aiming round the corner and keep firing from that exposed pose",
    RELOAD: "Duck back behind the corner and reload there, then come back out",
    SAFETY: "Duck back behind the corner right now for safety, without reloading",
    RETURN: "Lower the rifle and relax back against the wall: the fight here is over for now",
    TURN: "Turn away from the corner and shoot the mutant that has reached his side of the wall",
}


def describe(s):
    return {"ammo": clip_words(s),
            "targets": target_words(s["no_target_s"]),
            "health": health_words(s["health"]),
            "fire on him": fire_words(s["suppression"]),
            "recent hit": damage_words(s["recent_damage"]),
            "sniper laser": "a sniper's laser dot has just settled on him" if s["laser"] else "no sniper laser on him",
            "grenade": "an enemy grenade just landed a few metres from him" if s["grenade"] else "no grenade near him",
            "flank": (("a gunman has appeared on his open side, behind his back, a few metres away (the wall does not shield him from it)"
                       if s.get("flank_ranged", True) else "one of the rushing mutants has come round onto his side of the wall, a few metres away")
                      if s["flank"] else "his open side behind him is clear"),
            "mutants": melee_words(s),
            "enemy guns": ("there are enemy gunmen in the fight" if s.get("ranged", True)
                           else "none of the enemies has a gun: every one of them must come to arm's length to hurt him")}


def build():
    state = {"setting": "Arctic tactical shooter. A rifleman fights from the corner of a wall taller than a man: he stands "
                        "leaned out round the corner in a firing stance, rifle shouldered, and shoots at mutants beyond the "
                        "wall. Ducking back behind the corner fully protects him from the enemies beyond the wall but stops "
                        "his fire; leaning out again costs about a second. A sniper's laser dot means a lethal shot follows "
                        "within two seconds. Reloading takes about two seconds.",
             "situations": {}}
    questions = {}
    for s in SCENARIOS:
        state["situations"][s["id"]] = describe(s)
        questions[s["id"]] = {"type": "choice", "instructions": "Look at `situations.%s`. What should the rifleman at the corner do right now?" % s["id"],
                              "criteria": OPTIONS}
    return state, questions


def main():
    if "--dump-scenarios" in sys.argv:
        for s in SCENARIOS:
            print("%-24s -> %s" % (s["id"], code_decision(s)))
        return 0
    state, questions = build()
    answers, error = jev_client.ask(state, questions)
    if answers is None:
        print("Jev not asked: %s" % error)
        return 2
    agree = 0
    for s in SCENARIOS:
        code = code_decision(s)
        jev, confidence = jev_client.choice(answers, s["id"])
        ok = jev == code
        agree += ok
        print("%-24s code %-11s Jev %-11s (%.2f) %s" % (s["id"], code, jev, confidence or 0.0, "" if ok else "<-- DISAGREE"))
    rate = agree / float(len(SCENARIOS))
    print("Agreement: %d/%d (%.0f%%)" % (agree, len(SCENARIOS), rate * 100))
    return 0 if rate >= 0.75 else 1


if __name__ == "__main__":
    sys.exit(main())
