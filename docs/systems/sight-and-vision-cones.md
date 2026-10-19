# Sight, peek sight and vision cones

> `UTacticalSightSubsystem` decides who sees whom (stance eye height, cover, profile points); since 2026-10-19 the squad's sight runs while exploring too
> and a peeking operative sees from the lean point in a limited cone. Vision cones draw the same rules as a visibility polygon (occluded cones,
> light tone behind low cover) and are built in batches on task threads.
>
> Key files:
> - `Combat/SightRules`, `Combat/TacticalSightSubsystem`, `Combat/VisionConeRules`, `VisionConeVisibility`, `VisionConeComponent`
> - Smokes: `SightSmoke`, `PeekSightSmoke`, `VisionConeSmoke`; tests `CodexTactics.Combat.VisionCone.*`
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Symmetric sight and peek sight (old §5)

- **Squad sight is symmetric while exploring too (game designer 2026-10-19: "all prone behind a wall, the enemies don't see us, yet we still see them, plates and all").** Root cause: `UTacticalSightSubsystem` ran only in the wave fight (`IsActive`: WaveCombat, not turn-based) - in exploration (the whole tutorial) `IsVisibleToSquad` was always true, so every enemy stayed revealed / plated / targetable (and Aggressive would shoot) while the enemies already used the Sprint 08 traces (patrol perception). Now `IsSquadSightActive()` = `SightRules::IsSquadSightPhase` (the fight as before + Exploration while `Codex.Sight.Exploration` 1, default) runs the squad -> enemy part: an operative sees an enemy only along a clear `ECC_Visibility` line from HIS eye height for his stance (`SightRules::EyeHeight`: standing 160 / crouched 95 / prone 25 cm - the Sprint 08 values, the same number the enemies use for a prone target's profile) to the enemy's profile, pawns ignored (the trace of the enemies and of the occluded vision cones); within 2 m (touch) always seen. The enemy -> squad intel (`GetBelief`) stays fight-only (`bBeliefWasActive`; exploring the enemies perceive through `PerceptionRules`); a new fight seeds it afresh. **What the player sees for an enemy no operative sees (decision):** the actor is hidden (no mesh, no plate, no cone, no lure / scent marker - the HUD already skipped hidden enemies), it is not targetable (shot line, automatic targets, Aggressive opening fire, Ctrl + click in exploration refused "Can't see it from here." - in a fight a priority on it waits as before), the minimap marks it unseen (it now uses the same `IsVisibleToSquad` while exploring, no own 25 m rule); within 12 m it is HEARD: a faint silhouette follows the sound (Sprint 08); once lost, a frozen last-known silhouette stays where it was last seen / heard and - exploring only - fades after `SightRules::LastKnownFadeSeconds` 12 s together with the minimap's last-known icon (`static_assert` with `UMinimapSubsystem::EnemyFadeSeconds`); in a fight it stays (blind fire). Never seen = nothing shown. Tutorial stats `squad_prone`, `enemies_unseen`. Tests `CodexTactics.Combat.Sight.EyeHeightVsWall`; smokes `SightSmoke` (exploration part), TutorialSmoke stages 137-139.
- **Peek sight (game designer 2026-10-19: "when an operative peeks out from cover he sees NOTHING").** Root cause: since d380d83 the squad's sight traced only from the capsule's eyes (`EyePoint`: feet + stance height) - behind the wall - although in the corner aim / corner hold / manual corner peek his head and rifle lean OUT past the corner (the corner shot already fired from there, `GetCoverFireOrigin`), and over a low wall when he aims over it; so the enemy walking at the corner stayed hidden / untargetable. Now `UTacticalSightSubsystem::CanOperativeSee(Operative, TargetPoint)` = his own eyes (unchanged: hugging a wall he sees only what his position allows; prone with no peek nothing behind a wall) OR the **peek view**: `AOperativeCharacter::GetPeekSight(Eye, Direction)` while `GetPeekSightPose()` (pure `Combat/PeekSightRules::DecidePose`: in cover, not prone, corner aim active; at a peek corner (manual peek spot / high-cover corner hold spot) -> `CornerLean`, aiming over a LOW wall elsewhere -> `OverTop`, hugging -> `None`): CornerLean eye = `CoverRules::CornerLeanPoint` (the stance eyes moved along the wall past the edge by max(60, edge + 20) cm - the same point as the corner muzzle, `CornerFireOriginFor` now calls it), direction = `CoverFacingRules::CornerAimDirection` turned by the applied upper-body twist; OverTop eye = feet + max(stance eyes, wall top + 15) (cap 170) leaned 25 cm to the wall, looking across it (-WallNormal). The view is LIMITED: inside `FPeekSightConfig` (BP_Operative -> CodexTactics|Cover|Peek -> `PeekSightConfig`: `RangeCm` 1800, `HalfAngleDeg` 45 = a 90 deg cone, `OverTopClearanceCm` 15, `OverTopMaxEyeCm` 170, `OverTopLeanCm` 25) with a clear visibility trace from that eye. **Symmetric:** `ProfilePoint` of a peeking operative = his leaned-out head (`GetPeekExposedPoint`, the eye minus the stance's eye-profile gap), so the enemies' traces (fight intel and `FindVisibleOperative` patrol perception) look at the point he looks from; hugging, `IsHiddenInCoverFrom` keeps him unseen as before. His **drawn vision cone** while peeking is the peek cone from the lean point (`UVisionConeComponent::SetViewOverride` offset + yaw; `IsEnemySpottedInCone` too). The trap search of the peek (`UpdatePeekTrapSpotting`) looks from the same eye point and aim. CVar `Codex.Sight.Peek` 0 = the old rule (A/B). Tests `CodexTactics.Combat.Sight.PeekPose` / `.PeekLeanPoint` / `.PeekOverTopHeight` / `.PeekCone`; smoke `PeekSightSmoke`; `CornerEntrySmoke` now accepts a trace ending mid-shot from the corner stance and refills the clip per trace (from the corner he sees the threat behind the wall all the time and fires more - checked A/B with `Codex.Sight.Peek` 0: passes as before).

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
height differences (the model uses the observer's feet as the ground; slopes are traced as obstacles). Operatives draw 1
level: a man-sized enemy's profile (150 cm) from their stance eye height (`SightRules::EyeHeight`) at the cone range.
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
  `Move Rebuild Cm` 15, `Turn Rebuild Deg` 2, `Static Refresh Seconds` 2, `Max Update Distance Cm` 9000, `Light Tone Alpha
  Scale` 0.45, `Light Tone Whiten` 0.35. Console: `Codex.VisionCones.MaxRebuildsPerFrame` 6, `Codex.VisionCones.Parallel` 2.

