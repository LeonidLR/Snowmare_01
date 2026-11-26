# Gemini 135 (Hydrogen; Lead reviews) - corner look turns the body both ways (designer playtest 2026-11-26, sector 1)

Designer: corner take + peek is better now. Shift+click changes the look direction at a corner, but the BODY pose only follows when the look goes
to the LEFT; to the right (and generally while standing at a corner) the body stays put. The body must follow the look in both directions, also
when the operative stands at the corner. Use the aim offset (AO yaw + pitch) in the anim instance, clamped to the AO range; rotate the actor only
when the look leaves the AO range (and only if the corner rules allow it).
- Area: OperativeCharacterCornerAim.cpp (G025 cone / slot / AO + G023 roof over-top, already merged together on your G023 branch),
  OperativeAnimInstance (AO inputs), CoverFacingRules / RunCornerPeekRules. Find why only the left side feeds the AO (sign / side mirror / clamp).
- Pure rules -> unit tests for both sides (left / right corner x look left / right). Smoke: extend CornerPeek / TorsoDrift smoke with a right-look check.
- Branch gemini/135-corner-look-body from origin/main AFTER G023 is merged. Build via machine_dispatch.py; report docs/port/gemini/outbox/135-corner-look-body.report.md;
  FINISH line `135 FINISH | PASS/FAIL | <hash> | ...`. Queue: right after G023, before 121.

## 135 part A - DO FIRST: operative at a corner peek ignores a move order (designer 2026-11-26, play copy f25a9b2c, X6 sector 1)
An operative peeking around a corner gets a click order to run to a cover on the other side: the cover ghost shows at the target, the click is
accepted, but he stays at the corner and never leaves. Suspects: G025 corner peek state / RunPeekYawLock / peek stance yaw lock (BeginPeekStanceYawLock)
not released by a new move order, or the corner-watch hold re-asserting the slot. Repro in a smoke (corner peek -> move order to a cover
>= 5 m away -> must arrive), fix the root cause, keep G025 / G023 behaviour. Same branch gemini/135-corner-look-body, part A commit first.
Designer addendum: the reverse works (from the low cover, an order to run left to the corner is carried out). So it is one-way: corner peek -> cover fails.
Cover orders are two-click (ClickPreview Arm -> confirm, CodexTacticsPlayerControllerClickPreview.cpp ~740): check first whether the 2nd click
while the leader is in CornerPeek re-arms / is dropped instead of confirming (leader-state / peek hold check in the confirm path), then the movement side.
