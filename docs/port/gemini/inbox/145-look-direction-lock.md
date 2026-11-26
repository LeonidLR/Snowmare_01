# Gemini 145 (light; Lead reviews) - Shift+click locks a look DIRECTION, not a point (designer 2026-11-26, variant B)

Today Shift+click while moving sets a manual look POINT (AOperativeCharacter::SetManualLookPoint, OperativeCharacterStealth.cpp ~115): the
operative keeps turning his head / body to that point while he walks past it, so the designer re-clicks all the time.
Designer decision (variant B): Shift+click sets a fixed WORLD direction = yaw from the operative to the click point at the moment of the click.
He keeps looking along that compass direction wherever he walks (turns of the path do not change it), within the usual body / AO limits.
- Keep the existing clear / reset behaviour; selection of several operatives: each gets the same world yaw.
- At a cover corner (135 IsAtPeekableCorner / corner look) the same direction lock applies.
- Store yaw (+ pitch from the click height if the AO uses pitch), not a point; saves / loads with the operative if the point did.
- Pure rule (direction from click, keep while path turns) -> unit test. Smoke: walk an L-shaped path after Shift+click east -> look yaw stays
  east +-10 deg on both legs (today it would swing to the point).
- Branch gemini/145-look-direction-lock from origin/main; report docs/port/gemini/outbox/145-look-direction-lock.report.md;
  FINISH `145 FINISH | ...`. Queue: right after 135 (same code), before 141.

## 145 part B - BUG (designer 2026-11-26, play a12dea10, X6, several operatives): after a sprint move he arrives and turns to an OLD direction
Medic-Sapper (then others too): sprints facing the travel direction, arrives, then turns away (to his left) to some old direction. The
designer checked: no look lock shown. Play log has an earlier "[Medic-Sapper]: Watch sector locked!" (Shift+click) and cover entries
"facing left". Suspects: a stale manual look / watch sector surviving new move orders (it should end with a new plain move order unless the
player re-locks; with 145 a direction lock must be visible and clearable), the F-1 "return to the ordered facing after 3 s" using a stale
ordered yaw, or a cover facing kept after leaving the cover. Add one log line on arrival: which rule set the final yaw (manual look /
watch sector / F-1 return / cover facing / travel) and the yaw; fix the stale source. Rule: a plain move order ends at the travel
direction unless a look lock is active (and an active lock is shown on the HUD / vision cone).
Smoke: Shift+click a look, then plain move order elsewhere -> on arrival yaw = travel yaw +-15 deg (or the lock if the design keeps it - report).
Designer: after a new sector round started the bug was gone -> stale per-operative state that the round reset clears (look / watch sector / ordered facing). Reproduce within ONE round: Shift+click, several moves, cover in/out, sprint.
