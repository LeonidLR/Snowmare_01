# Operative character: stances and movement

> `AOperativeCharacter` is ONE class in ONE header with its members defined in 16 .cpp files grouped by cluster (Movement, Stance, Vault, Overlays,
> Inventory, Progression, Health, Weapons, Combat, Aim, TargetedShots, Cover, CoverEntry, CornerAim, Sniper). New code goes into the file of its cluster;
> a helper two files need goes into `OperativeCharacterInternal.h` (never two anonymous namespaces - unity builds).
>
> Key files:
> - `Source/CodexTactics/Private/Characters/OperativeCharacter*.cpp`, `Public/Characters/OperativeCharacter.h`, `OperativeCharacterInternal.h`
> - Docs: `docs/port/REFACTOR_OPERATIVE_2026-10.md` (§3 cluster map); tests `CodexTactics.Characters.OperativeUnits.*`
> - Related: [cover-and-peek](cover-and-peek.md), [combat-and-fire](combat-and-fire.md), [animation-setup](animation-setup.md)
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Squad / characters (old §5): AOperativeCharacter and its source layout

**Squad / characters**
- `Characters/OperativeCharacter` — orders, stances (capsule per stance, placeholder body until a skeletal mesh),
  sprint rules, shooting (Gemini) with cold misfire / aim penalty, inventory (matches 3, grenades 2/4, turrets /
  barricades / mines with limits 2/4/5), role (Commander / Engineer / MedicSapper) + luck (25/30/35),
  carrying (`SetCarrying`), lift limits (80 % cold, 50 % HP), placement radius 15 m. Godot `player.gd`.
  **Source layout (cleanup day 2026-10-14, `docs/port/REFACTOR_OPERATIVE_2026-10.md`):** one header, members in
  `Private/Characters/OperativeCharacter.cpp` (ctor, BeginPlay / EndPlay, Tick, identity) + `OperativeCharacter<Cluster>.cpp`:
  `Movement` (orders, formation moves, speed, traffic hold), `Stance` (stances, prone dive, capsule, placeholder), `Vault`,
  `Overlays` (selection ring, silhouette, vision cone), `Inventory` (items, deployables, field aid), `Progression`,
  `Health` (hits, knockdown hooks, death, corpse), `Weapons` (arsenal, ammo, reload, AI grenades), `Combat` (target
  selection, lines of fire, posture, `ShootAtTarget`), `Aim` (muzzle, aim point / cone, combat facing), `TargetedShots`,
  `Cover` (orders, enter / leave, shimmy, facing, events), `CoverEntry` (run-in, mesh blend, wall clearance),
  `CornerAim` (corner hold / aim / peek / snap, open shot), `Sniper`; shared helpers `OperativeCharacterInternal.h`.
  Net: `CodexTactics.Characters.OperativeUnits.*` + the per-file rules `operative-*-units` in `Scripts/test_map.json`.
- **Who a stance order goes to / whose stance it is (2026-11-08):** V / C / Z go to the selected operative - a box selection's to every
  selected one (`ACodexTacticsPlayerController::ApplyStance`); Alt + V / C / Z to the whole squad (`SetEntireSquadStance`). Every player
  stance order calls `USquadSubsystem::NotePlayerStanceOrder` -> `AOperativeCharacter::ClearStanceFromSquad`: the stance is no longer the
  squad's (`IsStanceFromSquad`) and is the player's (`IsStanceFromPlayer`) until anything changes it; Solo's guard crouch on a leader
  switch never overwrites it (bug 2026-11-08, [squad-and-orders](squad-and-orders.md) "Solo operatives keep the stance the player gave
  them", `Codex.Squad.SoloOwnStance`).

### Moved barricades are vaulted both ways (game designer play-test 2026-10-22)

"I moved the barricade and set it in one place; the operative vaulted over it, but cannot vault back and walks around." Cause:
`URelocationSubsystem::SetObjectCarried(false)` made the barricade's box affect the navmesh again when it was put down, although
`VaultNavigation::MakeVaultable` keeps every shape of a vaultable object off the navmesh (only its `UNavArea_Vault` modifier marks it): the moved
barricade cut a hole into the navmesh, paths across it vanished. Fix: a vault-made object (`VaultNavigation::FindVaultNavModifier`) keeps its box
off the navmesh; its vault area is switched back on and refreshed at the new spot. Check: `BarricadeVaultBackSmoke` (two 6 m walls with a 3.2 m
gap; a barricade pushed into it by a mate with the real push task, then a freshly placed one: box off the navmesh, the path straight through both
ways ~7 m, the leader vaults over and back).

### Falls and drops (game designer items 6 / 10 / 11a, 2026-11-03, branch `fix/falls-and-drops`)

The play-test on the generated map `L_Gen_Depot_1`: (6) on a raised bridge / roof an operative never stepped past the edge (the navmesh kept every
path on it) - walking off should be a fall with fall damage, and a heavy knockdown at an edge should throw him over it (enemies too); (10) running to a
roof edge he should carry on forward and drop onto a lower roof across a small gap - too far or too high = damage - with the standard fall animation
and a drop marker on the order's path; (11a) an order into the -3 m cut next to a building ran round it and stopped - a drop into the cut must be a way in.

**What was missing.** No navmesh link crossed an edge (only the level generator's hole links through the ruin's roof / floor holes), the
character movement never left the navmesh's ground, the knockdown played in place (no throw), and only a ladder fall knew fall damage.

**How it works now** (UE-only, no Godot reference; pure rules `Falls/FallRules` - tests `CodexTactics.Falls.Rules.*`; tunables
`Content/Data/Movement/falls.json`, mirrored by `FFallConfig`, test `TuningFileMirrorsDefaults`):
- **Drop links, generated at level start** (`Falls/DropLinkSubsystem`: `UDropLinkSubsystem` + the transient `ADropLinkActor` holding the links): once
  the infantry navmesh is built, every EXTERIOR navmesh edge (`ARecastNavMesh::GetDebugGeometryForTile` -> `NavMeshEdges`) is sampled every
  `link.spacing_cm` (150); a sample whose outward side has no parapet / wall / curb (two sphere sweeps at 45 and 110 cm), a profile of down-probes
  (`FallRules::AnalyseProfile`: the edge, a gap of at most `link.max_gap_cm` 250, a walkable lower surface `link.min_drop_cm`..`link.max_drop_cm`
  150..650 below - a HIGHER surface still within the gap wins over the gap's ground, `IsHigherLanding`), a landing a run-up at
  `link.reference_speed_cms` (600) reaches (`PlanTakeoff`), on the lower navmesh, and a clear flight arc (a capsule swept along the parabola) gets a
  ONE-WAY smart link (`UNavLinkCustomComponent`, agent 0 only) in `UNavArea_Drop` (free drops) or `UNavArea_DropHurt` (drops that hurt). Costs
  (PENDING the designer): DefaultCost 3 like a ladder plus an entering cost of 10 m (`Drop`) / 25 m (`DropHurt`) - a stair / ramp / ladder of a
  similar length is preferred; overridable in `DefaultEngine.ini` `[/Script/CodexTactics.NavArea_Drop]` / `NavArea_DropHurt`. The enemies'
  filters (`UNavFilter_NoVault` and its subclasses) exclude both areas. **Which maps**: `Codex.Fall.DropLinks` 0 none / 1 (default) the level
  JSON's `"drop_links": true` / 2 every map; the level generator's builder writes the flag (`L_Gen_Depot_1.json` has it); the hand-made maps stay
  without until the designer decides (their tutorials / smokes were tuned without drops). Console `CodexTactics.DropLinks [clear]` regenerates.
  On `L_Gen_Depot_1`: ~600 links (163 hurt) from 3354 edge segments in ~85 ms (log `[DropLinks]` with the rejection counts).
- **The drop** (`Falls/LedgeFallComponent`: `ULedgeFallComponent` on every operative and enemy, default subobject `LedgeFall`): the link's callback
  (or the component's own 0.1 s poll, like the ladder) hands him over; next tick he stops his path (the order's destination is kept), stands up
  first if crouched / prone, takes the landing spot (one at a time: `TryLease` - a landing within `lease.radius_cm` 220 of another man's running drop
  waits, state `Waiting`), and RUNS OFF the edge aiming at the link's landing: the run-up keeps his approach speed when it lands him inside the lower
  surface, is raised to what the gap needs (x 1.1, at least 150 cm/s) and checked so he does not sail past a narrow lower roof, never faster than his
  sprint (`AOperativeCharacter::GetSprintSpeedCap`: wounds, cold, a load; `TakeoffSpeedScale` for the smokes). In the air he KEEPS that horizontal
  speed (no input, no air control), lands, the landing takes 70 % of the speed, the fall damage is applied (`UHealthComponent::ApplyDirectHealthLoss`,
  source "Fall", the floating text "FALL -28"), after `land.settle_s` (0.35) the path goes on to the order's destination (`ResumeMoveAfterLadder`).
  Turn-based combat refuses a drop ("NO DROPS IN TURN-BASED COMBAT"); a run-up that never leaves its edge switches that link off (`DisableLink`).
  While he drops: no fire, cover, facing or stance change (`IsDroppingOffLedge`); the formation waits for him (`FollowTo` refused); a new order is
  walked to after the landing; a save puts him at the landing.
- **Fall damage** (`FallRules::FallDamage`, PENDING): the ladder-fall curve `clamp((H - safe) x 0.1, 0, 40)` with `damage.safe_height_cm` 320 -
  one 3 m level is free, 6 m = 28, 9 m and more = 40 (the curve's cap); `damage.lethal_height_cm` 0 = no lethal height (the ladder rule has none).
  Any other fall (walked / pushed off by other means, the generator's hole links) is judged the same on landing (falls under `track.min_fall_cm` 50
  are ignored; ladder drops keep their own rule).
- **Knockback falls** (`UKnockdownComponent::StartKnockdown` -> `ULedgeFallComponent::TryKnockbackFall`): a knockdown throws him away from the blow's
  source (no source: on the back = backwards, on the face = forwards); an open edge within `knockback.reach_cm` (120) inside the throw's flight
  (420 cm/s, 200 up: 1.7 m), a drop of at least 150 cm beyond it and no obstacle up to `knockback.parapet_height_cm` (80, a waist-high parapet /
  railing) between him and the edge -> he is thrown over, falls with the fall clip, takes the fall damage and the knockdown plays ON LANDING (the feed
  line "... is thrown off the edge!"). Only from the existing knockdowns (no new damage types), operatives and enemies alike, never in turn-based.
- **Animation**: the operative's ABP has no falling state - during a drop / throw the mannequin rifle clips `MM_Rifle_Jump_Fall_Loop` (looping) and
  `MM_Rifle_Jump_Fall_Land` play on the `FullBody` slot (root motion ignored meanwhile); the enemies fall in their current pose (other skeletons).
  The user may move this into the ABP later (a falling state from `IsFalling`).
- **The order's drop marker** (`ACodexTacticsHUD::DrawDropMarkers`, `CodexTacticsHUDDrops.cpp`; `UDropLinkSubsystem::GatherSquadMarkers`): every drop on
  a squad member's running path (and the one he is taking) - his way to the edge dashed, the drop in amber (free) or red (it hurts, or his run-up now
  would fall short), a down chevron over the gap, the label `FallRules::DropMarkerLabel`: "DROP 3.0 m", "DROP 6.0 m  -28 HP", "... TOO FAR".
- **Checks**: `CodexTactics.FallDropSmoke` on `L_Gen_Depot_1` (7 stages: the squad into the cut off the X_ne bridge one at a time with the marker;
  a 6 m walk-off off the ruin roof, 28 damage; a sprint off the ruin roof onto the annex roof, 0 damage, 725 cm/s kept in the air; the same with a
  slow run-up - TOO FAR, falls to the ground, 28 damage; a knockback throw over the ruin roof edge, 30 damage (6.2 m), knocked down on landing; the
  same blow next to the cut wall - no throw; a Frostbitten thrown too). Pictures: `CodexTactics.FallDropShots` (rendered offscreen) ->
  `Saved/Screenshots/FallsDrops/drop_marker.png`, `drop_midair.png`, `drop_landed.png`.
- **Not done / open**: enemies do not use the drop links (their filters exclude them; a "may drop" enemy filter is one area flag); the cut on
  `L_Gen_Depot_1` is walled by its 2.2 m cut walls everywhere but the crossings, so the drop into it is off the bridges' open sides (the shed
  `B_shed` overlaps the cut wall - a layout issue for the generator); no mp4 clip was recorded. (All three answered on 2026-11-03: see the follow-up below.)

#### Follow-up: the designer's answers (2026-11-03, branch `fix/falls-followup`)

1. **Drop links on every map.** `Codex.Fall.DropLinks` defaults to **2**: every map generates them at level start unless its level JSON says
   `"drop_links": false` (`FLevelCombatConfig::bDropLinksOff`; 1 = only maps that say `true`, 0 = none). A local suppression, without switching the
   feature off: the level JSON's `"drop_link_exclusions": [{"min": [x, y, z], "max": [x, y, z], "why": "..."}]` - no link starts or lands in a box
   (`FLevelCombatConfig::DropLinkExclusions`, counted in the `[DropLinks]` log line; test `CodexTactics.Falls.LevelJson.DropLinks`). The whole smoke
   registry (219, every hand-made map: the lake, the factory, the formation / combat labs, the tutorials, `L_MovementTest`, ...) ran with the links on:
   0 failed, and no smoke but `FallDropSmoke` took a drop link, so no exclusion box was needed. Links per map at level start: `L_MovementTest` 8 (0
   hurt), `L_Demo01_A_FrozenLake` 90 (42), `L_Demo01_B_FrozenFactory` 48 (38), `L_FormationLabCombat` 16 (16), `L_FormationLab` / `L_PatrolTest` 0,
   `L_WeatherRainTest` 12, `L_Gen_Depot_1` 609 (172) - 486 (26) with the faster falls of item 6 (a shorter reach).
2. **Height bands** (`FallRules::JudgeFall` / `JudgeFallForBody`, tests `CodexTactics.Falls.Rules.HeightBands`): below `damage.heavy_height_cm` (900) the
   ladder curve as before (6 m = 28, up to 40); **9-12 m a heavy wound** - `damage.heavy_min` 60 at 9 m rising linearly to `damage.heavy_max` 80 at
   12 m, never more than `damage.heavy_max_health_fraction` 0.85 of the body's max health (an operative at full health, 100 HP, survives: 60-80; a 60 HP
   Frostbitten loses at most 51, a 45 HP hound 38), and he goes down in the existing knockdown pose (`UKnockdownComponent::ForceKnockDown`, the new cause
   `EKnockdownCause::Fall`, feed "... is badly hurt by the fall!"), lies for the knockdown's downed time and gets up by its normal rules (a body that
   cannot be knocked down - the hound, the cutter - only takes the damage); **12 m and more (`damage.lethal_height_cm` 1200) kills**. Operatives and
   enemies alike, for drop links, knockback throws and any other fall (the knockdown of an "other" fall starts on the next tick, outside the movement
   mode callback; a knockback throw that lands in the band plays its own knockdown once). Floating text "FALL -28" / "HEAVY FALL -70" / "FATAL FALL".
3. **Enemies use the drop links, per type** (`FallRules::EnemyDropUse`, falls.json `enemy_drops.<archetype>`: 0 never, 1 free drops, 2 jumper; a
   Blueprint may override it: `AEnemyCharacter::DropLinkUseOverride`; tests `EnemyDropUse`, `EnemyFilters`). The AI controller's filter
   (`DropLinkNavigation::EnemyFilterClass`): **jumpers** - the Frost Hound and the Cutter (the pouncers) - `UNavFilter_EnemyDropJumper`: both drop areas
   at their own costs, and a pouncing run-up cap `enemy_drops.jumper_max_speed_cms` 900 (others 725); **free drops** - the zombies (Base,
   Frostbitten - also the PMC fighters / Military Zombie bodies) and the Marksman - `UNavFilter_EnemyDropFree` (+ `...Ladder` for climbers): the free
   drops open, a hurting one costs `enemy_drops.hurt_entering_cm` 60 m extra (taken only when there is no other way); **never** - the Brute, the
   Spitter, the Cryo Drone - the base `UNavFilter_NoVault` keeps both areas closed. An enemy at a link is taken over by its `ULedgeFallComponent`
   exactly like an operative (run-up, flight, landing, fall damage, `ResumeMoveAfterLadder` to the path's goal); its AI (`TickBehavior`) waits while
   the component is busy (like the ladder). Open: wider-gap "leap" links only a jumper may take (needs an area the operatives' default filter excludes).
4. **Generator** (section 8.6 of `docs/design/levelgen/LEVEL_GENERATOR.md`): no building on the cut walls; `B_shed` of `L_Gen_Depot_1` shrunk off the
   east cut wall, the map rebuilt.
5. **The fall animation is a state of the operative ABP** (`ABP_Operative`, backed up in `Saved/Backups/falls_followup_2026-11-03/`): a state machine
   **`FallStates`** inserted (not rebuilt - the user's polish stays) between the cold-layer blend and the `FullBody` slot - **Grounded** (the cached
   pose "PreFallPose" = everything the graph had before), **Falling** (`MM_Rifle_Jump_Fall_Loop`, looping), **Landing** (`MM_Rifle_Jump_Fall_Land`,
   once); transitions on `UOperativeAnimInstance::bFallAirborne` / `bFallLanding` / `bFallGrounded` (crossfades 0.15 / 0.1 / 0.25 s). The phase comes
   from `ULedgeFallComponent::GetAnimPhase` (`FallRules::FallAnimPhase`, test `AnimPhase`): Falling during a drop / throw flight or after
   `anim.fall_min_s` (0.25 s) of movement mode Falling (a step down never shows it; ladder, vault, pounce and vehicle excluded), Landing for
   `anim.land_s` (0.45 s) after it. A knockdown / death montage on `FullBody` still wins over it. Built headlessly by
   `Scripts/Editor/setup_operative_fall_animation.py` -> `UOperativeAnimGraphLibrary::AddFallStateMachine` (idempotent; `DescribeAnimGraph` writes the
   graph before / after to `Saved/Logs/setup_operative_fall_animation.txt`). The old C++ slot clips stay only as a fallback behind
   `Codex.Fall.SlotClips` (2 default = only for an ABP without `FallStates`, 1 always, 0 never). Enemies keep falling in their current pose.

6. **Faster falls** (the user after testing main, 2026-11-03: "as if they glide down on wings"): while a body is in a ledge fall - a drop link's
   flight, a knockback throw, any walk / push off a ledge - its movement's gravity is x `air.gravity_scale` **2.25** (`ULedgeFallComponent::SetAirGravity`,
   back to its own on landing / EndPlay): the fall time is 1 / 1.5 of before (3 m: 0.78 s -> 0.52 s). A walk / push off a ledge gets it once it is
   40 cm down (a stair step's micro-fall never: applying it at once broke the stairwell walk of `LevelGenRampSmoke`). Console `Codex.Fall.AirGravity`
   0 off / 1 drop links and throws only / 2 (default) also the other falls. Consistent everywhere: the run-up plan and the HUD
   prediction ("TOO FAR") use `FallRules::AirGravity` (so the ruin -> annex gap now needs ~612 cm/s - a walk speeds up to it, the sprint 725 keeps), the
   link generation's reach too (a run-up at 600 cm/s covers 2/3 of the old distance: fewer long-gap links); a knockback throw's speeds are x 1.5
   (`KnockbackLaunchScale`) so its arc and reach (1.7 m) stay and it only plays faster. Not on this path (unchanged): the ladder's own drop
   (`ULadderClimbComponent::TickDropping`) and the cutter's / hound's pounce (its own ballistic flight). Test `CodexTactics.Falls.Rules.AirGravity`.

**Checks:** `CodexTactics.FallDropSmoke` grew to 13 stages (stage 3 now also needs the ABP's Falling -> Landing and no slot clip; 8: the leader off a
test platform spawned 10.5 m up - 71.4 HP, knocked down, gets up; 9: Frostbittens off 10.5 m (51, knocked down, alive) and 13 m (dead); 10: a
Frostbitten chasing the leader off the ruin roof through a free drop link; 11: a Frost Hound across the same roof gap; 12: a Brute never takes a link;
13: a follower dead after 13 m). The platforms: `Debug/FallSmokeUtils.h` (open ground with a clear landing zone, a 3 x 3 m slab). Pictures:
`CodexTactics.FallDropShots` -> `Saved/Screenshots/FallsFollowup/` (`drop_midair.png` with the ABP in Falling, `heavy_fall_lying.png`,
`enemy_drop.png`, + the marker / landed ones).

#### The soft-lock after a roof-hole drop (P0 user bug 2026-11-03, branch `fix/drop-softlock`)

User, editor PIE on `L_Gen_Depot_1`: "When dropping through a hole in the roof the characters just got stuck. All three got hard stuck." - the
squad on the ruin's roof (+2), the leader clicked onto the floor below (-2738, 2783, +1); afterwards every order of every member was "unreachable".

**Root cause** (reproduced headlessly, `DropSoftlockSmoke` with `Codex.Fall.LinkCommit 0`): the leader's path ran through the generator's hole link
(`Drop_B_ruin_hole_roof_1`, a plain one-way `NavLinkProxy` point link 70 cm outside the hole's edge -> the hole's centre one level down) and he
walked off and fell (the "275 cm fall" of the log). The followers' formation slots lay below the hole too, but the squad re-paths a follower
every tick (`FollowTo`): once he stepped past the link's start into the rim's navmesh-free band (the navmesh is eroded by the agent radius at
every ledge), the next request from there came back "already at goal" (the 2D test of a goal 3 m below) - he stopped ON the hole's rim with his
centre 14-22 cm over the hole, held up by the slab's edge, OFF the navmesh. From there no path can start (`MoveToLocation` fails: "unreachable")
- all orders, forever (the leader got the same later from the user's quick re-clicks while crossing a rim). The user's screenshot shows exactly
this: two operatives standing in the dark squares of the roof holes.

**Fixes** (generic):
1. **No re-path off a drop link's rim**: a man whose running path takes a plain (non-smart) nav link that drops a level towards the path's goal
   (the goal lies below the link's upper level - a path that only brushes past a hole on its own floor does not count: that broke the bot's route
   through the ruin) and who stands at its start (within `link.commit_radius_cm` 200) or already over it keeps that path - `FollowTo` is refused
   until he has dropped
   (`AOperativeCharacter::IsCommittedToNavLinkDrop`: the path CORRIDOR's link polys - a crowd path has only 2 points; `FallRules::IsCommittedToDrop`,
   test `CodexTactics.Falls.Reach.CommittedToDrop`). Ladders and the generated drop links are smart links with their own components (not here).
   Console `Codex.Fall.LinkCommit` 1 (0 = the old behaviour, for the smoke's rescue stage).
2. **No drop into a dead-end pocket**: `UDropLinkSubsystem::Generate` keeps a drop link only when its landing poly reaches the main area (the
   player start) over the navmesh, its other nav links (ladders, the generator's hole links - in their own direction) and the other KEPT drops
   (a chain of drops is fine): `FDropLinkReach` (the navmesh's poly graph, ~1-3 ms) + `FReachGraph::SelectLinks` (pure, a fixed point; tests
   `CodexTactics.Falls.Reach.Solve` / `SelectLinks`). On `L_Gen_Depot_1` this rejects 12 links (`[DropLinks] ... rejected: dead end 12`, each
   logged "no link A -> B: it lands in a dead end"): strips behind buildings / beside ramps at the map's edges the pathfinder confirms as dead
   ends (a partial path back). The Lake: 0. Console `Codex.Fall.DropLinkReach` 1 (0 off).
3. **The safety net** (`Falls/LedgeFallComponentRescue.cpp`, falls.json `rescue.*`): after every landing an operative is watched for
   `rescue.watch_s` 20 s; off the navmesh, or on an island he can walk neither back to his fall's start nor to a player start, for
   `rescue.after_s` 2 s -> he is put on the nearest navmesh point of HIS level that reaches the main area (within `rescue.level_tolerance_cm`
   80 of his feet, at most `rescue.max_cm` 1500 away - never a teleport up / down a floor: a man on a runtime test platform 13 m up is left
   alone; `FallRules::PickRescuePoint`, test `Rescue`) and his own order goes on.
   Any time an order cannot start because he stands off the navmesh, the same rescue runs at once and the order is asked again
   (`AOperativeCharacter::RequestMove`; at most every `rescue.cooldown_s` 3 s, a failed try included - no teleport loop, no graph build per
   formation tick). It also catches a follower whose crowd steering cut a corner into a hole's rim band (`LevelGenRouteSmoke`: the Commander
   on the floor hole's rim, 4 rescues of 51-75 cm instead of the old stuck events). Log `[Fall] rescue: <who> was off the navmesh at
   ... - put on ... (64 cm away)` (Warning). Console `Codex.Fall.Rescue` 1 (0 off).

**Checks:** `CodexTactics.DropSoftlockSmoke` on `L_Gen_Depot_1` (7 legs): the user's case (roof -> the click below -> the click on the ground outside:
everybody drops through the hole and reaches the ground, no rescue needed), the same through every ruin hole (`hole_roof_0/1`, `hole_floor_0`),
the sweep (every drop link and one-way hole link lands where the main navmesh is reachable; a bad one is listed with the pathfinder's second
opinion), rescue A (the Engineer put on the user's rim spot, off the navmesh: back on his level's navmesh after 2 s, 64 cm) and rescue B (the
user's case with `Codex.Fall.LinkCommit 0`: the soft-lock happens, the safety net gets everybody down). `CodexTactics.DropLinkReachSmoke`: the
sweep on the Lake (67 drop links, 0 dead ends). Arguments: `stages=user+holes+sweep+rescue`, `trace`, `shots` (rendered, `-RenderOffscreen`:
`Saved/Screenshots/DropSoftlock/`), `regen`, `probe=<x>_<y>_<z>`.

## Vaulting on the generated maps (#36, fix/corner-exit-jerk branch, 2026-11-08)

- **Report (user 2026-11-04, `L_Gen_Depot_1`):** the operatives do not vault over the low obstacles there (vaulting works on the other maps).
- **Root cause:** a vault needs a barricade or an actor tagged `Vault` (`VaultNavigation::IsVaultable`; `ACodexTacticsGameMode::StartPlay` makes
  the tagged ones vaultable: their shapes off the navmesh, a `UNavArea_Vault` modifier instead). The level generator
  (`Scripts/Editor/build_levelgen_map.py`) tags each cover cube with its layout kind only (`sandbag`, `jersey`, `barrel`, `crate`, `wreck`,
  `boiler`) - nothing on a generated map was vaultable, so the paths went round every low cover and a blocked man never climbed.
- **Fix (runtime, the map is not edited or rebuilt):** `StartPlay` tags the low kinds that fit the vault before the usual pass -
  `VaultRules::IsVaultableLevelObject`: kind sandbag / jersey / barrel / low_wall, `MinHeight` 25 .. `MaxHeight` 105 cm tall, the thinner side
  <= `MaxVaultableDepthCm` 130 cm. On the Depot: 13 sandbags (100 cm), 8 jersey barriers (90), 68 barrels (90) vaultable (log `[Vault] 89
  generated low covers made vaultable`); the 100 crates (110 cm - over Godot's 105 cm vault) and the wrecks / boilers stay cover.
- **Open (designer):** the crates are 1.1 m - 5 cm over the vault's limit. Vaulting them needs the vault's `MaxHeight` raised (a gameplay
  change for every map) or the generator's crate height lowered (a Depot rebuild) - not done.
- **Checks:** unit `CodexTactics.Movement.Vault.LevelGenCovers`; smoke **`DepotVaultSmoke`** (`L_Gen_Depot_1`): every sandbag / jersey / barrel
  vaultable (tag + vault nav area), no crate / wreck; the leader ordered straight across a jersey barrier vaults it on the way and arrives
  behind it; in front of a jersey, a sandbag and a barrel he vaults each (`TryVault`) and lands behind it, not on top.
