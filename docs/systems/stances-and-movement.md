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
  `B_shed` overlaps the cut wall - a layout issue for the generator); no mp4 clip was recorded.
