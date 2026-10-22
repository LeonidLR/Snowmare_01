# Ladders: procedural ladder actor + climbing (design, 2026-10-22)

> UE-only system (the Godot archive has no ladders). Source: the user's course extract `ladder-climbing-system.md`
> (NotebookLM notes of a Udemy ladder course, not in the repo), audited with the user's spec-auditor checklist on 2026-10-22
> (~30 % ready as written: a direct-control shooter design on the UE5 Manny rig). This document is that spec **rewritten for
> Snowmare**: click-to-move squad on the navmesh, independent tactical camera, `AOperativeCharacter` / `UOperativeAnimInstance`,
> turn-based grid. Nothing is implemented yet - section 9 is the phase plan.
>
> Related: [stances-and-movement](../systems/stances-and-movement.md) (vault = the closest precedent), [animation-setup](../systems/animation-setup.md),
> [camera-hud-minimap](../systems/camera-hud-minimap.md), [combat-and-fire](../systems/combat-and-fire.md) (turn-based arena),
> [save-load](../systems/save-load.md). Clip renders in UE: dev command `CodexTactics.LadderClipShots` (session ladder-clips-ue, 2026-10-22).

## 1. User decisions (2026-10-22)

| Question | Decision |
|---|---|
| Turn-based combat | Ladders are **forbidden** for now: no grid move uses a ladder (see 7.6 for a fight that starts mid-climb). |
| Who climbs | The operatives, and **human enemies: PMC and marksmen**. Hounds and mutants never. |
| Wall behind the ladder | Optional: a **free-standing ladder** (no wall) is allowed. |

## 2. What changed against the course spec (audit trail)

| Course spec | Snowmare | Why |
|---|---|---|
| `ProcessClimbingInput(Axis)`, W / S climbs | **No climb axis.** The ladder is a navmesh link; a click anywhere on the other level builds a path through it and the climb runs by itself (like the vault). | WASD pans the camera; operatives move only by orders through `AOperativeAIController`. |
| `bIsOnGround`, `bIsClimbing`, `bIsClimbOutFromTop`, ... (7 booleans) + `EClimbState` named after montages | **One enum `ELadderClimbState`** with a transition table (section 5). | The booleans had no setters / resets in the spec; `Air` entry was unreachable. |
| AnimNotify "end" + timer of `MontageLength` end the phases | **Montage delegates** (`OnMontageBlendingOut` / `OnMontageEnded` with `bInterrupted`) + **one interrupt path** `AbortClimb(Reason)`. | A knockdown / death / sniper layer on FullBody interrupts the montage: the notify never fires = deadlock; `SetTimer(0)` never fires. |
| `ObjectBehindLadder->SetActorEnableCollision(false)` | **Never touch the world.** The climber's own collision goes off for the top transitions (exactly like `OperativeCharacterVault.cpp`), with an end-fit sweep. | The wall actor is often the whole building / landscape: mates, enemies, bullets, sight, cover and the roof cutaway traces would go through it. |
| `bOrientRotationToMovement = true`, `MOVE_Walking` hard-coded on exit | **Snapshot on entry, restore on every exit** (movement mode, rotation flags, braking, gravity, stance, weapon, path following). | The operative's facing is owned by combat / cover code. |
| Mesh bounds `BoxExtent.Z` for the Z snap | **Capsule half height** (stance forced to Standing first). | Mesh bounds follow the pose. |
| `YawDelta < 10 -> +180`, else ladder yaw; `ClimbIntoTopMontage->BlendIn.BlendTime = 0` | Actor yaw on the ladder is **always wall-facing**; the IntoFromTop arrival must **face the drop** (pre-turn otherwise); angles via `FMath::FindDeltaAngleDegrees`; blend set **per play**. | The old rule faced away from the wall for any approach >= 10 deg; the edit mutated a shared asset. See 4.3. |
| `RungsOffset = 43`, any `Height` | **Pitch 36.4 cm** (the clips), Height **quantised** to the pitch. | The loop climbs exactly 2 rungs of 36.4 cm. |
| +2.8..3.0 cm additive root keys against drift | **Rung-index snapping**: the climber's Z is `Base + k * Pitch` after every step. | Drift cannot accumulate; any clip error stays inside one step. |
| `SideRungHeight = TruncToInt(...)` from a discarded `GetBoxExtent()` | Fixed tiling formula with guards (3.3). | The spec code did not compile and could divide by zero. |
| `LadderPreset`: Ignore all, Overlap Pawn | Rungs / rails **no collision, no nav effect**; a **click proxy** blocks Visibility only; triggers check the class. | The cursor (`HandleWorldHit`) traces Visibility: the ladder was unclickable. |
| ABP_Manny, CR_QueenSimple, Sequencer Backward Solve | `UOperativeAnimInstance` / `ABP_Operative` gates + **rung IK** fed by the actor (6). | Our rig is the UE4 mannequin; the Control Rig part was authoring, not runtime. |

## 3. The ladder actor `ALadderActor`

### 3.1 Frame and anchor points

Actor origin = **bottom centre of the rung plane, on the floor**. **+X (ActorForward) points away from the wall / towards
the climber**, +Z up. The climber hangs **facing -X** (yaw = ActorYaw + 180).

| Point (actor space, cm) | Value | Use |
|---|---|---|
| Rung k centre | `(0, 0, k * Pitch)`, k = 1..RungCount | IK targets, snapping |
| Hang line (capsule axis on the ladder) | `X = +HangStandoff` (48), `Y = HangLateral` (7 cm to the climber's left of the ladder centre - the clips hold the body off-centre; the actor-space sign is fixed when `LadderClipShots` re-measures it) | where the climber's capsule axis runs |
| Bottom link point | `(+HangStandoff + 40, HangLateral, 0)` projected on the navmesh | path start / end below |
| Top link point | `(-TopStandoff, HangLateral, TopZ)` with `TopStandoff` = 49 (IntoFromTop start) | path start / end above |
| `TopZ` (platform floor) | `RungCount * Pitch` (+-`TopTolerance` 8 cm, checked) | the top floor height |

`HangStandoff` 48 >= capsule radius 40 + rung radius 2 + 6 cm clearance: the standing capsule never touches the rungs.
All numbers are from the Fixed clips (section 4) and must be re-measured on the imported assets by `LadderClipShots`.

### 3.2 Parameters (editor; `ExposeOnSpawn` where a runtime spawn needs them)

| Parameter | Default | Notes |
|---|---|---|
| `LadderType` (`FDataTableRowHandle` -> `FLadderMeshData`) | `Metal` | rows Metal / Wood from the course meshes (`SM_LadderPole`, `SM_LadderRung`, `SM_LadderOutGrabBar`, `SM_RoundWallBrackets`; wood: `SM_W_Segment`, `SM_W_Rung`, `SM_W_RoundWallBrackets`, `SM_W_SquareWallBracket`) |
| `Height` | 255 | quantised: `RungCount = max(1, round(Height / Pitch))`, `TopZ = RungCount * Pitch` |
| `Pitch` | 36.4 | read-only unless the climb profile (`ULadderClimbProfile`, the clip set) says otherwise |
| `Width` | 60 | rails at +-30 |
| `bTopExit` | true | false = a ladder that ends against nothing (no top link; climb up and back) |
| `RailOvershootSegments` | 1 | extra rail segments above `TopZ` (the course's `SideRungOvershoot`) |
| `bTopGrabBars` | true | the out-bent grab bars above the top (`TopLadder` / `OutGrabBar`) |
| `WallBracketCount` | 3 | 0 = free-standing; auto 0 when no wall is found |
| `bAllowEnemies` | true | humans that may climb (section 7.5) may use this ladder |

### 3.3 Generation (OnConstruction, deterministic)

- Rungs, rail segments, brackets, grab bars = **one ISM each** (draw calls; the roof cutaway already handles ISM / HISM).
- Rail tiling without stretching: `SegmentLen = 2 * SideMesh.Bounds.BoxExtent.Z` (float, from the *loaded* mesh; the soft
  pointers are loaded synchronously in the editor, async + regenerate on a runtime spawn); guard `SegmentLen >= 1 cm`,
  `Pitch >= 10 cm`, else log and draw nothing; `SegmentCount = ceil(TopZ / SegmentLen) + RailOvershootSegments`.
  The mesh pivot is read from the bounds (`Bounds.Origin.Z - BoxExtent.Z` = bottom), never assumed.
- Brackets at evenly spaced rail heights between the first rung and `TopZ`, only with a wall.
- Collision: every visual ISM `NoCollision`, `SetCanEverAffectNavigation(false)` (like `VaultNavigation::MakeVaultable`).
- **Click proxy** `UBoxComponent` around the rungs: Visibility = Block, everything else Ignore; hover = hand cursor, click =
  "climb" order (7.1). No overlap triggers for the logic: positions are computed from the anchor points.
- **Nav link**: a `UNavLinkCustomComponent` (smart link) bottom point <-> top point, both directions, area
  `UNavArea_Ladder` (new; cost = climb time). No link when `bTopExit` is false.

### 3.4 Validation (editor only; `CallInEditor Validate` + on construction)

Traces: floor under the bottom link point (<= 20 cm); a top floor at `TopZ +- TopTolerance` under the top link point when
`bTopExit`; capsule fit (standing capsule sweep) at both link points and along the hang line; wall within 25 cm behind the
rung plane (optional: none -> free-standing, no brackets). `RungCount < MinRungsForClimbOut` (7, section 4) with
`bTopExit` = error (see open question 1). Errors: an editor-only text (`bHiddenInGame`) + the material parameter
`ErrorGlow` (1 = red emissive) on the rung material instance; the map check also lists them. Nothing of this runs in a
packaged game.

## 4. Clip contract (measured 2026-10-22)

Source: the course's `CorrectAnimations/*Fixed.FBX` (the `NotFixed` set is not used: 25 fps, other lengths). Skeleton: **UE5
Manny** (88 bones: spine_04/05, neck_02, ik_*, interaction, center_of_mass); 30 fps; root motion on `root`. Measured with a
headless Blender import (axis-converted, +-1 cm); to be re-measured in UE by `LadderClipShots`.

| Clip | Frames / s | Root motion (from start) | Start pose | End pose |
|---|---|---|---|---|
| `AS_UpLadderLoopFixed` | 55 / 1.8 s | **Z +72.7** (= 2 rungs), lateral sway up to 17.6 cm mid-cycle (back to 0) | loop pose: left ball 7.5 cm (on the floor / rung k), right ball on rung k+1, hands on rungs k+4 / k+5 (wrists 1-2 cm under them) | the same pose 2 rungs higher |
| `AS_LadderClimbOutTopFixed` | 99 / 3.27 s | **Z +261, forward 77.5** (towards the wall), lateral -12.7 | ~ loop pose (feet 1-3 cm, pelvis 5 cm off) | standing on the platform |
| `AS_LAdderIntoFromTopFixed` | 96 / 3.17 s | **Z -184.8, back 97.3** (from the platform to the hang line), lateral sway up to 25 cm | standing on the platform, **body facing the drop** (the 180 deg turn is inside the pose; the root never rotates) | ~ loop pose (hands 2.5-4.6 cm, feet ~6 cm off) |

Derived facts:
- **Pitch 36.4 cm**, first rung 36.4 cm above the floor; hand wrists ~46 cm and the balls ~50 cm in front of the root ->
  rung plane at **48 cm** (`HangStandoff`). The ladder centre runs ~7 cm to the climber's right of the root (`HangLateral`).
- **ClimbOut needs 7 rungs** below the platform (`7 * 36.4 = 254.8`; the clip rises 261 - the 6 cm rest is warped away):
  the shortest ladder with a top exit is ~2.55 m. **IntoFromTop** lands 5 rungs below the platform.
- **No down loop** (the up loop reversed) and **no bottom-entry clip** (the loop's start pose IS the "foot on the floor"
  pose: the bottom entry is a blend into it, the bottom exit a blend out of it).
- The course's "fix" adds only ~1.1 cm per loop (72.7 vs 71.6): rung snapping makes it irrelevant.
- Operatives are UE4-mannequin bodies (Post_Apo, Female Soldier: head 159.6 vs 165.5 cm). Whether the clips play by bone
  name or need `RTG_UE5Manny_UE4Manny` (M4 pack) is answered by `LadderClipShots`; either way the hands / feet are
  pinned by rung IK (6.2), so the body difference stays a few cm.

### 4.1 Who moves the climber (one source per phase)

The montages play **in place** (`ERootMotionMode::IgnoreRootMotion` on the climber's anim instance during the climb). The
component moves the actor along the clip's **extracted root track** (`UAnimSequence` root transform sampled at the montage
time), **scaled per axis to the exact target**:
- loop: Z only (lateral sway dropped: the capsule stays on the hang line), scaled to `2 * Pitch / 72.7`; reversed for down;
  snapped to `Base + k * Pitch` at each loop end;
- ClimbOut / IntoFromTop: X / Z scaled so the end lands on the top link point / the hang line exactly (warp), lateral kept
  but scaled to the `HangLateral` offset.

This is how the vault already works (code-driven curve, own collision off), needs no MotionWarping plugin, makes
reversal free and lets another clip set with another pitch drive the same code. Play rate may scale with the pitch
(`Pitch / 36.4`) for a non-default ladder.

## 5. Climb component `ULadderClimbComponent`

On `AOperativeCharacter` (default subobject) and on climbing enemies (7.5). Pure logic in `LadderClimbRules` (no UObject:
transitions, yaw, anchor math, order resolution) - unit tested.

### 5.1 States and transitions

| State | Entered when | Mover | Collision | Leaves when |
|---|---|---|---|---|
| `None` | default | path following | normal | the path reaches a ladder link start (`OnLinkReached`) |
| `Queue` | the ladder is leased by another climber | path following paused, holds at the link start (40 cm spacing) | normal | lease free -> `AlignBottom` / `AlignTop` |
| `AlignBottom` | link start at the bottom, lease taken, stance Standing | code: lerp from a **cached** start transform to the hang line at floor Z (`Floor + CapsuleHalfHeight`), yaw to wall-facing, 0.35 s | own collision on, swept move | done -> `ClimbUp` |
| `AlignTop` | link start at the top | code: walk / turn to the top link point **facing the drop** (yaw = ActorYaw), 0.25-0.5 s | normal | done -> `EnterTop` |
| `EnterTop` | aligned at the top | actor yaw snaps to wall-facing, IntoFromTop montage with **BlendIn 0 for this play** (`FMontageBlendSettings`), root track warped to the hang line | **own collision off** | montage blend-out -> `ClimbDown` (k = RungCount - 5) |
| `ClimbUp` / `ClimbDown` | one loop step (2 rungs) | root track Z (reversed down), snap at the end | own collision on, Flying, braking 5000 | step end: target reached -> `ExitTop` / `ExitBottom`; else next step; new order -> 7.1 |
| `ExitTop` | k + 7 = RungCount (or the last steps of 1 rung are taken by a half loop) | ClimbOut montage, root track warped to the top link point | **own collision off**, end-fit sweep before | blend-out + fit -> `None` (Walking, restore) |
| `ExitBottom` | k = 0 reached going down | blend out of the loop pose, 0.2 s | on | -> `None` (Walking, restore) |
| `Interrupted` | `AbortClimb(Reason)` from any state | see 5.3 | | -> `None` |

Odd rung counts: the loop moves 2 rungs; a one-rung remainder is played as the first half of the loop (frames 1-27 = 1
rung) at the start of the climb, so ClimbOut always starts with the feet 7 rungs below the platform.

### 5.2 Entry refusals (a refused climb ends the path with a feed line, like the other refused orders)

Turn-based active; carrying (`SetCarrying`); mounted; knocked down / dead; in cover (leaves cover first, as a move order
does); prone / crouched -> **stands up first** (not a refusal); the ladder leased (-> `Queue`); a climbing-forbidden enemy
(nav filter: never gets here).

### 5.3 The interrupt path `AbortClimb(EReason)`

One function for: montage interrupted (`bInterrupted`), death, knockdown request, save load, turn-based start (7.6),
cinematic lock, possession change, `EndPlay`, the lease lost. It: stops the climb montage, clears every timer handle owned by
the component (member `FTimerHandle`s only), turns the own collision back on, restores the snapshot (movement mode, rotation
flags, braking, gravity, stance, weapon visibility, `PathFollowingComponent` resume or abort), releases the lease, and
places the actor at the **nearest valid end** (bottom link point if k <= RungCount / 2 else top link point; a capsule sweep
picks the other end if blocked) - except death, which drops the body (section 7.7). Deadlock rule: every state has a
timeout of `2 x` its expected length -> `AbortClimb(Timeout)` with a warning log.

### 5.4 Facing rules (normalised)

- On the ladder: actor yaw = `LadderYaw + 180` always (no combat facing, no `FaceAimAt`: their callers check `IsClimbing`).
- IntoFromTop arrival: `Delta = FMath::FindDeltaAngleDegrees(CharacterYaw, LadderYaw)`; `|Delta| <= 10` -> no pre-turn;
  else `AlignTop` turns him to `LadderYaw` (facing the drop) first. Then the actor yaw snaps to `LadderYaw + 180` at the
  montage start with BlendIn 0: the clip's first frame shows the body still facing the drop, so nothing pops; with a
  blend-in the walking pose (facing the wall) would spin through the clip pose.

## 6. Animation integration

### 6.1 Montages and the ABP

Montages on the existing **FullBody** slot (Claude-owned script `Scripts/Editor/setup_operative_ladder_animation.py`,
idempotent, only empty fields, `CODEX_RESET_LADDER_CLIPS=1` overwrites), clip fields on `UOperativeAnimInstance` class
defaults (`LadderUpLoop`, `LadderClimbOut`, `LadderIntoFromTop`). `UOperativeAnimInstance::bIsClimbing` (read-only, like
`bIsVaulting`) blocks / fades to 0: left-hand IK (`LeftHandIKAlpha`), aim offset (`AimOffsetAlpha`), foot IK
(`anim_foot_ik_enabled` / the ABP foot placement), the **sniper layer** (it takes FullBody when standing still - exactly the
pause between loop steps), interaction clips, hit reactions (upper body), grenade / reload clips, cold / warm / weather
idle variants. The hand weapon is hidden for the climb (the existing hidden-weapon path) and shown again on restore.

### 6.2 Rung IK (hands and feet on the rungs)

`ALadderActor::GetRungWorldLocation(k)` + the climber's rung index give the targets. Per limb a curve in the clips
(`LadderGrip_hand_l`, `_hand_r`, `_foot_l`, `_foot_r`: 1 = on its rung, 0 = moving; authored once by
`setup_operative_ladder_animation.py` from the measured plateaus) selects the alpha; the target rung per limb comes from
the loop phase (start pose: left foot k, right foot k+1, right hand k+4, left hand k+5; each limb +2 per loop). Two-Bone IK
nodes on hand / foot chains in `ABP_Operative` after the slots (the user adds the nodes, C++ exposes `LadderIK*`
locations + alphas, like the left-hand IK setup). The wrist target is 2 cm under the rung centre and 4 cm towards the
climber (palm on the bar); the ball target is the rung top.

## 7. Game integration

### 7.1 Orders
A click on the other level = a normal move order; the path crosses the link. A click on the ladder (proxy) = a move to the
link point at the other end. A new order mid-climb: the climber keeps climbing towards the end the new path leaves from
(up if the path's next point is above), then follows it. Stop / hold mid-ladder = finish to the end he was heading to.
Group orders: `ComputeGroupTargets` unchanged; the members queue at the link (`Queue`), one on the ladder at a time,
each in path order. Commander Mode autonomy never chooses a ladder for cover moves.

### 7.2 Combat while climbing
No firing, no aiming, no grenades; automatic fire postures suspended; he can be seen and shot (no cover). Tactical pause:
the climb is a move like any other (time dilation applies; planned orders run after it).

### 7.3 Camera
`ATacticalCameraPawn` follows the leader's location: during a climb the follow target's Z is **smoothed** (critically damped,
`LadderCameraZHalfLife` 0.35 s) so the view does not pump; the zoom is unchanged. Roof cutaway: a climber is evaluated at the
floor he is heading to only once `ExitTop` / `ExitBottom` ends (no flicker mid-ladder). Camera zones / interior exits: the
climber counts as outside both floors until he leaves the ladder. Minimap: his floor (`MinimapRules::SelectFloor`) stays the
start floor until the exit ends, then switches.

### 7.4 Navigation
`UNavArea_Ladder` on the link. Filters: the operatives' filters (`UNavFilter_NoCrawl`, `UNavFilter_TrapLiteral`) allow it;
the enemies' default `UNavFilter_NoVault` **excludes** it; a climbing enemy uses `UNavFilter_NoVaultLadder` (excludes vault,
allows ladder). The lease = per-ladder occupancy (one climber; operatives and enemies share it).

### 7.5 Enemies that climb
`AEnemyCharacter::bCanClimbLadders` (class default; true on `AMarksmanEnemyCharacter` and the PMC Blueprint(s), false
elsewhere - the archetype is not enough: `BP_Enemy_FrozenPMC_Fighter` and the Military Zombie are both Frostbitten
variants). It also needs ladder clips usable on that body's skeleton (retargeted copies); a class without them is treated
as `false` with a warning. Same component and states; `EnemyAIController` picks the climbing filter.

### 7.6 Turn-based
Forbidden (user decision): the grid never registers a ladder link; `TurnArenaRules` sees no ladder moves. A fight that
starts while someone is on a ladder: the climber finishes to the end he was heading to before the grid forms (entry waits,
at most the remaining clip time, <= ~4 s) - open question 3.

### 7.7 Damage, knockdown, death on the ladder
Damage applies; hit-reaction clips do not play. Knockdown: open question 2 (proposed: no knockdown on a ladder, the blow is
plain damage). Death: `AbortClimb(Death)` without the end placement - the body leaves the hang line 20 cm, falls (`MOVE_Falling`)
and plays the knockdown death fall (`UKnockdownComponent::PlayDeathFall`) on landing.

### 7.8 Save / load
A save during a climb stores the climber at the end he was heading to (standing on its link point, state `None`); the
load never restores a mid-ladder state. Ladders themselves are level actors (nothing in the save).

### 7.9 Tunables
Climb tunables (align times, `HangStandoff`, `HangLateral`, `TopStandoff`, queue spacing, camera Z half-life, timeouts) in
`DA_GameBalanceConfig` (Unreal is the master copy; added through `Scripts/generate_balance_config.py`). Ladder geometry and
mesh rows in the `DT_LadderTypes` DataTable; clip-derived numbers (pitch, root deltas, min rungs) in a
`ULadderClimbProfile` data asset written by the setup script from the imported clips, so a new clip set updates them.

## 8. Checks

| Check | What it proves |
|---|---|
| `CodexTactics.Ladder.Rules.*` (unit) | quantisation, rail tiling + guards, anchor points, yaw / pre-turn, rung snapping, loop-phase limb targets, order direction mid-climb, nearest-end choice |
| `CodexTactics.Ladder.Climb.*` (unit) | state table: every transition, every abort reason restores the snapshot, timeouts |
| `LadderGeneratorSmoke` | metal / wood, 3 heights, free-standing: ISM counts, bounds, no navmesh cut, link points on the navmesh, validation errors on a bad placement |
| `LadderClimbSmoke` (L_MovementTest, spawned platform 2.55 m + ladder) | click on the platform: path through the link, up / down / IntoFromTop / ClimbOut end on the link points (+-5 cm), 3 operatives queue, prone -> stands, carrying refused, death mid-climb drops the body, save mid-climb -> loads at the end, TB start mid-climb waits |
| `LadderAnimSmoke [Shot]` | slot weights, all gated layers at 0, hand / ball -> rung distance per frame on the operative body (< 4 cm with IK), rendered frames |
| `LadderEnemySmoke` | a marksman paths through a ladder, a hound / frostbitten paths around it |

## 9. Phase plan

**Scheduling rule (2026-10-22):** ladder work lives on branch `ladder/work` (worktree `CodexTactics-ladder`). Phase 1 may
start there; the later phases that touch shared code or assets - the `AOperativeCharacter` Ladder cluster, the nav link /
filters, the `UOperativeAnimInstance` gates, `ABP_Operative` and map edits - start only after the orchestrator session
schedules them with the user.

1. **Generator** - `ALadderActor`, `FLadderMeshData` + `DT_LadderTypes` (import script for the course meshes), `LadderRules`,
   validation, nav link + `UNavArea_Ladder` (no climbing yet: paths are built, the climb is a placeholder teleport).
   Checks: `Ladder.Rules.*`, `LadderGeneratorSmoke`.
2. **Climb FSM** - `ULadderClimbComponent`, lease, refusals, abort path, code-driven placeholder motion (constant speed, no
   clips), filters for operatives / enemies. Checks: `Ladder.Climb.*`, `LadderClimbSmoke` (no anim asserts).
3. **Animation** - clips imported (session ladder-clips-ue), montages + `ULadderClimbProfile` script, root-track playback
   and warping, ABP gates, rung IK exposure; the user adds the IK nodes. Checks: `LadderAnimSmoke [Shot]`.
4. **Integration** - camera Z smoothing, roof cutaway / zones / minimap, TB exclusion, save rule, damage / death, enemy
   climbers (retargeted clips per enemy skeleton). Checks: `LadderEnemySmoke`, the TB / save parts of `LadderClimbSmoke`.
5. **Content + polish** - ladders placed in the levels by the designer, `docs/systems/ladders.md` (the system doc replaces this
   design doc's status section), smokes registered in `Scripts/test_map.json`, bot run.

## 10. Open questions (each changes gameplay)

1. **Short ladders.** ClimbOut needs 7 rungs (~2.55 m). Ladders to lower ledges: forbid a top exit below 7 rungs (editor
   error), or get / make a short climb-out clip?
2. **Knockdown on a ladder.** Proposed: no knockdown while climbing (the blow is plain damage); alternative: he falls off.
3. **Fight starts mid-climb.** Proposed: turn-based entry waits for the climber to reach the end; alternative: the climb is
   reversed to the start end, or the entry is refused while anyone is on a ladder.
4. **Which enemy Blueprints climb.** Marksman and `BP_Enemy_FrozenPMC_Fighter` - any other PMC body? (the Military Zombie is
   treated as a mutant: no.)
