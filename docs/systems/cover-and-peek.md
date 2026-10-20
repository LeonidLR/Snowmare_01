# Cover, peek and corner aim

> Tactical cover lives in the `Cover*` / `CornerAim` clusters of `AOperativeCharacter`. The corner peek leans the operative's head out past an edge:
> it reveals hidden traps inside a sight cone (growing reach), is the eye point of the peek sight (see [sight-and-vision-cones](sight-and-vision-cones.md))
> and after a knockdown the operative returns to the same slot unless enemies surround him.
>
> Key files:
> - `Private/Characters/OperativeCharacterCover.cpp`, `…CoverEntry.cpp`, `…CornerAim.cpp`; `Interactables/TrapSpotRules`; `Characters/SquadOrderRules`;
> - `Combat/KnockdownRules::DecideCoverReturn`; smokes `CoverSmoke`, `CornerHoldSmoke`, `PeekMinesSmoke`, `KnockdownCoverSmoke`
> - Group cover order (2026-10-20): `Tactics/GroupCoverRules`, `Characters/SquadSubsystemGroupCover.cpp`, `ACodexTacticsPlayerController::OrderGroupCover`; smoke `GroupCoverSmoke`
> - Wall-edge cover switch (2026-10-20): `Tactics/CoverSwitchRules`, `OperativeCharacterCoverEntry.cpp` (`OrderCoverSwitch`, `TryEnterCoverSwitchFromRun`), `UOperativeAnimInstance::CoverStandSwitchEnter`, controller `ConfirmCoverPreview(bDoubleClick)` / `UpdateCoverSwitchHover`; smoke `CoverSwitchSmoke`
> - Corner-to-corner switch + wall-following round a cover object (2026-10-20, second pass): `Tactics/CoverWrapRules` (footprint, perimeter path, `CoverWrapWorld::PlanWrap`), `CoverSwitchRules::ChooseSwitchCorner`, `OperativeCharacterCoverWrap.cpp` (`OrderCoverWrap`), controller `TryOrderCoverWrap` / `TryHandleCoverWrapClick` / `UpdateCoverWrapHover` / `GetCoverPreviewPath`; smokes `CoverSwitchSmoke` (stage 9), `CoverWrapSmoke`
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

## Wall-edge cover switch (game designer 2026-10-20, every level)

> "Standing at the left end of a wall in cover, I hover another wall across from me, DOUBLE-CLICK, and he runs over into that cover
> with `anim_M4_std_jog_fwd_L_to_cvr_std_idle_L` / `anim_M4_std_jog_fwd_R_to_cvr_std_idle_R`." Defaults below are the designer's brief;
> every number is a tunable (BP_Operative Class Defaults -> CodexTactics|Cover|Switch).

- **Input rules (exact):** a click on ANOTHER cover = the cover ghost as before; the second click on the ghost = the ordinary cover order
  (a run, unchanged). The second click counts as a **double-click** when it comes within the ground's double-click thresholds of the click
  that showed the ghost (`DoubleClickSeconds` 0.35 s, `DoubleClickPixels` 30 px). A double-click is the **switch** only when
  `CoverSwitchRules::Evaluate` says so: the operative is in cover, not side-stepping, **at a wall edge** (an exposed edge of his slot within
  `CoverSwitchEdgeReachCm` 100 = the corner reach; high or low cover), the click is on a **different** wall (his own wall stays the shimmy /
  corner peek), and the target slot is `CoverSwitchMinRangeCm` 120 .. `CoverSwitchMaxRangeCm` **800** cm away (2D). Anything else -
  a double-click away from an edge, out of range, too close, locked by the tutorial, turn-based - is the ordinary cover order exactly as
  before (a double-click on a wall always "ran into cover": the cover order is a run). A double-click on EMPTY ground stays the sprint move;
  on an object (barricade, barrel) it stays the walk-up / interaction (Alt + click takes cover at objects, as before).
- **Preview:** hovering another cover from a wall edge (real time, the leader, `UpdateCoverSwitchHover` in `PlayerTick`) shows the cover
  ghost at the slot he would dash to plus a dashed run line from his feet (drawn by the HUD, `ACodexTacticsHUD::DrawCoverSwitchPreview`;
  `GetCoverSwitchPreviewLine`); the click ghost of a switchable cover keeps the line. The probe traces only when the cursor moves (cached
  like the corner peek hover).
- **The order:** `AOperativeCharacter::OrderCoverSwitch` = the ordinary running `OrderTakeCover` (he leaves his cover at once: **out of
  cover on the dash**, enemy sight and fire apply normally) to `ResolveCoverSwitchSlot` (on a high wall the corner slot when he would walk to
  it right after entering - the group cover's corner rule, so no side-step cuts the clip short; a low cover keeps the clicked slot), marked
  as a pending switch (`HasPendingCoverSwitch`).
- **Arrival (`TryEnterCoverSwitchFromRun`, `OperativeCharacterCoverEntry.cpp`):** the pack's run-in clips have no root motion; their travel is
  in the root bone, so - as the ordinary run-in - the capsule enters the slot and the tracked mesh offset (`CoverEntryClipOffset` + glide)
  carries the body from where he runs. Measured (`Scripts/Editor/measure_cover_switch_clips.py`, read-only, writes
  `Saved/Logs/measure_cover_switch_clips.txt`): `std_jog_fwd_to_cvr_*` comes straight at the wall from 4.6-5.5 m out; `std_jog_fwd_L_to_cvr_*`
  runs along the wall line from the pack's L side = the operative's own RIGHT (+RightTangent) 4.2-5.2 m away, `_R_` from his left; the suffix
  `_L` / `_R` is the cover idle he ends in = the side he faces (CoverFacingRules::ClipIndex: Right -> _L). The clip's root track is sampled
  at runtime (30 Hz, cached, `UOperativeAnimInstance::GetCoverSwitchTrack`) and turned into world offsets from the slot. From
  `CoverSwitchClipLeadCm` 220 cm out, running (>= 200 cm/s) and heading for the slot, every frame `CoverSwitchRules::ChooseClipStart` picks
  the approach (front / from his right / from his left) and the start time whose clip body lies nearest to him **and moves the way he runs**
  (a clip from the other side never qualifies), leaving at least `CoverSwitchMinClipSeconds` 0.6 s of the clip; the clip takes over when its
  body is within `CoverSwitchGoodFitCm` 40, when the fit stops improving, or at the latest `CoverSwitchLastChanceCm` 120 cm out. A fit worse
  than `CoverSwitchMaxMismatchCm` 110 -> the ordinary run-in / enter clip instead. While the switch waits for its moment the ordinary run-in is
  off. The end side (_L / _R) follows the facing he ends with (the usual `PredictCoverFacing`: threat side, corner hold, else the nearest
  exposed edge). The montage starts mid-clip (`PlayCoverMontage(..., StartSeconds)`, eased blend `CoverSwitchBlendSeconds` 0.2 s), the old
  yaw is held on the mesh with the clip's weight at any turn, the glide (runner vs clip body) eases out over `CoverEnterDecelSeconds`; the rest
  (wall clearance, `EnforceMeshBaseOffset`, the hand-off to the cover loop) is the ordinary entry.
  In the designer's layout (`CoverSwitchSmoke`): from a wall end across the gap into the next wall's corner = `std_jog_fwd_L_to_cvr_std_idle_L`
  (he comes from his right, faces back over the gap; a diagonal run-in: the clip takes over 1.1-1.2 m out, its body 55-80 cm off the runner,
  eased by the glide), back again = `std_jog_fwd_R_to_cvr_std_idle_R` (1.8 m out, 38 cm); low cover = the crouched endings
  (`std_jog_fwd_R_to_cvr_crch_idle_R`). The six clips per stance are loaded from `/Game/M4_Cover_Pack` when
  `UOperativeAnimInstance::CoverStandSwitchEnter` / `CoverCrouchSwitchEnter` are empty (nothing saved, no .uasset touched).
- **Group:** with a box selection / in group mode the switch is the group cover order (section above) with the **leader** dashing in through
  the run-in clip; the others take their slots / prone spots at the target cover by the group rules (ordinary runs).
- **Tactical pause:** planned like any cover order (`SetPendingCover` + `SetPendingCoverSwitch` + `PlanMove`; "[PLAN] Dash to the other
  cover!"); the release runs the switch. Found on the way: the release of a planned walk to ANOTHER cover used to keep the old cover
  (`OrderMoveTo` kept `bInCover` for any walk to the pending slot: the cover pose slid across, no enter clip) - it leaves it now (same wall =
  unchanged).
- **Turn-based:** no switch (the double-click is the ordinary grid cover move at its AP cost; grid walks stop on their cell, so a run-in clip
  has nothing to blend from).
- **Tutorial:** command `Cover.Switch` (F1 Controls row "Double-click other cover - Double-click another cover from a wall edge: dash to it").
  Demo 01: locked from the hut lesson (A1, after its `Cover.*` unlock) and again at the A2 / A3 beat starts; the A3 cover field card
  (`A3_cover`, its key chips now include it) unlocks `Cover.*` = the switch. A locked switch is the ordinary cover order
  (`TutorialRules::ResolveLockedCommand`: Cover.Switch -> Cover.Enter; the controller checks the lock silently).
- **Debug / log:** `[Cover] <name>: double-click on another cover - <verdict> -> ...`, `cover switch run-in <clip> from <t> s`,
  `[CoverAnim] ... cover switch run-in`; `Codex.Debug.CoverSwitch 1` logs every run-in probe (distance, speed, heading, fit).
- **Tests:** `CodexTactics.Tactics.CoverSwitch.EdgeEligibility` / `.TargetRange` / `.ClipChoiceFromGeometry` / `.ClipStartFromTracks` /
  `.ClickRules`; smoke `CoverSwitchSmoke` (runtime arena: wall A 6 m, wall B 3 m and the low wall C 2.5 m beyond A's ends, 1.5 m further
  out; A's end -> B by double-click: hover ghost + dash line, exposed + sprinting on the dash, `..._L_to_cvr_std_idle_L` started mid-clip,
  on B's corner slot, facing back over the gap, run-in offset gone; B -> A `..._R_to_cvr_std_idle_R`; a slow confirm, the middle of A and
  a 3 m range = the ordinary order; A's other end -> C crouched `..._R_to_cvr_crch_idle_R`; group mode: the leader switches, the others
  join; pause: planned, released -> switch). `CodexTactics.CoverSwitchSmoke Shot` (rendered) writes `Saved/Screenshots/cover_switch_preview.png`,
  `_dash`, `_arrival`, `_settled`, `_low`.

### Corner to corner, the mirrored pose (game designer 2026-10-20, second pass: "the switch really works well")

> "From the corner I stand at he should dash to the NEAREST corner of the other block - diagonally if needed - and arrive in the MIRRORED
> pose: left shoulder to the old wall -> right shoulder to the new one, facing back along the new wall towards the open side, ready to peek."

- **Target corner (`AOperativeCharacter::ResolveCoverSwitchSlot(Target, &Facing)`):** the clicked object's footprint (`CoverWrapWorld::FootprintForSlot`:
  the hit component's local bounds as an upright rectangle; checked against the clicked slot) -> `CoverSwitchRules::ChooseSwitchCorner`: among the
  object's faces on the **protected side** - outward normal within 60 deg of **his own wall's normal** (in cover the threat is behind his wall, so this
  is "the same side as the cover he leaves") - and both their convex ends, the corner slot (45 cm off the face, the corner stand-off of the side short
  of the corner: `CornerStandOff`, standing _L 57 / _R 28, crouched 65 / 49) **nearest to him** (2D). Wherever on the object the click was - its side
  face towards him, its far end - the target is that corner. No face on that side: the clicked face's nearest end. The slot is traced there
  (`FindCoverSlotAt`, same object, normal within 25 deg). An object without a footprint (landscape / BSP / tilted): the old rule (the corner he would
  walk to on a high wall, else the clicked slot).
- **Mirrored pose:** the arrival side = the side of that corner along the new face (`FCoverSwitchCorner::Facing`) = facing back along the new wall
  towards the open side he came over. In the designer's layout (both faces on one side, the gap between them) that is exactly the mirror of his facing
  at the old corner (`CoverSwitchRules::MirrorFacing`; logged `(mirrored)`). It is kept through the arrival: `bCoverSwitchFacingPending` /
  `CoverSwitchArrivalFacing` (EnterCover of the new wall takes it instead of `PredictCoverFacing`; a planned switch takes
  `CoverSwitchRules::ArrivalFacing` = the nearest exposed edge of the resolved slot), and the run-in clip is chosen for it (pack `_L` ending = facing
  his right). The clips stay the jog run-ins (`std_jog_fwd[_L|_R]_to_cvr_*`; the sprint variants exist in the pack but the dash already blends from
  the sprint, the jog ones measured best): from A's end to B's near corner = `std_jog_fwd_L_to_cvr_std_idle_L`, diagonal (CoverSwitchSmoke stage 9:
  151 cm forward, 336 cm across, the clip takes over 1.1 m out, its body 69 cm off the runner).
- **Preview:** the hover ghost stands at that corner slot in the mirrored pose (`ACoverGhostActor::ShowForFacing`); the HUD draws the dash's
  **navmesh path** (`FindPathToLocationSynchronously`, `GetCoverPreviewPath`; diagonal when nothing is in the way) instead of the straight line.
- **Hovered = committed (user bug 2026-10-20: "the ghost is right, I click the point again - he ends on the OTHER side of that corner"):** the
  first click's ghost stood at the CLICKED slot (B's end face) and a slow second click was the ordinary cover order to that slot - round the
  corner. Now a switchable preview resolves once in `ShowCoverPreview` (`CoverPreviewResolvedSlot` / `CoverPreviewArrivalFacing`), the click
  ghost stands exactly where the hover ghost stood (same corner, same pose), any second click on that object confirms, and the confirm commits
  that slot: a double-click = the switch (`OrderCoverSwitchResolved`), a slow one = the ordinary run to the same slot, arriving in the same pose
  (`SetCoverArrivalFacing`); the group / pause paths take it too. The first click never starts an order. `CoverSwitchSmoke` stage 10 (through
  `HandleWorldHit`, real double-click timing): B's end face double, B's front face near the edge double, the same slow - the first click starts
  nothing, its ghost 0 cm off the hover ghost, the committed slot 0 cm off, he ends there facing as the ghost did.
- **Unchanged:** the double-click rule, range limits (1.2-8 m, measured to the clicked slot), edge reach, tutorial lock, group (the leader takes the
  corner slot; the others probe along that face - its end is a `NoWall` side, they fill the other side or go prone), pause (planned), turn-based (no
  switch). A click on ANOTHER face of his own object is never a switch (`EvaluateCoverSwitch`: same object = `SameWall`) - it is the wrap below.
- **Tests:** `CodexTactics.Tactics.CoverSwitch.CornerToCornerMirrored` (the designer's layout: the side-face click -> the front face's near corner,
  facing mirrored, the slot point, a diagonal dash; the far-end click -> the same corner; the mirror layout; no protected face -> the clicked face;
  `ArrivalFacing`); `CoverSwitchSmoke` stage 9 (hover over B's far end -> the ghost at B's near corner, mirrored; a double-click on B's side face ->
  B's front face at R 607, facing right after facing left at A, at the corner, a diagonal dash). Stills `cover_corner_switch_preview.png` / `_dash` /
  `_settled` (`CodexTactics.CoverSwitchSmoke Shot`).

## Wall-following round a cover object (game designer 2026-10-20: "move along the wall of any building / object without leaving it")

- **Input rules:** the leader in cover (real time / tactical pause; not shimmying):
  - a click on the **ground within `CoverWrapMaxSurfaceDistanceCm` 100 cm** outside the outline of the object he is in cover at, or on **another face of
    that object** (a wall hit whose normal differs from his wall's by > 25 deg) -> he goes there along the object's wall (`TryHandleCoverWrapClick` in
    `HandleWorldHit` after the cover click; `TryHandleCoverClick` for the wall hit). The same face further on = the plain shimmy (as a click on his wall).
  - less than `CoverWrapMinTravelCm` 60 cm along the outline, further than 1 m out, more than `CoverWrapMaxTravelCm` 30 m, no footprint, a blocked way
    -> the click keeps its ordinary meaning: **open ground = leave cover and walk**, another object = the cover order / the switch, Ctrl + click = fire
    (handled before), objects / bodies keep theirs. The corner peek keeps its precedence on his own wall's END (its last 75 cm, its end face within
    2.5 m depth). Tutorial: locked while `Cover.Preview` or `Cover.Shimmy` is (reported as `Cover.Shimmy`).
- **The path (`Tactics/CoverWrapRules`, pure):** the footprint polygon (counter-clockwise; in the world an upright box from the hit component's local
  bounds - `CoverWrapWorld::FootprintOf`, refused when tilted > 14 deg, instanced, > 80 m) offset by the slot distance (`GetCoverWrapConfig().OffsetCm`
  = max(45, capsule radius + 10) = 50 cm): his place projected onto his face, the click onto the nearest edge; along each face its offset line; at a
  **convex** corner a quarter arc round the vertex (2 intermediate points) - he holds the next face from the arc's start, so the body turns with the arc;
  at a **concave** corner the mitre point of the two offset lines. Both ways round are built, **the shorter one** is taken (`BuildShorterPath`; a far
  side two corners away goes the shorter way). A goal within `CoverWrapCornerSnapCm` 100 of a convex face end becomes that corner's stand point (the
  stand-off of the side facing it) and he ends facing that corner (ready to peek); else facing the way he walked (`SnapGoalToCorner`).
- **The world plan (`CoverWrapWorld::PlanWrap`):** every path point on the navmesh (within 45 cm - the navmesh itself keeps ~55-60 cm off walls), a
  knee-high sphere sweep (18 cm, bodies ignored) along every segment, a cover slot of the same object traced on every face walked along and at the
  goal (their `WorldLocation` moved onto the path line). Any failure = `Blocked` (the ordinary click).
- **The movement (`AOperativeCharacter::OrderCoverWrap`, cluster file `OperativeCharacterCoverWrap.cpp`):** in cover all the way (`bInCover`,
  `bShimmying`, `ShimmyDirection` = the path direction: round any corner his right stays his right), the shimmy pace (crouch speed) and clips. He
  **steers himself** along the polyline (`UpdateCoverWrap`: `AddMovementInput` towards the path point 30 cm ahead of his progress, slowing over the
  last 40 cm; no navmesh path - it would keep the navmesh's ~60 cm off the wall and round the corners wide: first try measured 40-81 cm). Passing a
  corner's arc start / mitre point the cover slot switches to the next face (`SetCoverWrapFace`; the actor yaw eases round with
  `UpdateCombatFacing`'s RInterp). **No turn transition clip:** the M4 Cover Pack has no corner / cover-to-cover turn (searched
  `/Game/M4_Cover_Pack/Animations`: only `cvr_*_idle_L_to_R` in-place side swaps), so the corner is the "short smooth reposition" - the side-step
  round the quarter arc with the body turning 90 deg. At the goal: `EnterCover(GoalSlot)` facing `GoalFacing`. Stuck (< 15 cm/s for 1 s - a body in
  the way), panicking / enraged / knocked down / turn-based starting -> `GiveUpCoverWrapHere` (in cover where he stands on the face he holds, else out
  of cover). Any other order, leaving the cover, a shimmy / cover order cancels it; `HandleMoveFinished` ignores path-following ends while he wraps.
  Prone he gets up first (as the shimmy). High and low cover (crouched all the way at a low one).
- **Preview:** hovering such a point shows the ghost at the goal in its end pose (`ShowForFacing`) and the **perimeter path** (the HUD's dashed
  polyline, `GetCoverPreviewPath`); only for a path round at least one corner (along his own face: the plain shimmy, no preview).
- **Group:** with a box selection / in group mode a wrap round a corner is the group cover order with the leader's wrap (`OrderGroupCover(...,
  LeaderWrap)`): he follows the perimeter; the others take the destination face by the group rules, **never on the side he comes along** (those probes
  count as `NoWall`: they would stand in his way - first try: the leader stalled behind the Medic-Sapper), else prone behind it.
- **Tactical pause:** planned (`SetPendingCover(GoalSlot)` + `SetPendingCoverWrap(Plan)` + `PlanMove`, "[PLAN] Along the wall, round the corner!");
  the release's `OrderMoveTo` to that goal starts the wrap (`StartPlannedCoverWrap`).
- **Turn-based:** no wrap (the clicks keep their grid meaning; a cover click on another face is the grid cover move whose AP cost is the grid path's
  length - the grid path round the object hugs it).
- **Tests:** `CodexTactics.Tactics.CoverWrap.Outline` / `.ConvexCornerPath` (43-46 cm off the outline all the way, the arc, the face switch at the arc
  start) / `.ShorterWayRound` (2 corners, both directions, the same face = no corner, all the way round never chosen) / `.ConcaveCornerMitre` (an L:
  the mitre 45 cm off both faces, mixed corners) / `.ClickRules` (wrap / TooFar / TooShort / TooLong / Disabled / NoOutline, the corner snap both ways);
  smoke `CoverWrapSmoke` (own arena: box H 3 x 3 x 3 m, low box L 2 x 2 x 1 m; 1 ground click by H's next face: hover ghost + path, round the corner in
  cover all the way 45-57 cm off the outline, turned 90 deg, at the goal, mesh on the capsule; 2 a click on H's far face: two corners the shorter way
  via the front, 44-50 cm off; 3 low box crouched all the way; 4 open ground 2 m out leaves the cover; 5 group mode: leader wraps, the others take the
  face / prone; 6 pause: planned, nobody moves, the release runs it). Stills `cover_wrap_preview.png` / `_corner` / `_settled` / `_low`
  (`CodexTactics.CoverWrapSmoke Shot`).
- **Limits / next:** the world outline is one upright box per component (a building made of several actors has no concave corners in practice; the
  pure rules handle concave polygons); the wrap stops at the object's outline (another object touching it is not walked onto).

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

