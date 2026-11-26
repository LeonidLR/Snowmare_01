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
- Branch gemini/144-wall-hug-window from origin/main; build via machine_dispatch.py; report docs/port/gemini/outbox/144-wall-hug-window.report.md;
  FINISH `144 FINISH | ...`. Queue: after 141.
