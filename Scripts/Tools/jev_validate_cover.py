"""Design-time check of the Sprint 12 cover decisions against TypeSafe Jev's tactical common sense.

The game never calls Jev at run time: the decisions are deterministic C++ rules
(Source/CodexTactics/Private/Tactics/CoverDecisionRules.cpp). This script mirrors them on worded scenarios (Jev is
weak at numbers, so every quantity goes in as words), asks Jev the same questions in one request and prints where the
code and Jev disagree, so the thresholds in CoverDecisionRules can be tuned. Two decisions are covered:

  1. Fire mode at the wall: Corner Peek (lean out, aimed fire) vs Blind Fire (no head exposure, -40 % accuracy) vs
     Hold — from the suppression pressure, the damage he just took, his health, the distance to the enemy, whether the
     cover has a free corner and whether a sniper laser rests on him.
  2. Stance at the wall: Stand vs Crouch — instant crouch under a sniper laser (bIsAimingAtTarget), otherwise by the
     cover height, the pressure, the health, an elevated enemy and the wish to fire aimed over a low wall.

Usage: python Scripts/Tools/jev_validate_cover.py [--dump-scenarios]   (needs the TypeSafe key, see jev_client.py)
Exit code 0 when the agreement is at least 75 %, 1 below, 2 when Jev could not be asked.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import jev_client  # noqa: E402

# --- Mirror of CoverDecisionRules (keep in step with the C++ FCoverDecisionConfig defaults) ---------------------------
PEEK_MIN_HEALTH = 0.35           # below: wounded, peeking is too risky
BLIND_FIRE_DANGER = 0.6          # danger at or above: blind fire
HOLD_DANGER_FAR = 0.85           # far enemy and this much danger: hold
CLOSE_RANGE_M = 6.0              # point-blank: blind fire already at moderate danger
CLOSE_RANGE_DANGER = 0.3
BLIND_FIRE_MAX_M = 25.0          # beyond: blind fire is futile -> peek (or hold)
DAMAGE_NORMALISER = 40.0         # recent incoming damage that counts as "heavy"
LOW_HEALTH_DANGER_BONUS = 0.3
STAND_MAX_SUPPRESSION_LOW = 0.3  # low cover: stand up to fire aimed only under this pressure
CROUCH_SUPPRESSION_HIGH = 0.5    # high cover: crouch from this pressure on

HIGH, LOW = "high", "low"
PEEK, BLIND, HOLD = "peek", "blind", "hold"
STAND, CROUCH = "stand", "crouch"


def danger_score(s):
    danger = 0.5 * s["suppression"] + 0.5 * min(1.0, s["recent_damage"] / DAMAGE_NORMALISER)
    if s["health"] < PEEK_MIN_HEALTH:
        danger += LOW_HEALTH_DANGER_BONUS
    return danger


def code_fire(s):
    if s["height"] == HIGH and not s["edge"]:
        return HOLD  # nothing to shoot around or over: the wall blocks every line
    if s["laser"]:
        return HOLD  # a sniper's dot: nothing shows until the shot has passed (Jev-calibrated 2026-10-06)
    if s["health"] < PEEK_MIN_HEALTH:
        # Badly wounded: never peeks; fires blind only at an enemy at point-blank range (self-defence), else holds.
        return BLIND if s["distance_m"] < CLOSE_RANGE_M else HOLD
    danger = danger_score(s)
    if s["distance_m"] > BLIND_FIRE_MAX_M:
        return PEEK if danger < HOLD_DANGER_FAR else HOLD
    if s["distance_m"] < CLOSE_RANGE_M and danger >= CLOSE_RANGE_DANGER:
        return BLIND
    return BLIND if danger >= BLIND_FIRE_DANGER else PEEK


def code_stance(s):
    if s["laser"]:
        return CROUCH
    if s["height"] == LOW:
        return STAND if (s["wants_aimed_fire"] and s["suppression"] < STAND_MAX_SUPPRESSION_LOW and s["health"] >= PEEK_MIN_HEALTH) else CROUCH
    if s["suppression"] >= CROUCH_SUPPRESSION_HIGH or s["health"] < PEEK_MIN_HEALTH or s["enemy_elevated"]:
        return CROUCH
    return STAND


# --- Words for Jev --------------------------------------------------------------------------------------------------
def health_words(fraction):
    if fraction >= 0.7:
        return "barely scratched"
    if fraction >= PEEK_MIN_HEALTH:
        return "wounded but steady"
    return "badly wounded, one more good hit could drop him"


def suppression_words(level):
    if level <= 0.1:
        return "nobody is shooting at him right now"
    if level < 0.5:
        return "one enemy fires at him now and then"
    return "several enemies hammer his cover with steady fire, chips of concrete fly"


def damage_words(amount):
    if amount <= 0:
        return "he took no damage in the last seconds"
    if amount < 20:
        return "a round grazed him a moment ago"
    return "he was hit hard a moment ago"


def distance_words(m):
    if m < CLOSE_RANGE_M:
        return "point-blank, a few steps away (about %d m)" % round(m)
    if m <= BLIND_FIRE_MAX_M:
        return "close, across the yard (about %d m)" % round(m)
    return "far, at the edge of rifle range (about %d m)" % round(m)


def cover_words(s):
    if s["height"] == HIGH:
        return "a solid wall taller than a man, too tall to fire over" + (
            ", with a free corner he could lean out of or fire blind around" if s["edge"]
            else "; no corner within reach, so nothing can be fired around or over it")
    return "a knee-high barricade he can shoot over"


FIRE = [
    # id, height, edge, laser, health, suppression, recent_damage, distance_m
    {"id": "f_calm_far", "height": HIGH, "edge": True, "laser": False, "health": 0.9, "suppression": 0.0, "recent_damage": 0, "distance_m": 22},
    {"id": "f_calm_close", "height": HIGH, "edge": True, "laser": False, "health": 0.9, "suppression": 0.0, "recent_damage": 0, "distance_m": 10},
    {"id": "f_heavy_fire_close", "height": HIGH, "edge": True, "laser": False, "health": 0.8, "suppression": 0.9, "recent_damage": 30, "distance_m": 9},
    {"id": "f_heavy_fire_far", "height": HIGH, "edge": True, "laser": False, "health": 0.8, "suppression": 0.9, "recent_damage": 30, "distance_m": 24},
    {"id": "f_wounded_light", "height": HIGH, "edge": True, "laser": False, "health": 0.2, "suppression": 0.3, "recent_damage": 10, "distance_m": 11},
    {"id": "f_wounded_calm_far", "height": HIGH, "edge": True, "laser": False, "health": 0.25, "suppression": 0.0, "recent_damage": 0, "distance_m": 20},
    {"id": "f_point_blank_some", "height": HIGH, "edge": True, "laser": False, "health": 0.9, "suppression": 0.3, "recent_damage": 15, "distance_m": 4},
    {"id": "f_point_blank_calm", "height": HIGH, "edge": True, "laser": False, "health": 0.9, "suppression": 0.0, "recent_damage": 0, "distance_m": 4},
    {"id": "f_laser_close", "height": HIGH, "edge": True, "laser": True, "health": 0.9, "suppression": 0.2, "recent_damage": 0, "distance_m": 12},
    {"id": "f_laser_far", "height": HIGH, "edge": True, "laser": True, "health": 0.9, "suppression": 0.2, "recent_damage": 0, "distance_m": 28},
    {"id": "f_no_corner", "height": HIGH, "edge": False, "laser": False, "health": 0.9, "suppression": 0.5, "recent_damage": 0, "distance_m": 10},
    {"id": "f_low_calm", "height": LOW, "edge": True, "laser": False, "health": 0.9, "suppression": 0.1, "recent_damage": 0, "distance_m": 12},
    {"id": "f_low_heavy", "height": LOW, "edge": True, "laser": False, "health": 0.6, "suppression": 0.8, "recent_damage": 25, "distance_m": 8},
    {"id": "f_light_fire_mid", "height": HIGH, "edge": True, "laser": False, "health": 0.6, "suppression": 0.4, "recent_damage": 0, "distance_m": 13},
]

STANCE = [
    # id, height, laser, health, suppression, enemy_elevated, wants_aimed_fire
    {"id": "s_high_calm", "height": HIGH, "laser": False, "health": 0.9, "suppression": 0.0, "enemy_elevated": False, "wants_aimed_fire": True},
    {"id": "s_high_heavy", "height": HIGH, "laser": False, "health": 0.9, "suppression": 0.8, "enemy_elevated": False, "wants_aimed_fire": True},
    {"id": "s_high_laser", "height": HIGH, "laser": True, "health": 0.9, "suppression": 0.1, "enemy_elevated": False, "wants_aimed_fire": True},
    {"id": "s_high_wounded", "height": HIGH, "laser": False, "health": 0.2, "suppression": 0.1, "enemy_elevated": False, "wants_aimed_fire": False},
    {"id": "s_high_elevated", "height": HIGH, "laser": False, "health": 0.9, "suppression": 0.2, "enemy_elevated": True, "wants_aimed_fire": False},
    {"id": "s_low_fire_calm", "height": LOW, "laser": False, "health": 0.9, "suppression": 0.1, "enemy_elevated": False, "wants_aimed_fire": True},
    {"id": "s_low_fire_heavy", "height": LOW, "laser": False, "health": 0.9, "suppression": 0.8, "enemy_elevated": False, "wants_aimed_fire": True},
    {"id": "s_low_laser", "height": LOW, "laser": True, "health": 0.9, "suppression": 0.0, "enemy_elevated": False, "wants_aimed_fire": True},
    {"id": "s_low_wait", "height": LOW, "laser": False, "health": 0.9, "suppression": 0.1, "enemy_elevated": False, "wants_aimed_fire": False},
    {"id": "s_low_wounded_fire", "height": LOW, "laser": False, "health": 0.2, "suppression": 0.1, "enemy_elevated": False, "wants_aimed_fire": True},
]

FIRE_OPTIONS = {
    PEEK: "Lean out of the cover and fire aimed shots at the enemy, exposing his head and shoulders for a moment",
    BLIND: "Fire blind around / over the cover without showing his head: far less accurate, but he stays protected",
    HOLD: "Hold fire and stay fully behind the cover for now",
}
STANCE_OPTIONS = {
    STAND: "Stand upright against the cover",
    CROUCH: "Crouch low behind the cover",
}


def describe_fire(s):
    return {"cover": cover_words(s),
            "enemy": "a hostile shooter " + distance_words(s["distance_m"]),
            "fire on him": suppression_words(s["suppression"]),
            "recent damage": damage_words(s["recent_damage"]),
            "health": health_words(s["health"]),
            "sniper laser": "a sniper's laser dot rests on his cover right where his head would appear" if s["laser"] else "no sniper laser on him"}


def describe_stance(s):
    return {"cover": cover_words(dict(s, edge=False)),
            "fire on him": suppression_words(s["suppression"]),
            "health": health_words(s["health"]),
            "enemy position": "an enemy shoots from a rooftop above him" if s["enemy_elevated"] else "the enemies are on his level",
            "intent": "he wants to fire aimed shots at them right now" if s["wants_aimed_fire"] else "he is waiting, not about to fire",
            "sniper laser": "a sniper's laser dot has just settled on him" if s["laser"] else "no sniper laser on him"}


def build():
    state = {"setting": "Arctic tactical shooter. A rifleman presses his back to cover: either a wall taller than a man (he can "
                        "only shoot by leaning out of a corner, or fire blind around it) or a knee-high barricade (he can "
                        "shoot over it standing, or crouch fully behind it). Mutant shooters and snipers fire at the squad; a "
                        "sniper's laser dot means a lethal shot follows within two seconds. Blind fire lands roughly six "
                        "hits where aimed fire lands ten but never exposes his head.",
             "fire": {}, "stance": {}}
    questions = {}
    for s in FIRE:
        state["fire"][s["id"]] = describe_fire(s)
        questions[s["id"]] = {"type": "choice", "instructions": "Look at `fire.%s`. What should the rifleman do right now?" % s["id"],
                              "criteria": FIRE_OPTIONS}
    for s in STANCE:
        state["stance"][s["id"]] = describe_stance(s)
        questions[s["id"]] = {"type": "choice", "instructions": "Look at `stance.%s`. Which stance should the rifleman take at the cover?" % s["id"],
                              "criteria": STANCE_OPTIONS}
    return state, questions


def main():
    if "--dump-scenarios" in sys.argv:
        for s in FIRE:
            print("fire   %-22s -> %s" % (s["id"], code_fire(s)))
        for s in STANCE:
            print("stance %-22s -> %s" % (s["id"], code_stance(s)))
        return 0
    state, questions = build()
    answers, error = jev_client.ask(state, questions)
    if answers is None:
        print("Jev not asked: %s" % error)
        return 2
    agree = total = 0
    for s in FIRE:
        code = code_fire(s)
        jev, confidence = jev_client.choice(answers, s["id"])
        ok = jev == code
        agree += ok
        total += 1
        print("%-22s code %-6s Jev %-6s (%.2f) %s" % (s["id"], code, jev, confidence or 0.0, "" if ok else "<-- DISAGREE"))
    for s in STANCE:
        code = code_stance(s)
        jev, confidence = jev_client.choice(answers, s["id"])
        ok = jev == code
        agree += ok
        total += 1
        print("%-22s code %-6s Jev %-6s (%.2f) %s" % (s["id"], code, jev, confidence or 0.0, "" if ok else "<-- DISAGREE"))
    rate = agree / float(total)
    print("Agreement: %d/%d (%.0f%%)" % (agree, total, rate * 100))
    return 0 if rate >= 0.75 else 1


if __name__ == "__main__":
    sys.exit(main())
