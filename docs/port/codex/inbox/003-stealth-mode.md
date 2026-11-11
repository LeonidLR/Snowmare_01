# 003 - Stealth mode (the designer's task, 2026-11-11) - on interim/2026-11-12

Codex implements it (C++ + data + the existing editor-Python retarget pattern), on a task branch `codex/stealth-mode` merged into
`interim/2026-11-12` after its checks. Read docs/port/codex/README.md + INTERIM_LEAD.md rules first.

## What the designer wants
A full stealth mode for one operative or several (the selected ones), toggled by a BUTTON on the bottom HUD panel (the toolbar row with
INV / WHISTLE / THROW / PAS / DEF / AGG ... - add "STEALTH", plus a hotkey; say which key you chose and that it does not clash).
In stealth an operative:
1. looks semi-transparent grey (an overlay / material param on his meshes - reuse the overlay pattern of DefenseMarkerSubsystem /
   the PMC arena team colours; no new asset needed if a MID of an existing overlay material works);
2. is detected MUCH less by enemies (the vision / perception rules: SightRules, the enemy awareness / detection code, noise / hearing if
   present) - data keys, default detection range x0.4 and detection build-up x0.5; firing or sprinting breaks stealth for that man
   (data: stealth_break_on_fire, re-enter needs the button again);
3. moves CROUCHED in 8 directions with the pack /Game/StealthLocomotion (the designer's pack: Animations/*: Stealth_Slow_*_IP /
   Stealth_Fast_*_IP 8 directions with *_Start_IP, Stealth_Idle_IP, Stealth_Crouch_IP ...) - a native layer like the injured / crawl
   layers (weapon_anim_sets.json block "stealth", OperativeAnimInstance* pattern, phase-matched direction blend, play rate by speed);
4. presses against a wall like today's cover / wall hug (Stealth_LeftWall_* / Stealth_RightWall_*: Idle, Slow, Fast, Start / Stop,
   Turn_180) - reuse the cover / wall-hug logic, swap the clips;
5. peeks round a corner (Stealth_*Wall_Lean_*) - and while peeking he REVEALS every enemy behind the corner that we cannot see from
   above: reuse the corner peek sight (the peek cone, PeekSight / UpdatePeekTrapSpotting) so peeked enemies become visible / marked on
   the map for the squad (say exactly how they are shown and for how long).
Retarget: the pack is on its own skeleton (check: /Game/StealthLocomotion/Characters). Make IK-retargeted copies for the squad's UE4
male / female bodies with a script modelled on Scripts/Editor/setup_injured_animation.py into the NEW folder
/Game/Characters/Animations/Stealth/{Male,Female} - the ONLY asset writes allowed in this task, and only while the designer's editor is
CLOSED (check the process command line); never modify the pack itself.

## Checks (in the report docs/port/codex/outbox/003-stealth-mode.report.md)
- Build, unit tests for the pure rules (detection multipliers, break rules, toggle), CodexTactics.Animation.* still green.
- A smoke StealthModeSmoke: toggle on -> grey overlay present, crouched 8-direction stealth clips play, an enemy at a distance that
  spots a normal operative does NOT spot the stealthed one (and does at x0.4 range), wall press + lean play, a peek reveals an enemy
  hidden behind a corner, firing breaks stealth. Existing smokes: InjuredLocomotionSmoke, ProneCrawlSmoke, CoverSwitchSmoke-type cover
  smokes, FormationLabSmoke, SoloStartSmoke.
- Frames from the game camera (stealthed operative, wall press, peek with a revealed enemy) into the MAIN repo Saved/Orchestrator/stealth/.
- Open design questions to the designer in the chat (Russian) with a recommended default - do not block on them.
