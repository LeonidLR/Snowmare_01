"""Design-time check of the Sprint 09 tripwire choices that the directive left open, against TypeSafe Jev.

The game never calls Jev at run time; the rules are C++ (Source/CodexTactics/Private/Interactables/TripwireRules.cpp,
ATripwireActor, URelocationSubsystem). This script states each open choice as a worded scenario, asks Jev in one
request and prints where the code and Jev disagree.

Usage: python Scripts/Tools/jev_validate_tripwire.py      (needs the TypeSafe key, see jev_client.py)
Exit code 0 when the agreement is at least 75 %, 1 below, 2 when Jev could not be asked.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import jev_client  # noqa: E402

# Each choice: the scenario, the yes/no question, what the code does (True = yes).
CHOICES = [
    ("arming_delay", "A sapper has just finished stretching a tripwire with two grenades and still kneels right next to it.",
     "Should the wire stay safe for a moment (about a second and a half) so he can step away before it is live?", True),
    ("rigger_ignored", "The soldier who rigged the wire is still standing over it when it goes live.",
     "Should his own first step off the wire be ignored, so only a later crossing can set it off?", True),
    ("enemies_avoid", "Mutants charge across a snowfield where the squad hid a thin tripwire 30 cm above the snow.",
     "Should the mutants path around the wire as if they knew it was there?", False),
    ("squad_avoids", "The squad's own soldiers walk around their position where they stretched a tripwire.",
     "Should they route around their own wire whenever there is a way round?", True),
    ("medic_rigs", "The commander orders a tripwire; the squad has a trained medic-sapper free.",
     "Should the sapper be the one to go and rig it rather than the commander himself?", True),
    ("only_sapper_disarms", "A live tripwire must be removed; a rifleman and a trained sapper are both nearby.",
     "Should only the trained sapper be allowed to disarm it?", True),
    ("fumble_rare", "A trained sapper calmly disarms a two-grenade tripwire lying flat beside it.",
     "Should he usually (about nine times in ten) recover both grenades intact?", True),
    ("squad_hurt_less", "A tripwire's two grenades blow up next to a soldier and a mutant standing equally close.",
     "Should the soldier take somewhat less damage than the mutant (body armour, training to drop)?", True),
    ("crawl_under", "A soldier crawls flat on his belly under a wire stretched 30 cm above the snow.",
     "Can he pass under it without setting it off?", True),
]


def main():
    state = {"setting": "Arctic tactical shooter. The squad rigs Soviet MUV-3 tripwires with two F-1 grenades between two "
                        "anchors, 1 to 5 m apart, 30 cm above the snow; anyone walking into the wire pulls the pin.",
             "choices": {c[0]: c[1] for c in CHOICES}}
    questions = {c[0]: {"type": "noul", "instructions": "Look at `choices.%s`. %s" % (c[0], c[2])} for c in CHOICES}
    answers, error = jev_client.ask(state, questions)
    if answers is None:
        print("Jev not asked: %s" % error)
        return 2
    agree = 0
    for cid, _, _, code in CHOICES:
        jev = jev_client.noul(answers, cid, -1)
        ok = jev >= 0 and (jev >= 0.5) == code
        agree += ok
        print("%-20s code %-4s Jev %.2f %s" % (cid, "yes" if code else "no", jev, "" if ok else "<-- DISAGREE"))
    rate = agree / float(len(CHOICES))
    print("Agreement: %d/%d (%.0f%%)" % (agree, len(CHOICES), rate * 100))
    return 0 if rate >= 0.75 else 1


if __name__ == "__main__":
    sys.exit(main())
