# Cover, peek and corner aim

> Tactical cover lives in the `Cover*` / `CornerAim` clusters of `AOperativeCharacter`. The corner peek leans the operative's head out past an edge:
> it reveals hidden traps inside a sight cone (growing reach), is the eye point of the peek sight (see [sight-and-vision-cones](sight-and-vision-cones.md))
> and after a knockdown the operative returns to the same slot unless enemies surround him.
>
> Key files:
> - `Private/Characters/OperativeCharacterCover.cpp`, `…CoverEntry.cpp`, `…CornerAim.cpp`; `Interactables/TrapSpotRules`; `Characters/SquadOrderRules`;
> - `Combat/KnockdownRules::DecideCoverReturn`; smokes `CoverSmoke`, `CornerHoldSmoke`, `PeekMinesSmoke`, `KnockdownCoverSmoke`
> - Group cover order (2026-10-20): `Tactics/GroupCoverRules`, `Characters/SquadSubsystemGroupCover.cpp`, `ACodexTacticsPlayerController::OrderGroupCover`; smoke `GroupCoverSmoke`
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Corner peek reveals traps (old §5)

- **The corner peek reveals hidden traps (rev 5h, every level):** `AOperativeCharacter::IsPeekingRoundCorner` (in cover, corner aim active, at a manual peek spot or the corner-hold spot) -> `UpdatePeekTrapSpotting` (called from `UpdateCover`, cluster file `OperativeCharacterCornerAim.cpp`; every `PeekSpotConfig.IntervalSeconds` 0.2 s; not in the wave fight): every `AInteractableActor::IsHiddenTrap()` (an armed hidden level mine; an armed, laid, concealed PMC wire) inside `TrapSpotRules::IsInPeekSightCone(GetCoverFireOrigin(), CoverFacingRules::CornerAimDirection(CoverSlot, CoverFacing, CornerAimOutwardDeg), Target, RangeCm 1200, HalfAngleDeg 30)` with a clear visibility trace (pawns do not block) is revealed by `RevealByPeek` (the normal spot response of part 1; the spotter is not turned, so the pose stays; extra event `TrapSpotted.Peek`). `FTrapPeekSpotConfig PeekSpotConfig` = BP_Operative Class Defaults -> CodexTactics|Cover|Peek (`bEnabled`, `RangeCm`, `HalfAngleDeg`, `bRequireLineOfSight`, `TargetHeightCm` 20, `IntervalSeconds`). **Trap groups:** `AInteractableActor::SpotEventTag` (Details -> CodexTactics|Trap) raises `TrapSpotted.<tag>` and counts in the tutorial condition stat **`traps_spotted.<tag>`** (`TrapSpotRules::ParseSpottedStatTag`, `TutorialRules::IsKnownStat`, `UTutorialSubsystem::GetConditionStat`: traps of that tag no longer hidden). Tests `Interactables.TrapSpot.PeekCone` / `.GroupEventAndStat`; smoke `PeekMinesSmoke`.
- **The peek finds traps PHYSICALLY, one by one (rev 5j, game designer 2026-10-19: "not all lesson mines light up when the peek starts").** `UpdatePeekTrapSpotting` looks from the peek sight's leaned-out eye and aim (`GetPeekSight`, the cone drawn for him) with `FTrapPeekSpotConfig` now `HalfAngleDeg` 45 (= the peek view's 90 deg cone; was 30), `RangeCm` 1200 (shorter than the 18 m enemy view: a mine lies flush with the snow) and new `SweepSeconds` 1.5: the reach grows from 0 to 12 m after he leans out (`TrapSpotRules::PeekSweepRange`, `PeekSpotElapsed`), so the mines are revealed one by one, the nearest first. The sight line ignores hidden traps AND spotted armed mines / wires (a revealed near mine's trigger shape hid the next one). Tests `Interactables.TrapSpot.PeekCone` (cone = the peek view's) / `.PeekSweep`; smoke `PeekMinesSmoke`.

## Corner lean pose: the body visibly leans out (2026-10-20, game designer: "at the low wall's left end he looks hidden behind the barricade")

- **Root cause (measured with the head bone, `LeanPoseSmoke`):** `UpdateCoverEntryBlend` cancelled the crouched fire stance's sideways root step on the mesh in EVERY low-cover case (rev 2026-10-07: over the top he fires where he stands) - also in the rev-5e low-cover corner peek, where that step is the lean itself. With the peek sight state active the head stood 48 cm (left end, pack `_L` clip) / 26 cm (right end, `_R`) BEHIND the wall's end while the peek eye point was 22 cm past it (head-eye 77 / 62 cm): he looked hidden but was seen. High cover never cancelled, but its pack `_R` head was only 14 cm past the edge (the `_L` one 25) and 30-43 cm off the eye point.
- **Fix:** (1) the cancel is eased out while `IsPeekingRoundCorner()` (`CoverLeanCancelAlpha` 1 -> 0 over `CoverLeanBlendSeconds` 0.25 s, back to 1 afterwards; over the top at a non-corner spot nothing changes); (2) `CornerLeanExtraPackLCm` 0 / `CornerLeanExtraPackRCm` 11 - an eased, tracked part of the mesh offset (`CornerLeanExtraAlpha`, inside `CoverEntryAppliedOffset`, restored when the peek ends; `EnforceMeshBaseOffset` untouched) so the `_R` head too is 25 cm past the edge. The stand-off was NOT shortened instead (first try, 28 -> 17 cm): hugging 17 cm from the wall's end let his eyes see round it (`PeekSightSmoke` hug check). (3) The peek eye point sits `FPeekSightConfig::CornerLeanDepthCm` 35 cm out in front of the wall (`PeekSightRules::CornerLeanEyePoint`), where the clip's head is.
- **Numbers (cm, after the fix, all four ends - low / high wall, left / right):** head 25 past the edge, leading shoulder 23-28, pelvis 12-21; head-to-peek-eye 8-14 cm (was 33-77). Pack `_L` caveat (his right corner): the barrel runs along the wall face, muzzle 13 cm short of the end but in front of the face; `_R` muzzle 63 cm past it. Screenshots `Saved/Screenshots/lean_left.png`, `lean_right.png` (+ `_front`, `lean_high_*`) by `CodexTactics.LeanPoseSmoke Shot` (rendered run, see the command header).
- **Tests:** `LeanPoseSmoke` (head >= 20 past the edge, shoulder past, rifle not inside the barricade, head within 30 cm of `GetPeekSight`, mesh back at its default place after leaving cover; every number logged as `LeanPose: ...`), unit test `CodexTactics.Combat.Sight.PeekLeanPoint` (eye depth).

## Group cover order (game designer 2026-10-20, every level)

> Reverses the 2026-10-11 decision "Followers in cover: NO automatic stacking" (DEMO01_BEAT_SHEET) **for GROUP orders only**. A
> single selection in Solo is unchanged: only that operative takes cover. Tutorial lessons that teach cover one operative at a time
> run in Solo (A1 hut stack, A3 "TAKE COVER ONE BY ONE"), so they are unaffected; role spots / confinements are judged per operative.

- **When:** the second click on the cover ghost (`ConfirmCoverPreview`) -> `ACodexTacticsPlayerController::OrderGroupCover` when
  `USquadSubsystem::GetGroupCoverMembers(leader)` is not empty (`SquadOrderRules::ShouldJoinGroupCover`): a **box selection of two or
  more** -> the other selected ones (Solo or not, a fight too, busy or not); else **group mode** (Solo off, no camera-zone hold, the
  exploration formation on) -> every formation follower not busy with an order of his own (`IsOnOwnOrder`), not a guard, not aboard /
  boarding, not held by a level obstacle. Nobody joins -> the single cover order as before. A click along the leader's own wall while he
  is in cover stays HIS shimmy (no preview, no group order).
- **Direct-order operative = the leader** (gold ring): the one the cover ghost is shown for, in both modes (in a box selection the leader
  is the one the selection keeps / its first member - `SetSelectedGroup`). He takes the clicked slot. On a high wall, if he would walk to
  the threat-side corner by himself right after entering (`CoverFacingRules::ShouldSnapToCorner` with `PredictCoverFacing`), that corner
  slot is his at once (so nobody is placed on that walk): "clicked a corner -> the corner is his".
- **Slots for the others (`Tactics/GroupCoverRules`):** probes along the SAME wall (`CoverTraceRules::FindShimmySlot` from his slot; the
  same `WallActor`, the same side = same normal / plane), `SlotSpacingCm` 100 apart (shoulder to shoulder: in cover the rifle points
  along the wall), `ProbesPerSide` = others + 2 per side. A probe is `NoWall` (the side's scan stops: nobody is sent past a gap) when
  nothing is found, another object, the navmesh pulled it > 35 cm off its place, or its outer end is < 30 cm away (the body would stick
  out); `Occupied` (skipped, the scan goes on) when a body outside the order stands within 60 cm or another operative's cover slot is
  there. The **nearest free slots** to his slot are taken (a tie: the side the members come from); then the **minimal total travel**
  assignment (`AssignMinimalTravel`, exhaustive up to 8, greedy beyond - a minimal sum of straight paths never crosses). Everybody on a
  group slot holds it exactly: `AOperativeCharacter::SetHoldGroupCoverSlot` stops `TrySnapToCoverCorner` (cleared by the next
  `OrderTakeCover` / `SetPendingCover`; kept over an open shot's and a knockdown's return to the same slot).
- **Prone overflow (cover too short):** the rest go **prone directly behind the cover**: `ComputeProneSpots` - a row `ProneSpacingCm` 75
  apart, centred on his slot and kept 15 cm inside the cover's ends (`ComputeCoverSpan`: the exposed corners, else >= 200 cm / the
  farthest wall probe), a cover narrower than the row -> centred on the cover; `ProneDepthCm` 170 off the wall face (behind the one at the
  wall, whose slot is 45 cm off it; a 60 cm barricade still hides a prone body there from an enemy beyond ~7 m by the Sprint 08 trace eye
  160 -> profile 25 cm); spots projected onto the navmesh (<= 40 cm) and not taken; spares outward for a refused slot order. Facing the
  cover (towards the threat side). `USquadSubsystem::OrderGroupCoverProne` (`SquadSubsystemGroupCover.cpp`): the run (`OrderMoveTo`) ->
  arrived (one more walk onto the spot when stopped 30-400 cm short) -> turn to face the cover (`SetIdleFacingYaw`, <= 25 deg or 1.2 s,
  standing still: he lies down in place, no dive) -> `DropProne` -> **holding**: held like a follower in cover (`ShouldLeaveCoverToFollow`:
  stays while the leader is in / going to cover or stands; the leader walks away in group mode -> he gets up to the leader's stance and
  follows). Ended by a new order to him (his `MoveOrderSerial` changed), Regroup, a stance change, being moved > 2.5 m, turn-based combat.
  Solo's "followers crouch" on a leader switch / Solo on skips him. Log `[GroupCover] ...`.
- **Running:** every member runs (`OrderTakeCover(..., true)` / the prone run), as any cover order (a double-click confirm runs too).
- **Tactical pause:** planned like the single cover order: each slot member `SetPendingCover` + `PlanMove`, each prone member a planned
  walk (`Planned` phase -> `Moving` on the release, dropped if re-planned elsewhere); waypoint markers. Feed "[PLAN] Group into cover ...".
- **Turn-based:** unchanged - only the active unit (the grid preview / walk); `OrderGroupCover` refuses there, a prone order is dropped
  when the grid starts.
- Feed: SQUAD "Group into cover (N operatives)!" / "Group into cover: N at the wall, M prone behind it!"; the reports `Cover.Enter`
  (+ `Cover.Corner`) are squad-wide. Smokes / log: `GetLastGroupCoverSlotCount` / `GetLastGroupCoverProneCount`.
- **Tests:** `CodexTactics.Tactics.GroupCover.LongWallEnoughSlots` / `.ShortCoverProneOverflow` / `.CornerClick` /
  `.MinimalTravelNoCrossing`, `CodexTactics.Squad.Orders.GroupCoverJoin`; smoke `GroupCoverSmoke` (own arena: 8 m wall in group mode
  -> all three in cover, same side, 100 cm apart, leader on the clicked slot, all ran, still there 2 s later; Solo + box of two -> both,
  the third stays; Solo single -> only he; a 1.5 m barricade in group mode -> leader in cover + two prone behind it, facing it, <= 45 cm
  off their spots, hidden from an eye 10 m beyond it, still there 2 s later, then the leader walks 8 m away -> they get up and follow;
  wave fight + tactical pause with a box of three -> only planned (3 planned moves), the release -> leader in cover, two prone).

## Followers in cover (old §5)

- **Followers in cover (rev 5g, same play-test):** `SquadOrderRules::ShouldLeaveCoverToFollow` (`FCoverFollowFacts`: in cover, not on his own order, not holding (Solo / camera-zone perimeter), not guarding, formation active = exploration, leader walking and NOT in / going to cover, slot > `CoverLeaveSlotDistanceCm` 400) -> `USquadSubsystem::UpdateFollower` calls `LeaveCoverToMove("follows the leader")` (+ stand up from a crouch for a standing leader; the stance clip roots him, the next repath walks him). Before, `FollowTo` refused anyone in cover and nothing took him out, so he stayed at the wall forever in group mode. `ExitSoloMode` (Regroup) takes in-cover followers out of cover too. Fights: the formation is off (`IsFormationActive`), the autonomy / ROE rules are unchanged. Tests `Squad.Orders.CoverFollowerLeavesForWalkingLeader`; `SoloRejoinSmoke` stage 108-116 (an 8 m wall: B in cover through a selection switch, stays with the leader standing / in Solo, Regroup takes him out, the leader walks 15 m away in group mode -> B leaves the cover and reaches the formation).

## Return to cover after a knockdown (old §5)

**Return to cover after a knockdown (user rule 2026-10-18, every level)** — «if enemies surround him, don't return him to cover; if a
grenade / blast just happened to explode next to him and he fell, return him to cover». `AOperativeCharacter::HandleKnockdownPhase`
remembers the cover slot (+ the cause) when a knockdown takes him out of cover; at the get-up `ReturnToCoverAfterKnockdown` asks
`KnockdownRules::DecideCoverReturn` (Off > NotInCover > OrderGiven (an order buffered while he lay wins) > TurnBased > NotABlast (pounce,
Brute blow, heavy hit) > EnemiesClose (a living enemy within `CoverReturnThreatRadiusCm` 400) > Return) and on Return orders the SAME
slot (`OrderTakeCover`; a corner slot takes up the corner hold again by the usual rules). Data: `FKnockdownConfig` / `Codex.Knockdown.CoverReturn`
(1), `Codex.Knockdown.CoverReturnThreatRadius` (400 cm) - settable in `ai_tuning.json`. Log `[Knockdown] <name> up again: <decision>`.
Tests `CodexTactics.Combat.Knockdown.CoverReturn`; smoke `KnockdownCoverSmoke` (blast -> back in 3.1 s at the same slot; pounce with two
hounds -> out; blast with a hound 2 m off -> out; blast + a move ordered while down -> the order).

