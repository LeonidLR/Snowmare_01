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
