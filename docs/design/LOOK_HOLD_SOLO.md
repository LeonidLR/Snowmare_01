# 059 Look hold outside stealth (solo walk / jog / crouch / prone)

Designer request 2026-11-15: the stealth look anchor works well; give the same "fix the look, move sideways" control in normal
solo movement, including prone (crawl sideways / backwards while watching a point).
Scout: `docs/port/codex/outbox/059-lookhold-scout.report.md` (clips + code entry points).

## Designer decisions (2026-11-15)

1. **Gesture = merge with the watch sector.** Outside stealth, Shift + click on the ground sets a look point:
   - standing still, he watches that point (replaces today's watch-sector behaviour; same click);
   - while moving (walk / jog / crouch walk / crawl), he keeps body and weapon on the point and moves sideways or backwards.
   - Shift + click on an enemy / silhouette / near known enemies = suppressive fire (unchanged, takes precedence).
   - Shift + click on the operative himself = clear the look point (as in stealth: "LOOK CLEARED").
   - Stealth keeps its own anchor path unchanged (it already runs before the suppress check).
2. **Speed: walk and jog sideways / backwards are allowed; sprint (double click) clears the look point** automatically
   (floating text "LOOK CLEARED"). No locked-body sprint: there are no sprint strafe clips.

## Behaviour rules

- Applies in solo mode to the selected operative(s); in formation the leader only (followers keep formation facing).
- The look point is cleared by: sprint, entering cover, a new Shift + click elsewhere (replaces it), Shift + click on self,
  death / knockdown / panic / ladder / vault / carry, turn-based combat start.
- Combat precedence stays as in `OperativeCharacterAim.cpp` UpdateCombatFacing: an explicit fire order (quick fire, suppress,
  targeted shot) turns him to the target; when that order ends he returns to the look point.
- A look point within 50 cm of him is ignored (same as stealth `GetStealthLookPoint`).
- Body twist limit while moving: reuse the stealth residual-twist clamp idea, but separate tunables for standing and prone
  (prone crawl turns the whole body; no bone twist beyond the prone layer's own limit).

## Animation

- Standing walk / jog: 8-direction loops exist (mannequin rifle Walk/Jog, shotgun Walk/Jog; RifleAnims lacks forward diagonals,
  blend from F + L/R). Direction = velocity yaw minus actor yaw (`OperativeAnimInstance.cpp:1084`), already computed.
- Crouch walk: RifleAnims crouch 6 directions (no forward diagonals), shotgun crouch 4.
- Prone: `Crawl_Walk_{F,L,R,B,45L,45R,135L,135R}_IPC` 8 loops; `OperativeAnimInstanceProneCrawl.cpp` already blends 8 directions
  by relative heading - mostly a facing change, keep the wounded / full-body exclusions.
- Starts / stops: Rifle2 walk has sector starts/stops; use the stealth `GetGaitStartSector` approach (lead sector when the look
  holds the body). Jog sideways without dedicated starts: blend in, no start clip.

## Implementation outline

1. Extract neutral helpers from `StealthModeRules` (direction sector, gait-start sector, look-point validity) into a shared
   `LookHoldRules` (pure, unit-tested); stealth keeps calling them.
2. `AOperativeCharacter`: `SetManualLookPoint / ClearManualLookPoint / HasManualLookPoint` used by both stealth and solo;
   stealth wrappers stay for compatibility. Facing: `UpdateCombatFacing` look eligibility no longer stealth-only.
3. Controller (`CodexTacticsPlayerControllerOrders.cpp` ~365-417): outside stealth, Shift + click ground -> look point
   (merging the watch-sector order); Shift + click self -> clear; suppress precedence unchanged.
4. Anim: standing / crouch directional selection when the look holds the body; prone crawl uses the look point as facing.
5. Sprint / cover / stance / order-change clearing per the rules above.

## Tests

- Unit: LookHoldRules sectors, validity (50 cm), clearing matrix (sprint, cover, knockdown...).
- Smoke (L_MovementTest, solo): set look point 90° left, move forward 6 m at walk and at jog -> actor yaw stays within 15° of
  the point, velocity heading ~90° off; prone crawl same at crawl speed; double-click sprint clears the point; Shift + click
  on an enemy still issues suppress. Regression: StealthModeSmoke, ProneCrawl smoke, SoloStartSmoke, SquadControlSmoke.
