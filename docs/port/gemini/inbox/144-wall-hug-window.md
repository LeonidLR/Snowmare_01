# Gemini 144 (L3, Hydrogen + Neon; Lead reviews) - wall hug along a wall with windows: duck under, fire through (designer 2026-11-26)

Designer (X6, screenshot: two operatives hugging an outer wall with a row of windows, "Hugging the wall, holding the corner!"): when an operative
hugs a wall and is ordered along it, at a window he must CROUCH and pass under it (not walk past upright in the opening), and he must be able to
stop at the window and FIRE through it when needed. Today he has no way to shoot through a window from a wall hug - it blocks his tactics.
We already have the under-window anims (window cover / lean, Grok G016 window cover fire, docs/port/grok/outbox/G016*; window vault data 11-21).
Wanted:
1. Shimmy / walk along a hugged wall: a window opening on the path = crouch-walk under the sill for its width (+ margin), stand up after it.
2. A cover / wall-hug order whose slot lands at a window (or the player clicks the window from inside or outside on the wall-hug side):
   the slot = the window cover spot - crouched under the sill, pops up to fire through the opening (G016 logic), back down when not firing.
3. Autonomy / fire mode: an operative under a window with an enemy in the window's arc fires through it per his fire mode (AGG / DEF / PAS).
4. Same for PMC (weapons and tactics are for all sides) if it reuses the same component - report if not.
- Pure rules (window interval on the wall-hug path, crouch span, window slot pick) -> unit tests. Smoke on X6 (that building, street x1_10 side):
  wall hug -> order along the wall past 2 windows -> crouched under both; order to a window slot -> enemy outside -> fires through it.
ADDED by the designer 2026-11-27 (same task, same rules as the wall-edge corner peek that works today):
5. WINDOW EDGE = A CORNER: an operative STANDING next to a window opening (beside the frame, in the wall plane) takes the window edge like a
   wall corner: the corner peek / lean-out round the frame (CornerAim / PeekSightRules, OperativeCharacterCornerAim.cpp) and fires out of the
   window from the side, back behind the wall when not firing. Left and right frame edges, standing and crouched where the sill allows.
6. BLIND FIRE UNDER THE SILL: crouched under a window he can blind-fire - stays down, raises the arm / weapon over the sill and sprays
   (the low-cover blind fire that today works only on roof parapets / low covers: IsCoverShotBlind, ECoverFireMode::BlindFire,
   OperativeCharacterFireGate.cpp, G023 parapet fire in OperativeCharacterCover.cpp ~727). Windows are not low-cover slots today, which is
   probably why it never triggers there - make the under-window slot a low-cover-like slot with the sill as its top.
Smoke steps for 5 and 6 (full smoke names; extend WindowCoverFireSmoke or the 144 smoke): standing beside a window -> peeks round the frame and
hits a target outside; under the window in BlindFire mode -> shots leave over the sill while he stays crouched.
- Run headless via Scripts/Tools/agy_task.ps1 (lane C:/Work/Lanes/CodexTactics-144); post "144 MERGE-READY | <sha> | ..." via agent_chat.py --as gemini.
- Branch gemini/144-wall-hug-window from origin/main; build via machine_dispatch.py; report docs/port/gemini/outbox/144-wall-hug-window.report.md;
  FINISH `144 FINISH | ...`. Queue (Lead 2026-11-27): Gemini 135b -> 154 -> 144.
