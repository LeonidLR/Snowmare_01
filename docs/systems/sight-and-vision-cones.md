# Sight, peek sight and vision cones

> `UTacticalSightSubsystem` decides who sees whom (stance eye height, cover, profile points); since 2026-10-19 the squad's sight runs while exploring too
> and a peeking operative sees from the lean point in a limited cone. Vision cones draw the same rules as a visibility polygon (occluded cones,
> light tone behind low cover) and are built in batches on task threads.
>
> Key files:
> - `Combat/SightRules`, `Combat/TacticalSightSubsystem`, `Combat/VisionConeRules`, `VisionConeVisibility`, `VisionConeComponent`
> - Smokes: `SightSmoke`, `PeekSightSmoke`, `VisionConeSmoke`, `TowerVisionSmoke`, `VisionHeightSmoke`; tests `CodexTactics.Combat.VisionCone.*`, `CodexTactics.Combat.Sight.*`
> - Sight from a height (2026-11-02): the range ladder per height level, the roof sniper's cone, one model for the squad and the armed humans - section "Sight from a height".
> - Sight by the conditions (2026-11-04): 120 m clear day, x weather x time of day for everybody, humans x2, the cone shows the sight range - section "Sight by the conditions" (supersedes the 60 m / weapon-range cone statements above).
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Symmetric sight and peek sight (old §5)

- **Squad sight is symmetric while exploring too (game designer 2026-10-19: "all prone behind a wall, the enemies don't see us, yet we still see them, plates and all").** Root cause: `UTacticalSightSubsystem` ran only in the wave fight (`IsActive`: WaveCombat, not turn-based) - in exploration (the whole tutorial) `IsVisibleToSquad` was always true, so every enemy stayed revealed / plated / targetable (and Aggressive would shoot) while the enemies already used the Sprint 08 traces (patrol perception). Now `IsSquadSightActive()` = `SightRules::IsSquadSightPhase` (the fight as before + Exploration while `Codex.Sight.Exploration` 1, default) runs the squad -> enemy part: an operative sees an enemy only along a clear `ECC_Visibility` line from HIS eye height for his stance (`SightRules::EyeHeight`: standing 160 / crouched 95 / prone 25 cm - the Sprint 08 values, the same number the enemies use for a prone target's profile) to the enemy's profile, pawns ignored (the trace of the enemies and of the occluded vision cones); within 2 m (touch) always seen. The enemy -> squad intel (`GetBelief`) stays fight-only (`bBeliefWasActive`; exploring the enemies perceive through `PerceptionRules`); a new fight seeds it afresh. **What the player sees for an enemy no operative sees (decision):** the actor is hidden (no mesh, no plate, no cone, no lure / scent marker - the HUD already skipped hidden enemies), it is not targetable (shot line, automatic targets, Aggressive opening fire, Ctrl + click in exploration refused "Can't see it from here." - in a fight a priority on it waits as before), the minimap marks it unseen (it now uses the same `IsVisibleToSquad` while exploring, no own 25 m rule); within 12 m it is HEARD: a faint silhouette follows the sound (Sprint 08); once lost, a frozen last-known silhouette stays where it was last seen / heard and - exploring only - fades after `SightRules::LastKnownFadeSeconds` 12 s together with the minimap's last-known icon (`static_assert` with `UMinimapSubsystem::EnemyFadeSeconds`); in a fight it stays (blind fire). Never seen = nothing shown. Tutorial stats `squad_prone`, `enemies_unseen`. Tests `CodexTactics.Combat.Sight.EyeHeightVsWall`; smokes `SightSmoke` (exploration part), TutorialSmoke stages 137-139.
- **Peek sight (game designer 2026-10-19: "when an operative peeks out from cover he sees NOTHING").** Root cause: since d380d83 the squad's sight traced only from the capsule's eyes (`EyePoint`: feet + stance height) - behind the wall - although in the corner aim / corner hold / manual corner peek his head and rifle lean OUT past the corner (the corner shot already fired from there, `GetCoverFireOrigin`), and over a low wall when he aims over it; so the enemy walking at the corner stayed hidden / untargetable. Now `UTacticalSightSubsystem::CanOperativeSee(Operative, TargetPoint)` = his own eyes (unchanged: hugging a wall he sees only what his position allows; prone with no peek nothing behind a wall) OR the **peek view**: `AOperativeCharacter::GetPeekSight(Eye, Direction)` while `GetPeekSightPose()` (pure `Combat/PeekSightRules::DecidePose`: in cover, not prone, corner aim active; at a peek corner (manual peek spot / high-cover corner hold spot) -> `CornerLean`, aiming over a LOW wall elsewhere -> `OverTop`, hugging -> `None`): CornerLean eye = `CoverRules::CornerLeanPoint` (the stance eyes moved along the wall past the edge by max(60, edge + 20) cm - the same point as the corner muzzle, `CornerFireOriginFor` now calls it), direction = `CoverFacingRules::CornerAimDirection` turned by the applied upper-body twist; OverTop eye = feet + max(stance eyes, wall top + 15) (cap 170) leaned 25 cm to the wall, looking across it (-WallNormal). The view is LIMITED: inside `FPeekSightConfig` (BP_Operative -> CodexTactics|Cover|Peek -> `PeekSightConfig`: `RangeCm` 1800, `HalfAngleDeg` 45 = a 90 deg cone, `OverTopClearanceCm` 15, `OverTopMaxEyeCm` 170, `OverTopLeanCm` 25) with a clear visibility trace from that eye. **Symmetric:** `ProfilePoint` of a peeking operative = his leaned-out head (`GetPeekExposedPoint`, the eye minus the stance's eye-profile gap), so the enemies' traces (fight intel and `FindVisibleOperative` patrol perception) look at the point he looks from; hugging, `IsHiddenInCoverFrom` keeps him unseen as before. His **drawn vision cone** while peeking is the peek cone from the lean point (`UVisionConeComponent::SetViewOverride` offset + yaw; `IsEnemySpottedInCone` too). The trap search of the peek (`UpdatePeekTrapSpotting`) looks from the same eye point and aim. CVar `Codex.Sight.Peek` 0 = the old rule (A/B). Tests `CodexTactics.Combat.Sight.PeekPose` / `.PeekLeanPoint` / `.PeekOverTopHeight` / `.PeekCone`; smoke `PeekSightSmoke`; `CornerEntrySmoke` now accepts a trace ending mid-shot from the corner stance and refills the clip per trace (from the corner he sees the threat behind the wall all the time and fires more - checked A/B with `Codex.Sight.Peek` 0: passes as before).

## Operative field of view and the squad's cones (2026-10-20)

- **Field of view (game designer, screenshot: "one operative crouched behind a low barricade, the other two PRONE crawling away in the opposite direction - yet the Frostbitten and the Frost Hound 20 m away are fully visible, plates and all").** Root cause: (a) `UTacticalSightSubsystem::CanOperativeSee` traced from his eyes with NO angle - every operative saw 360 degrees out to 60 m, so the squad (the union of its members) saw an enemy standing behind all three backs; not (b) (a crouched man with 95 cm eyes does see over a 0.8 m wall, correctly) and not (c) (the HUD already skips hidden enemies and the minimap uses `IsVisibleToSquad`; the faint plate in the screenshot is a heard / last-known silhouette). Fix: his own eyes now see only inside a planar field of view round his facing - `SightRules::FieldOfViewDeg` / `IsInFieldOfView` (pure, test `CodexTactics.Combat.Sight.FieldOfView`), **140 deg standing, 140 crouched, 110 prone** (data: console variables `Codex.Sight.FovStandDeg` / `FovCrouchDeg` / `FovProneDeg`, settable in `ai_tuning.json` "cvars"; `Codex.Sight.Fov 0` = the old 360 deg sight; `Codex.Sight.FovFight 0` = the rule only while exploring). The range stays `MaxSightCm` 60 m (+ the eye-height traces). **Unchanged all-round awareness:** the 2 m touch reveal (`SquadTouchRevealCm`) and hearing (a faint heard silhouette within 12 m, never targetable). **The peek cone is separate and overrides** (`CanOperativeSee`'s second line, `PeekSightConfig`, 90 deg from the leaned-out eye). The squad's knowledge = the union of every live operative's current view; an enemy nobody sees now is hidden (no plate / cone / marker, not targetable) with the faded last-known silhouette as before.
- **Facing (`AOperativeCharacter::GetSightFacing`):** out of cover the body's facing (`GetActorForwardVector`); in cover he looks ALONG the wall to the side he faces (`CoverFacingRules::AlongWallDirection(CoverSlot, CoverFacing)` - the clips' look-around pose; a hugging operative does not see through the wall, as before) and, in the fire-ready pose / corner aim, over the cover the way the stance aims (`CornerAimDirection`). Consequence in play: an enemy BEHIND a wall the squad hugs is seen only once the operative is fire-ready (a threat heard within 12 m puts him there) or peeks; in the open the squad has to look at what it wants to see (mates parked facing away see nothing behind them). Fights: the same rule (`FovFight` 1); the smokes Horde, CombatFlow, EnemyAI, Marksman, Sniper, FlankBreach, KnockdownCover, PatrolAmbush, HoundScent, Guard, Ambush, EnemyHunt, SoloRejoin, TurnBasedAim, Posture, SquadFire, QuickFire, StealthLure, Minimap, Tutorial stay green; set `Codex.Sight.FovFight 0` to keep the 360 deg sight in the wave fight if play-tests find hounds from behind too sudden.
- **Drawn cone = his sight.** `AOperativeCharacter::GetVisionConeHalfAngle` = half the stance's FOV (70 / 70 / 55 deg; `VisionConeHalfAngleDeg` 35 only without the FOV rule) round `GetSightFacing` (in cover the cone is turned along the wall / over the cover with a view override), and the peek cone while peeking; `IsEnemySpottedInCone` uses the same. The drawn range is unchanged (weapon reach x stance multiplier / `VisionConeRangeCm`), the sight itself reaches 60 m.
- **Shift+O = the squad's cones (user approval 2026-10-20), O stays the enemies'.** Command `View.SquadVisionCones` (F8 row "Shift+O", event `SquadVisionConesToggled(.On/.Off)`), `UVisionConeComponent::IsPlayerSquadConesOn` / `SetPlayerSquadCones` (console `Codex.VisionCones.Squad`, a session setting like O), pause-menu entry "SQUAD VISION CONES: ON / OFF" next to VISION CONES. Combination: an operative's cone is drawn when `Codex.Debug.VisionCones` is 1, or -1 (default) and (his `bShowVisionCone` OR the squad toggle); the debug CVar 0 hides everything (the toggle can add cones, never hide a BP-forced one - as for the enemies). Lock: the handler asks the lock of `View.VisionCones`, so the Demo 01 tutorial (A4 cones card unlocks it) opens both together; the command itself is reported as `View.SquadVisionCones`. Smoke `SightSmoke` stages 110-122 (the screenshot scene: enemies 20 m behind a crouched leader facing a 0.8 m wall and two prone mates facing away -> not seen, no plate, not targetable; he turns -> seen; a prone mate turns -> seen; 62 deg off: prone no / crouched yes; 1.8 m behind: touch reveal; the real keys O / Shift+O toggle only their own side; drawn cone 70 deg crouched / 55 prone).

## Vision cones and occluded vision cones (old §5)

**Vision cones (user request 2026-10-11)** — `Combat/VisionConeRules` (pure: sector fill / outline geometry, inside test,
`ResolveVisible`, enemy state + colours, colour lerp; tests `CodexTactics.Combat.VisionCone.*`) + `Combat/VisionConeComponent`
(`UProceduralMeshComponent`; driven by its owner's tick through `UpdateCone`; `ConeMaterial` slot, fallback engine
`/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent` tinted through its `Color` vector parameter (rgb + a = opacity);
console `Codex.Debug.VisionCones` -1 / 0 / 1). `AOperativeCharacter::VisionCone` (range = weapon range x stance
multiplier, +-35 deg = Godot 70 deg full, green / red when an enemy is in it or a combat / priority target exists - Godot
player.gd `_is_any_enemy_spotted`) and `AEnemyCharacter::VisionCone` (Godot had no enemy cones: range = perception sight
range for a standing operative, x1.25 while searching; half angle = `SightHalfAngleDeg`; colour idle blue / suspicious amber
by the suspicion meter / searching / alert red). Godot: Scenes/movements/player.gd `_setup_vision_cone`,
`_rebuild_vision_cone_mesh`, `_update_vision_cone`.

**Occluded vision cones (game designer 2026-10-19, Commandos style; UE-only)** — the cone is a visibility polygon cut by
what the owner physically cannot see. Pure `Combat/VisionConeVisibility` (tests `CodexTactics.Combat.VisionCone.Occlusion.*`):
a fan of rays every `RayStepDeg` 1.5 deg across the field of view; each ray reports its obstacles (front / back face distance,
top height above the observer's feet, face normals); the game's sight rule - a straight line from the eye to the target's
profile point, blocked by sight geometry (`SightRules::ClearsCover` along the ray = the `ECC_Visibility` trace of
`AEnemyCharacter::FindVisibleOperative` / `UTacticalSightSubsystem::HasClearSight`) - is solved exactly along the ray for
each drawn **level** (`FVisionConeSightSpec`: eye height + levels {profile height, range}); `ClassifyRay` gives the seen
stretches with a level count; neighbouring rays that disagree (another span structure / blocker / obstacle count, a face
normal turning > 20 deg = a corner, a jump > max(25 cm, 6 %)) are bisected up to `EdgeRefineDepth` 6 times (edge rays
~0.02 deg apart: crisp shadow edges); `BuildShape` joins rays of the same structure quad by quad (the polygon follows the
hit points) and splits the others at the middle angle; `LevelAtPoint` = what the mesh shows at a point (smokes / tests).
World side (`UVisionConeComponent`): per ray a horizontal trace at `GetHighTraceHeight` (max of the eye and the profiles:
anything that tall hides every level behind it) and one at `GetLowTraceHeight` (min of them: anything lower changes no
level), for each low obstacle `LineTraceComponent` probes of its top and back face (`MaxLowObstaclesPerRay` 3,
`ObstacleDepthProbeCm` 400); `ECC_Visibility`, simple collision, every pawn and stasis silhouette ignored (= the game's
trace). **Levels / tones (mapping to the sight rules):** enemies draw 2 levels - a standing operative (profile 150 cm, the
standing sight range) and a crouched one (90 cm, range x `CrouchingVisibility` 0.7): **full tone** = even a crouched operative
is seen, **light tone** (same material, own MID: colour x `LightToneWhiten` 0.35 towards white, opacity x
`LightToneAlphaScale` 0.45) = only a standing one (right behind a low obstacle - e.g. a 1.2 m wall 6 m from a 1.6 m eye hides
a crouched man up to ~11 m - or beyond the crouching range), **cut out** = nobody (behind anything taller than the sight
lines; a 1.55 m crate hides a standing man only close behind it, he is seen again further out - drawn exactly). Not drawn
(documented): prone (profile 25 cm, range x0.4: hides behind anything >= ~25-60 cm), the 3 m all-round proximity circle,
hearing / smell, the high-cover «head down» rule (`IsHiddenInCoverFrom`: >= 180 cm cover is above the eye anyway), terrain
height differences (the model uses the observer's feet as the ground; slopes are traced as obstacles). Operatives draw 2
levels too (2026-10-20, below): a standing (150 cm) and a crouched (90 cm) enemy's profile from their stance eye height (`SightRules::EyeHeight`, the peek eye while peeking) at the cone range.
**Throttle / perf:** rebuild at most `RefreshHz` 12 when the owner moved > `MoveRebuildCm` 15 / turned > `TurnRebuildDeg` 2,
a still owner every `StaticRefreshSeconds` 2; skipped while neither the owner nor the cone was rendered in 0.25 s (always
in headless runs) or the camera is > `MaxUpdateDistanceCm` 90 m away; at most `Codex.VisionCones.MaxRebuildsPerFrame` 6 per
frame (a cone 3 periods overdue goes anyway). The shape keeps its traced heading (absolute rotation) and follows the feet.
A frame's rebuilds are one batch traced on the task threads (`ParallelFor`, `FGCScopeGuard`) from
`FWorldDelegates::OnWorldPostActorTick` (where the engine launches its async traces) and uploaded at the next
`OnWorldPreActorTick` (one frame later; world clean-up waits too); `Codex.VisionCones.Parallel` 2 (default) / 1 (parallel,
waited the same frame) / 0 (inline). Headless numbers (VisionConeSmoke, 21 cones shown, 20 guards turning, ~2.6 rebuilds
per frame, 80 rays / 210 traces per rebuild, ~0.45 ms ray work per rebuild): game thread 1.5 ms / frame inline (mode 0),
0.8 ms (mode 1), **0.25 ms (mode 2: 0.04 set-up + 0.2 mesh upload + ~0.01 wait)**; ~1.15 ms / frame of ray work on the
task threads. `FVisionConeBuildStats` (`UVisionConeComponent::GetBuildStats`). `bClipToSight` off (per component) = the
old plain sector (`VisionConeRules::BuildFill` / `BuildOutline`).

## The squad's cones: two tones like the enemies' (2026-10-20)

- **Problem (game designer screenshot, Shift+O):** an operative crouched right behind a low barricade and two standing ones further back - all their cones ran straight over the barricade, no shadow. **Root cause:** the operative cone drew ONE level, a standing enemy's 150 cm profile. A sight line from his eye (160 standing / 95 crouched) to a 150 cm point clears every wall lower than both ends, so a 0.8-1.2 m barricade never cast a shadow - technically right (a standing enemy behind it IS seen), but it read as "the cone ignores obstacles" and hid the useful part (a crouched enemy behind that wall is NOT seen). High obstacles already cut (the high trace runs at max(eye, profiles)); the operative path used only the outer level.
- **Now:** `VisionConeVisibility::MakeOperativeSightSpec(EyeHeight, Range)` = exactly the squad's sight rule `UTacticalSightSubsystem::CanOperativeSee` (his stance eye or the peek eye, his FOV / facing via the cone's angle and view override, the `ECC_Visibility` trace, pawns ignored) at two enemy profiles, both to the cone range: **full tone** = he would see even a crouched enemy (`SightRules::ProfileHeight(Crouching)` 90 cm) there; **light tone** = only a standing one (150 cm); **cut** = nobody (high walls, buildings). The levels are nested (crouched seen => standing seen). The light tone is the existing light MID of the cone material (no asset change). Console `Codex.VisionCones.SquadLevels` 2 (default) / 1 = the old single 150 cm level.
- **What it shows (flat ground, honest):** a wall below 90 cm hides nothing from a crouched or standing operative (a 0.8 m barricade: the cone stays whole - he does see over it; only a prone man, eyes 25 cm, is blocked); a 1.2 m wall right in front of a crouched man (eyes 95 < 120) cuts everything behind it except a ~40 cm light strip right behind it (a standing enemy's head); the same wall from 5 m back (eyes 160) leaves a light strip ~3 m deep behind it (a crouched enemy hidden, a standing one seen), full again further out; a 3 m wall cuts for everybody. In cover the cone follows `GetSightFacing` (along the wall / over it), so a crouched man hugging a wall looks along it, not through it.
- **Cost (VisionConeSmoke, 3 operative cones with walls in view, async mode):** per rebuild 121 -> 131 rays, 376 -> 433 traces (the low trace now runs at 90 cm, so obstacles between 90 and 150 cm get their top / back probes), 0.58 -> 0.68 ms ray work on the task threads, game thread 0.265 -> 0.286 ms (the light section's upload). Throttle unchanged (12 Hz while moving, 2 s still): 3 operatives walking ~36 rebuilds/s = ~+0.06 ms/frame of task-thread work at 60 fps.
- **Verified:** unit `CodexTactics.Combat.VisionCone.Occlusion.OperativeLevels` (crouched behind 0.8 / 1.2 / 3 m walls, standing 5 m back: spot levels, the drawn level = the exact 3D line test on a grid, nested levels, no light ring on open ground); `VisionConeSmoke` stages 40-47 (Shift+O on; the leader crouched behind a 0.8 m wall, a mate crouched behind a 1.2 m wall, the third standing 5 m back, a 3 m wall beyond: regions behind each wall and a 75 cm grid of every cone - drawn level = `CanOperativeSee` at 90 / 150 cm, 0 mismatches; the cost of 1 vs 2 levels logged `[VisionCone] squad perf`; `Shot` -> `Saved/Screenshots/squad_cones_occluded.png`, the designer's scene: one crouched behind the barricade, two standing back).

## Elevated observers: the 2.5D cone (bug 09, 2026-11-01)

- **Problem (game designer, L_FormationLabCombat, `Saved/BugReport/2026-11-01_bug_09_tower_vision`):** an operative on a combat-lab tower
  (3 x 3 x 4 m) had his green cone drawn straight over a 3 m lab wall - "he sees everything behind it"; the designer drew the wall's shadow
  as a black triangle. **Root cause:** the cone model was flat - every height (eye, target profiles, obstacle tops) was measured from the
  observer's feet and the targets were assumed to stand at his feet's level. On a 4 m roof the targets "stood" 4 m up in the air: the
  sight-line traces ran at roof + 0.9 / 1.6 m, above every wall, so nothing was cut, and the fan was drawn floating at roof height. The
  game's real sight checks (`UTacticalSightSubsystem::CanOperativeSee`, `AEnemyCharacter::FindVisibleOperative`, the cone colour check)
  were already right: 3D `ECC_Visibility` traces from the eye to the target's profile point (no 2D shortcut found; `AmbushWatchRules`
  deliberately ignores walls and was left alone).
- **Now (pure rule, `VisionConeVisibility`):** a ray carries `GroundCm`, the ground its targets stand on relative to the observer's feet
  (0 = his level, the old model; negative = he looks down from a perch). A target's profile is `GroundCm + profile`; the high / low traces
  follow it (`GetHighTraceHeight(GroundCm)` / `GetLowTraceHeight(GroundCm)`: from 4 m the low trace runs 90 cm over the ground below).
  The perch is an obstacle from the eye to its edge with `bHidesInside` (the ground under the roof is out of sight) whose rim (a parapet,
  else the roof) hides the ground near its foot. Closed forms for docs / tests / smokes: `WallShadowLengthCm` (behind a wall h_w at d,
  eye h_e > h_w: h_w x d / (h_e - h_w) of ground, everything when h_e <= h_w), `PerchDeadZoneCm`, `ElevatedGroundCm` (elevated only when
  the ground lies >= `ElevatedMinDropCm` 100 cm below the feet: a crate / step stays flat). The mesh lies on each ray's ground.
- **Tracing (`UVisionConeComponent`):** per rebuild `ProbePerch` (2 traces): what he stands on, and ground >= 1 m below it right under him
  with that block ignored -> a perch. Per ray from a perch `SamplePerch` (4 traces): the edge (the perch traced back towards the feet just
  under its roof), the ground past it (the lower of two columns 40 / 120 cm out - a ladder at the face never reads as ground), the rim's
  top; the low trace then starts past the edge with the perch ignored. A ground-level observer (a floor, a landscape: nothing under it)
  costs 2 traces per rebuild more and is otherwise unchanged. The rebuild cadence / budget / async batches are as before; `GroundCm` is
  derived, not part of `SameSpec`, so it never forces rebuilds. Known limit: one ground level per ray (the ground just past the perch's
  edge) - a target on another roof further out is judged as if on that ground.
- **Example (TowerVisionSmoke):** eye 562 cm over the ground on T_SW, W_C_4 (300 cm) 15.6 - 16.6 m out: ground shadow 18.9 m; a
  standing / crouched enemy hidden for 9.5 / 13.3 m behind the wall; the roof edge's dead zone to 4.2 / 4.9 m from the roof's middle.
- **Verified:** unit `CodexTactics.Combat.VisionCone.Elevated.*` (ShadowRules, TowerOverWall, ConeMatchesSight: a whole cone from a tower
  and from the ground = the exact 3D line test on a grid, the mesh on the ground); `TowerVisionSmoke` on L_FormationLabCombat (the
  leader on T_SW facing W_C_4: every ray looks down 402 cm, the cone drawn on the ground, dead zone / before the wall / shadow / beyond
  = drawn and CanOperativeSee, 0 mismatches on a 1 m grid; a ground operative's cone flat; an enemy in the shadow unseen, one beyond
  seen); arg `Shot` -> `Saved/Screenshots/TowerVision/tower_vision_fixed.png`, `tower_vision_close.png`.

## Sight from a height: the roof sniper, the range ladder, one model (2026-11-02)

- **Report (game designer, `/Game/Maps/Generated/L_Gen_Depot_1`, the Engineer PRONE at a roof edge as a sniper, cones on):** (a) "the cone does not
  cut the wall shadows - it still covers areas behind walls he cannot see"; (b) "the sight RANGE grows with the height level"; (c) "one vision model
  for the operatives and the armed human enemies". The cone is **2.5D, not volumetric**: a planar fan of rays from his eye, in each ray's vertical
  plane the exact eye -> target-profile line test against the measured obstacles, drawn as a flat mesh on each ray's target ground (one ground
  level per ray). The gameplay sight (`CanOperativeSee`, `FindVisibleOperative`) is a 3D trace and was right; only the cone was wrong.
- **Root cause of (a) - the 2026-11-01 elevated sampler did not recognise the generated roofs as perches** (`UVisionConeComponent`, A/B console
  `Codex.VisionCones.ElevatedFix 0` = the old sampler; smoke stage "A/B" reproduces the report on the hall's edge: 0 / 75 rays looked down, 242 of
  759 grid points drawn although hidden, the 3 m office's shadow drawn seen):
  1. **the perch's supports** - the generated halls' 9 m walls end FLUSH with the roof slab (walls 0 .. 900, slab 880 .. 900), a ruin's upper interior
     walls end under its roof. A man lying at the roof's edge lies over the wall: `ProbePerch` traced down past the roof, met the wall's top 0 cm lower
     -> "no drop" -> the FLAT model at the roof's level (targets assumed up there, every sight line above every wall: no shadow anywhere, the cone
     drawn at the roof). Now the column skips blocks less than `ElevatedMinDropCm` below the roof (up to 4: wall tops, a second slab) until a real
     drop, and the low trace ignores them too;
  2. **a roof of several blocks** - a generated roof with a stairwell / roof hole is 2-4 slabs; the perch was ONE component, so a ray crossing onto
     the next slab found "ground" at his own level past the first slab's edge -> that ray fell back to the flat model. Now `SamplePerch` follows the
     edge across every block that carries the walking surface on at his level (the next slab, the wall top flush with the edge; a 5 cm column past
     each edge, up to 4 hops);
  3. **too few low obstacles** - from a roof every wall below the eye is a "low" obstacle (the high trace runs at his eye), competing with the
     crates / barrels in front of it for `MaxLowObstaclesPerRay` 3: a wall behind three crates was never measured, its shadow not cut. From a perch
     `MaxLowObstaclesPerRayElevated` 12 (component property);
  4. **roofs below / around him** - a roofed lower building (the office, 3 m) or the far side of a hole in his own roof hide the ground under them, but
     both trace heights miss a roof slab (the eye line runs above it, the profiles' line below it). From a perch a wall whose top carries on as a
     roof gets a "roof" obstacle (`bHidesInside`) to that roof's far edge, and roofs at his own level further out along the ray are measured too.
  Cost (VisionHeightSmoke, prone roof sniper, 40 m cone): ~8.4 traces / ray, ~1 ms of task-thread work per rebuild (the rebuild cadence unchanged:
  12 Hz moving, 2 s still); ground-level cones are unchanged (2 traces per rebuild for the perch probe, as before).
- **(b) The range ladder (`SightRules::FHeightSightConfig`, NUMBERS PENDING the user's confirmation):** the observer's height level =
  floor(eye height above the ground below him / 3 m) - the ground below = the LOWEST surface a column straight down from his feet crosses
  (`SightRules::GroundBelowZ`: roof slab, wall top, the ruin's +1 floor, the street), not the absolute Z; in the -3 m cut the cut floor is his
  ground. Range x (1 + 0.25 x level) for level >= 1, at most x2; level <= 0 = x1 (never less than on the ground): **ground / cut x1, +1 (3 m)
  x1.25, +2 (6 m) x1.5, +3 (9 m) x1.75, +4 and up x2**. The squad's 60 m (`MaxSightCm`) -> 105 m on a +3 roof; a Marksman's perception range
  (45 m x the `Codex.Perception.SightRangeScale` knob) -> x1.75. The eye height decides: standing on a 1.5 m wreck (eye 3.1 m) is already +1.
  Data: `Content/Data/AI/enemy_perception.json` `"height_sight"` (`level_step_m`, `bonus_per_level`, `max_multiplier`; `EnemyPerception::GetHeightSight`),
  console `Codex.Sight.HeightRange 0` = the old flat range (A/B). Runtime: `UTacticalSightSubsystem::GetEyeAboveGroundCm` (cached per observer
  1 s / 25 cm), `GetHeightLevel`, `GetSightRangeMultiplier`, `GetSightRangeCm` = `MaxSightCm` x it: `CanOperativeSee`, the fight's squad -> enemy
  and enemy -> squad sight, `UCombatPressureSubsystem::IsPointInSquadSight`. Walls, roof edges and dead zones apply at every height (3D traces).
- **(c) One vision model:** the operatives and the armed human / humanoid enemies (`AEnemyCharacter::UsesHumanSight`: the Marksman archetype or
  any class with `bCanClimbLadders` - the PMC fighters, the Military Zombie, the future mercenaries) share the eye / profile points, the 3D sight
  trace, the 2.5D occluded cone (perch, shadows) and the height bonus: `AEnemyCharacter::GetPatrolPerception` (patrol sight, its cone, the bot's
  stealth view) multiplies the sight range by `GetSightHeightMultiplier`, the fight's enemy -> squad sight uses `GetSightRangeCm`. Hounds and the
  other beasts keep their own senses (x1). Each side keeps its own field of view and base range (operatives 140 / 110 deg prone, 60 m; enemies
  their `enemy_perception.json` row). **Operative cone range** stays the weapon's reach x stance (what he can shoot; the shot's own Godot
  elevation bonus x1.25 applies to targets 1.5 m below) - his sight reaches farther than his cone; see the open question.
- **Tests:** `CodexTactics.Combat.Sight.HeightRange` (levels, the ladder, the cap, the ground below), `CodexTactics.AI.Perception.HeightSightData`
  (+ the shipped file's values in `SmellHoundOnly`), `CodexTactics.Combat.VisionCone.Elevated.RoofSniperOverWalls` (a prone sniper 30 cm from a
  9 m roof's edge over crates / walls / a fence: drawn = the exact 3D test; the budget of 3 and the lost perch shown to draw the shadow as seen).
  Smoke **`VisionHeightSmoke`** (`L_Gen_Depot_1`): the leader prone at `B_hall_n`'s west edge over its wall's top (+3: x1.75, 105 m), cone traced
  from the perch, the office's shadow cut, beyond it seen, drawn = `CanOperativeSee` on a 1 m grid (0 mismatches), the A/B old sampler reproduces
  the report, a target 62.5 m out seen from the roof and not with `HeightRange 0`, nor from the ground 62 m away with a clear line; in the cut
  level 0 (50 m seen, 70 m not); prone on the ruin's +2 roof over its interior wall (x1.5, 0 mismatches); a Marksman on the roof spots an
  operative 62.6 m out (ground 40 m, elevated 70.9 m), not one behind the office, not from the ground; a hound up there x1; the PMC class is human.
  Arg `Shot` -> `Saved/Screenshots/VisionHeight/roof_sniper_cone.png`, `roof_sniper_shadow.png` (`-RenderOffscreen`). Training Ground:
  `HeightSightRules` (combatSim `rules.ts`) mirrors the ladder and the sim's squad sight range uses it (its enemies stand on the street: x1).
- **Known limits:** one target ground per ray (the ground just past the perch's edge: a target on another, lower roof is judged as on the street -
  the cone hides it although he would see it; a target on a roof at his own level further out is hidden too). The peek cone kept its 18 m
  until #32 (2026-11-07, below): it now reaches his sight range, height bonus included.

## The peek sees as far as his eyes (#32, 2026-11-07)

- **Report (task board #32):** the cover peek saw only 18 m (`FPeekSightConfig::RangeCm` 1800) while the squad's sight is 120 m on a clear day and
  the rifles reach 60 m (the PMC engagement band 19-24 m): a man firing round a corner / over a wall could not see a target on his own band -
  corner fire barely worked, for the operatives and the PMC alike.
- **Now:** `PeekSightRules::EffectiveRangeCm(Config, SightRangeCm)` = `SightRangeCm` x `SightRangeFraction` (1) while `bRangeFromSight` (default on;
  BP_Operative -> Cover|Peek, data-tunable), the fixed `RangeCm` 1800 only without a squad sight subsystem or with it off.
  `UTacticalSightSubsystem::GetPeekSightRangeCm(Operative)` feeds it `GetSightRangeCm(Operative)` - the same clear-day range x height level x
  weather / time of day as his own eyes; `AOperativeCharacter::GetPeekSightRangeCm` wraps it. The peek's own design limits stay: the 90 deg cone
  (`HalfAngleDeg` 45), the leaned-out / over-the-top eye point, the trap search's own 12 m sweep (`FTrapPeekSpotConfig`).
- **Every reader uses it:** `CanOperativeSee`'s peek line (the squad's reveal and targeting, and through it the PMC teams' sight of each other:
  `UPmcSquadSubsystem::UpdateSight` calls `CanOperativeSee` for their fighters), the update's distance pre-filter (max of the eye / peek range),
  the drawn peek cone and the cone's spotted state (`OperativeCharacterOverlays`), the PMC commander's corner bonus (`FindCoverNear`: a free corner
  counts only when the threat lies within his peek range, `PeekSightRules::IsCornerInPeekReach`). The enemies' sight of a peeking operative is
  unchanged: they look at his leaned-out head (`ProfilePoint` -> `GetPeekExposedPoint`) with their own ranges.
- **Tests:** `CodexTactics.Combat.Sight.PeekRangeIsSightRange`; `PeekSightSmoke` stage 3: the enemy 20 m back behind the wall is now seen, one 40 m
  out is seen and engaged (a shot line from the corner, his combat pick), the drawn cone's range = `GetPeekSightRangeCm`.

## Sight by the conditions: 120 m on a clear day, weather and night cut it (2026-11-04)

- **Report (user, L_Gen_Depot_1, over the shoulder):** "the vision cone stops in the middle of an open yard although everything is visible".
  Wanted: at least 2x the range, a clear day = the maximum (the cone covers practically everything visible from that angle); NIGHT cuts it
  drastically whatever the weather; snowfall, blizzard and fog reduce it; the same for the enemies ("so they don't cheat"); the height bonus
  stays on top; the operative's cone shows his SIGHT range, not the weapon's.
- **Numbers (PENDING the user's confirmation; data `Content/Data/AI/enemy_perception.json` `"sight_conditions"`, `EnemyPerception::GetSightConditions`,
  pure `SightRules::FSightConditionsConfig`):** squad base `squad_base_range_m` **120 m** (was the flat 60 m `MaxSightCm`); the armed humans
  (`UsesHumanSight`: Marksman, PMC, ladder climbers) x `human_enemy_range_multiplier` **2** over their archetype `sight_range_m`; beasts keep their
  rows. Every observer (squad, humans, beasts) x **weather** (Clear 1.0, Overcast 0.85, Snowfall 0.6, Blizzard 0.5; blended through a weather
  transition by the weather amounts) x **time of day** by the sun's elevation (day 1.0 at >= +10 deg, twilight 0.7 at the horizon, night 0.35 at
  <= -10 deg, linear between - no pops). Light snow 0.75 / fog 0.4 are in the JSON but unused: the weather ladder has no such states yet.
  Examples on the ground: clear noon 120 m, blizzard noon 60 m, clear night 42 m, blizzard night 21 m; a Marksman (row 45 m) 90 / 45 / 31.5 /
  15.75 m; a frost hound (row 15 m) 15 / 7.5 / 5.25 / 2.6 m. The height ladder (x1.25 per 3 m level, cap x2) multiplies on top.
- **One range everywhere:** `UTacticalSightSubsystem::GetSightRangeCm` = (squad / armed human: `GetSquadBaseSightCm` x height; beast: 60 m) x
  `GetSightConditionsMultiplier` (the `UEnvironmentSubsystem` weather -> next weather by the blend and `GetSunElevation`, evaluated once per frame;
  1 without an initialised environment). Read by `CanOperativeSee`, the fight's squad -> enemy and enemy -> squad sight, the PMC teams' sight
  (`CanOperativeSee` of their fighters) and `UCombatPressureSubsystem::IsPointInSquadSight`. The enemies' own rows go through
  `AEnemyCharacter::GetSightRangeScale` = `GetEnemySightRangeScale` (human x2 x height x conditions; beast x conditions) in `GetPatrolPerception`
  (patrol sight, the enemy cone, the bot's stealth view). Console A/B: `Codex.Sight.Conditions 0` (no weather / time multiplier),
  `Codex.Sight.LongRange 0` (the old 60 m, humans x1).
- **The operative's cone = his sight range:** `AOperativeCharacter::bVisionConeShowsSightRange` (new, default on; BP_Operative has no saved value)
  -> `GetVisionConeRange` = `GetSightRangeCm`; off = the older weapon / `VisionConeRangeCm` rules (the weapon-range inner marker was not added -
  optional, ask). The peek cone kept its 18 m (until #32: his sight range now). The wall / roof shadow cutting is unchanged (the same occluded builder, longer rays).
  `UVisionConeComponent` now treats a range drift under 1 % as the same shape (a dusk / weather blend moves the range every frame: the cone follows
  at the normal cadence instead of an urgent rebuild per frame).
- **Cost (SightConditionsSmoke, the Depot start, the leader's cone, 20 forced rebuilds each):** 60 m: 320 rays / 1882 traces / 3.13 ms task-thread
  work per rebuild; 120 m: 320 rays / 1891 traces / 3.35 ms (x1.07) - the Depot already hits `MaxRaysPerCone` 320 at 60 m; the rebuild cadence
  (12 Hz moving, 2 s still, async batches) is unchanged.
- **Pressure:** at clear noon (the longest sight) the pressure still found a concealed ambush spot on the Depot (23 m from the squad).
- **Smokes:** `SightConditionsSmoke` (`L_Gen_Depot_1`): clear noon / blizzard noon / clear night / blizzard night via `ForceWeather` + `SetHour`
  (clock frozen): the multiplier 1 / 0.5 / 0.35 / 0.175, the leader's range and his drawn cone, `CanOperativeSee` of a man 110 m and 38 m out on
  the longest clear line, a Marksman at those spots spotting the squad exactly within 45 m x2 x it, a hound's patrol 15 m x it (no x2) and fight
  60 m x it, the Clear -> Blizzard transition blending (largest step 0.0002 per frame), the cone cost, the pressure's concealed spot; arg `Shot` ->
  `Saved/Screenshots/SightConditions/clear_day.png`, `night.png` (`-RenderOffscreen`). `VisionHeightSmoke` pins `LongRange 0` + `Conditions 0`
  (it checks the height ladder against the 60 m base), `TowerVisionSmoke` pins `Conditions 0` and both set their scene cone ranges with
  `bVisionConeShowsSightRange` off; `VisionConeSmoke` expects the sight range by default and checks the weapon / fixed ranges with it off.
  Tests `CodexTactics.Combat.Sight.ConditionsMultiplier` / `.ConditionsData`.

## The 120 m cones fixed, the weapon zone (bugs #38 / #39, 2026-11-07)

- **Report (user, L_Gen_Depot_1, `Saved/UserEvidence/cone_46/47/48.webp`):** since the 120 m sight the cones (a) drop the game well below
  60 FPS, (b) rebuild visibly frame by frame, (c) are stair-stepped with several translucent layers stacked (three squad cones), (d) from a
  ROOF shoot thin shards / needles across the scene; and (#39) "operatives with an M16 kill very distant enemies like a sniper".
- **Measured first (`ConePerfSmoke`, the Depot, 3 operatives turning 1 deg / frame, Shift+O, clear noon, rendered offscreen, uncapped):**
  before - ground: game thread 0.09 ms / frame, 1.4 ms task work per rebuild (199 rays / 690 traces); roof (crouched, the sight x1.75 =
  210 m): game thread 0.67 - 0.79 ms / frame of which 0.47 - 0.58 ms WAITING for the batch at the next tick, 14 ms task work per rebuild
  (320 rays - the cap - / 9 776 traces); roof sweep (6 spots x standing / crouched x 4 yaws): 1 312 slanted triangles, 4 040 triangles at
  the roof's level beyond 30 m, 3 768 slivers narrower than 60 cm beyond 15 m. After: see the table in the CHANGELOG row / below.
- **Shards (d), root cause:** from a perch `SamplePerch` returns 0 when it finds no edge / no ground below it along a ray (the hop limit over
  a multi-slab roof, the next slab at his level, a probe missing the block) - that ray fell back to the FLAT model and was drawn at the
  roof's level for the whole 120 - 210 m: the needles across the scene; its neighbours on the street 9 m below joined it with slanted quads
  and the outline band stood up between the two levels. Fix: such a ray is marked `FVisionConeRay::bNoGround` (no more traces spent on it)
  and `CastRays` drops it (all of them none drawn rather than the roof's level); a span is drawn on its own ray's ground, never above the
  feet (`DrawGround` = min(GroundCm, 0)); neighbours on grounds more than `GroundToleranceCm` apart are not joined (`IsJoinable`: split at
  the middle angle, each on its own level) and an outline edge across a ground jump is left out. Plus the **slivers**: a crack between two
  slabs / a wall top and a roof let single rays see a thin strip far out - `FindSlivers` drops a seen stretch whose run of rays is narrower
  than `SliverMinWidthCm` 60 at its distance (beyond `SliverFromCm` 15 m; repeated until none is left).
- **Stair steps (c):** the refinement's rays were first-come (`MaxRays` 320 spent on the first shadow edges, the rest of the fan stepped);
  now the rays left after the base fan are shared out among the gaps that need refining, in order (`CastRays`, `FCaster::GapLimit`). The
  base rays adapt to the range (`RaySpacingCm` 200: the arc between two at the widest range, clamped to [`MinRayStepDeg` 0.5, `RayStepDeg`
  1.5]: 120 m ~0.95 deg, 210 m 0.55 deg, a 18 m peek cone 1.5 deg) and sit on a **world-anchored grid** (`GridAngles`: a turn keeps the
  inner rays' bearings - the walls are sampled at the same points, the shape no longer "boils" when he turns).
- **One layer (c):** the cones of one `MergeGroup` (operatives: their team + 1; enemies 0 = alone) are cut as a whole in one batch: each
  draws only where it shows a higher level than every other member, or the same level and the lower `Priority` (its unique id)
  (`VisionConeVisibility::ClipToLayer`, sampled every `MergeSampleCm` 150 along each span, crossings bisected to ~1 cm) - every point is
  drawn once, at the best level any of them shows; the outline runs round the layer only (`FVisionConeShapeOptions::OutlineHidden`). A
  member that moved re-traces; the others re-cut their share from their last rays (no traces: `Reshapes` in the stats); a member hidden /
  unregistered makes the rest re-cut. `GetDrawnLevelAt` still answers the owner's OWN sight (the smokes' drawn = sight checks);
  `GetDrawnMergedLevelAt` answers what the mesh draws.
- **Cost / cadence (a, b):** a batch not done by the next actor tick keeps running on the task threads (`Codex.VisionCones.MaxMsInFlight`
  150; then waited for) and its meshes are swapped in whole when it is - no game-thread wait, no half-built mesh; new rebuilds of that world
  wait for it, the change checks compare against what it was launched with (no second trace of the same pose). A still owner is no longer
  re-traced every 2 s (`StaticRefreshSeconds` 0): only a move (> 15 cm) / turn (> 2 deg) / a sight change (stance, weather, the hour:
  the range drifting > 1 %) / a request rebuilds.
- **Weapon reach (#39, `Combat/WeaponRangeRules`, every real-time shooter):** `weapons_tuning.json` per weapon `attack_range_m` = the MAX
  reach (a target farther out is seen but never engaged: `EvaluateShotLine` / `EvaluateBlindLine` no longer stretch it by the stance /
  elevation bonus) and the new `effective_range_m` (`UWeaponDataAsset::EffectiveRangeCm`, 0 = the max): M16 28 / 60 m, sniper rifle
  90 / 120, pistol 15 / 25, shotgun 8 / 15, plasma carbine 10 / 16, cryo emitter 7 / 10.5, flamethrower 6 / 8.5 (melee, the grenade:
  none); the Marksman's rifle (`enemy_weapons.marksman_rifle`) 80 / 110 m. Hit chance = the weapon's distance table x `HitMultiplier`:
  x1 up to the effective range (the stance / elevation range bonus stretches the EFFECTIVE range, capped at the max), then
  1 - 0.9 t^2 down to x0.1 at the max reach (`range_falloff`: `floor_multiplier` 0.1, `exponent` 2), 0 beyond. M16: 20 m x1 (table
  0.38), 40 m x0.87 (0.23), 50 m x0.57 (0.15), 60 m x0.1 (0.026), 80 m no shot. Applied in `AOperativeCharacter::ShootAtTarget` and the
  quick fire (the squad AND the hostile PMC operatives: the same class), the Marksman's `Fire` (and no shot past 110 m). The turn-based
  grid keeps its own cell ranges.
- **The weapon zone on the cone:** the operative the player commands (the leader / the selected group) shows a line at his weapon's
  effective range (his stance's bonus in it) and a faint band from it to the max reach, cut from the ground he sees (`SetWeaponZone`,
  sections 3 / 4 of the cone's mesh and material: `WeaponBandAlpha` 0.1, `WeaponEdgeAlpha` 0.6, `WeaponEdgeWidthCm` 20).
- **Verified:** unit `CodexTactics.Combat.VisionCone.Layer.Rays` (adaptive / world-grid angles, the fair budget on a picket fence, the
  no-ground rays dropped), `.Layer.Shape` (two grounds: no slanted / upward triangle; a crack = one sliver; the weapon band / edge
  distances), `.Layer.Merge` (three overlapping cones, one light-toned beyond 20 m: on a 50 cm grid every covered point drawn once at the
  best level, <= 1 % seams), `CodexTactics.Combat.WeaponRange.Falloff`; smokes `ConePerfSmoke` (above; arg `Shot` ->
  `Saved/Screenshots/ConeFix/roof.png`, `ground.png`) and `WeaponReachSmoke` (an M16 operative and a frozen hound on the Depot's longest
  clear line: 80 m seen, 0 shots; 50 m fires at chance ~0.15; 20 m at ~0.38, more hits).
- **Console:** `Codex.VisionCones.MaxMsInFlight` (0 = the old wait at the next tick); the BP knobs on the VisionCone component:
  `Ray Spacing Cm`, `Min Ray Step Deg`, `Sliver Min Width Cm` (0 = off), `Sliver From Cm`, `Merge Sample Cm`, `Static Refresh Seconds`
  (0), the Weapon Zone alphas.

## Vision cones - how to switch them on (old §6)

- **Vision cones - how to switch them on (user request 2026-10-11).** Off by default (as in Godot's shipped config).
  (1) All operatives: open `/Game/Characters/Operatives/BP_Operative` -> Class Defaults -> Details, category
  **CodexTactics | Vision Cone** -> tick **Show Vision Cone** (also: `Vision Cone Range From Weapon` on = the cone shows the
  weapon's reach x stance; `Vision Cone Range Cm` 1400 without a weapon; `Vision Cone Half Angle Deg` 35). The squad is
  spawned from BP_Operative, so the class default is the switch for the squad. (2) All enemies of one type: open
  `/Game/Characters/Enemies/<Type>/BP_Enemy_<Type>` (Hound, Brute, Frostbitten, Cutter, Marksman, ...) -> Class Defaults ->
  **CodexTactics | Vision Cone** -> **Show Vision Cone** (`Show Vision Cone When Engaged`: keep the red cone while it hunts).
  (3) One enemy placed in a level: select it in the viewport / Outliner -> Details -> **CodexTactics | Vision Cone** ->
  **Show Vision Cone** (per instance). (4) Look: select the **VisionCone** component (Components panel of the BP) ->
  **Vision Cone** category: `Cone Material` (empty = engine translucent fallback), `Fill Alpha` 0.22, `Outline Alpha` 0.85,
  `Segments` 32, `Height Above Feet Cm` 8, `Outline Width Cm` 8. (5) Console in PIE / game (`~`): `Codex.Debug.VisionCones 1`
  every cone on, `0` every cone off, `-1` (default) each actor's own setting. Nothing was saved into the BPs by the port.
  (6) Occlusion (2026-10-19): the same component -> **Vision Cone | Occlusion**: `Clip To Sight` (off = the plain sector),
  `Ray Step Deg` 1.5, `Edge Refine Depth` 6, `Max Rays Per Cone` 320, `Max Low Obstacles Per Ray` 3, `Refresh Hz` 12,
  `Move Rebuild Cm` 15, `Turn Rebuild Deg` 2, `Static Refresh Seconds` 0 (was 2; bug #38), `Max Update Distance Cm` 9000, `Light Tone Alpha
  Scale` 0.45, `Light Tone Whiten` 0.35. Console: `Codex.VisionCones.MaxRebuildsPerFrame` 6, `Codex.VisionCones.Parallel` 2.

