# Enemy AI, registry and pack tactics

> `AEnemyCharacter` + `AEnemyAIController` (tag `Enemy`) implement the Godot enemy behaviour; every enemy registers in `UEnemyRegistrySubsystem`,
> which per-frame queries must use. Pack flanking is a real detour (`EnemyTacticsRules::FlankPoint`). Perception details (patrol, hearing, scent) are in
> [stealth-lure-and-scent](stealth-lure-and-scent.md); the parity notes (affinities, re-path throttle, melee close-in) are in `docs/port/KNOWN_GAPS.md`.
>
> Key files:
> - `Combat/EnemyRegistrySubsystem`, `EnemyRegistryRules`, `Characters/EnemyTacticsRules`, `Characters/EnemyCharacter`, `EnemyAIController`
> - Data `Content/Data/AI/ai_tuning.json`; smokes `EnemyPrioritySmoke` (operatives first, last section), `EnemyAISmoke`, `EnemyTacticsSmoke`, `EnemyArchetypeSmoke`, `PmcFighterSmoke` (Frozen PMC Fighter), `MilitaryZombieSmoke` (Military Zombie, last section)
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Code layout (cleanup 2026-10-19)

`AEnemyCharacter` is one header (`Public/Characters/EnemyCharacter.h`) and ten translation units under `Private/Characters/`, one per cluster (map, rules and verification: [REFACTOR_ENEMY_2026-10](../port/REFACTOR_ENEMY_2026-10.md)): `EnemyCharacter.cpp` (ctor, BeginPlay / EndPlay, Tick, facing, AI move requests, turn-based / world-AI-pause holds, overhead label), `EnemyCharacterArchetype.cpp` (spawn archetype scope, archetype defaults, knockdown config, wave modifiers, balance), `EnemyCharacterBehavior.cpp` (`TickBehavior`: fire fear, pack order + flank detour, melee close-in and the `Codex.Enemy.MeleeCloseIn` CVar; target selection, fire zones), `EnemyCharacterCombat.cpp` (blows, object / vehicle attacks, spitter, cutter jump), `EnemyCharacterHealth.cpp` (knockdown hooks, damage, trap blast scope, death), `EnemyCharacterPatrol.cpp` (routes, escorts, guard posts, patrol break), `EnemyCharacterSave.cpp` (patrol snapshot), `EnemyCharacterPerception.cpp` (sight / hearing, vision cone, squad noise), `EnemyCharacterScent.cpp` (hound scent), `EnemyCharacterSearch.cpp` (patrol search, stealth lure). New code goes into its cluster's unit; a file-local helper needs a FILE-UNIQUE name (unity blobs merge the units). Tests `CodexTactics.Characters.EnemyUnits.*`.

## Enemy registry (old §5)

- `Combat/EnemyRegistrySubsystem` + `EnemyRegistryRules` (cleanup 2026-10-16, audit 2026-10-11 #2 / P2-P3; Godot: the "enemies"
  group) — every `AEnemyCharacter` registers in BeginPlay / leaves in EndPlay; other actors carrying the `Enemy` tag
  (`CombatQueries::EnemyTag`) are picked up at world begin play, when spawned with the tag, or by `RegisterTaggedActor` (a tag
  added after the spawn: CombatFlow / RealTimeControl smoke stand-ins). `GetTaggedEnemies(World)` / `GetEnemyCharacters(World)`
  return a snapshot (inline storage, safe to kill / spawn while walking it) in registration order and fall back to the old world
  scan before the world has begun play (OnWorldBeginPlay code, the actors' own BeginPlay, editor worlds). Used by the per-frame
  queries: operative targeting (`FindShootTarget`, `FindBestCombatTarget`; the shot line's squad + turret ignore list is gathered
  once per frame, `OperativeCharacterInternal::AddShotLineAllies`), trap / turret / mine / crate / barricade ticks, panic, rage,
  fire puddle, blasts, `CombatQueries::HasEnemiesWithin`, HUD plates, sight, pack tactics, autonomy, cover / corner threat, marksman
  / spitter lines, patrol noise, vehicles. **New per-frame enemy code: use the registry, never `TActorIterator<AActor>`** (test
  `Combat.EnemyRegistry.HotPathsUseRegistry` scans the converted files). Measured with the dev probe `CodexTactics.EnemyPerfProbe`
  (55 unkillable enemies, fixed 60 fps step): world tick 15.2 -> 13.4 ms mean.

## Pack flanking = a real detour (old §5)

**Pack flanking = a real detour (user decision 2026-10-18)** — `EnemyTacticsRules::FlankPoint` now lies 450 cm from the target, 90 deg off
the target->enemy line, on his OUTER side (away from the squad's centre; a centre target: a fixed side) - the old point (70 deg off the
centre->target axis towards the enemy) lay in front of a line's wings. A Flank-role enemy walks there first
(`AEnemyCharacter::TickBehavior`, latched per target in `FlankDoneTarget`) and closes in only when `EnemyTacticsRules::EvaluateFlankDetour`
ends the detour: at the point (<= 150 cm), or within bite reach (+40 cm) on the flank side (<= 60 deg off the point's direction), or - never
frozen - after 1.5 s without 50 cm of progress (a blocked / unreachable point) or 8 s in all. Knobs (`ai_tuning.json` / console):
`Codex.Enemy.FlankDetour` (0 = the old 5 m cut, A/B), `FlankArriveCm`, `FlankSideConeDeg`, `FlankTimeout`, `FlankStuckSeconds`. `[Melee]`
log lines name the side a blow came from (front < 60 < side < 120 < back). Tests `CodexTactics.Characters.EnemyTactics.FlankDetour`; smoke
`EnemyTacticsSmoke` (per-hound first contact angle, a flank-role hound from the side / back, bites from the side / back).


## Frozen PMC Fighter = a Frostbitten variant body (game designer 2026-10-20, branch agents/pmcfighter)

The designer added the pack `/Game/Cyberpunk_Explorer_A` and asked for a new enemy on it animated like the Frostbitten.
`/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/BP_Enemy_FrozenPMC_Fighter` (made by `Scripts/Editor/create_frozen_pmc_fighter.py`, idempotent,
never overwrites a saved asset; `CODEX_PMC_REAPPLY=1` re-applies the BP fields) is a **child Blueprint of `BP_Enemy_Frostbitten`**:

- **Why a child BP, not the C++ class:** the archetype then comes from the existing safety net (`EnemyAIRules::ResolvePlacedArchetype` walks the
  class chain: BP_Enemy_FrozenPMC_Fighter_C -> BP_Enemy_Frostbitten_C = mapped to `Frostbitten`), so stats (60 HP, 2.8 m/s, 15 dmg, light armor,
  affinities), AI, knockdown (a >= 40 HP hit floors it), vision cone (18 m), kill EXP and every later edit of the Frostbitten Blueprint's AI tunables
  carry over; no new `EEnemyArchetype`, no game-mode mapping. The child overrides only its look: mesh, ABP, capsule 40 / 90 (written explicitly),
  mesh z -90 / yaw -90 / scale 1 (the 181 cm body has its feet at its origin and faces +Y like the Frostbitten).
- **Name:** new `AEnemyCharacter::DisplayNameOverride` (EditDefaultsOnly; `EnemyAIRules::ResolveDisplayName`) = "Frozen PMC" on the nameplate, the
  feed and the turn-based panel; the Frostbitten keeps "Frostbitten". No balance row of its own (stats = Frostbitten's).
- **Knockdown clips:** new `bOverrideKnockdownClipFolder` + `KnockdownClipFolder` (`EnemyAIRules::NormalizeKnockdownClipFolder`, applied in
  `ConfigureKnockdown` when the archetype can fall): "" = `/Game/Animations_KnockDown` itself, the UE5 Manny "RTGT" originals the operatives use (the
  `Frostbitten/` copies are on the Frostbitten's UE4 skeleton).
- **Waves:** the wave spawner still spawns `BP_Enemy_Frostbitten` for `frostbitten` (the class mapping is per archetype). The PMC is placed by hand
  (it is on no map yet) or spawned by code; a `frozen_pmc` wave type would need an archetype or a per-entry class (not done, ask first).
- Registry / sight / cone / save: inherited from `AEnemyCharacter` (the save stores the class path, so a loaded PMC comes back as a PMC).
- Tests: unit `CodexTactics.Characters.EnemyAI.VariantBody` (name / folder rules, the child -> Frostbitten resolution),
  `CodexTactics.Characters.EnemyAssets.FrozenPmcFighter` (the Blueprint as loaded: parent, mesh copy = the pack's geometry / materials / physics
  asset, ABP + every clip on the mesh's skeleton, same clip set and rates as the Frostbitten's, nameplate, knockdown clips, capsule / offset /
  facing; skipped without the pack); smoke `PmcFighterSmoke` (placed -> Frostbitten / 60 HP / "Frozen PMC" / cone; spawned like a wave enemy: idle not
  in the reference pose, feet on the ground, walks to the squad, attacks the leader, hit clip, Manny knockdown, death from the ground, a second one
  dies standing; `shot` = `Saved/Screenshots/pmc_fighter_*.png`, rendered). Animation route and skeleton facts: [animation-setup](animation-setup.md)
  "Frozen PMC Fighter".


## Military Zombie = a Frostbitten variant with its own clips (game designer 2026-10-20, branch agents/zombie2)

The designer added the pack `/Game/military_zombie_set2` ("set up its Blueprint and Animation Blueprint - all the animations it has"; user
guidance the same day: "use the Frostbitten Animation Blueprint on this enemy, just swap its animations").
`/Game/Characters/Enemies/MilitaryZombie/BP_Enemy_MilitaryZombie` + `ABP_Enemy_MilitaryZombie` (made by `Scripts/Editor/create_military_zombie.py`,
idempotent, never overwrites a saved asset; `CODEX_MZ_REAPPLY=1` re-applies the class defaults / BP fields):

- **Parent:** a child Blueprint of `BP_Enemy_Frostbitten`, like the Frozen PMC Fighter (archetype Frostbitten through the class mapping: 60 HP,
  2.8 m/s, melee AI, knockdown, 18 m cone, kill EXP). Nameplate `DisplayNameOverride` = "Military Zombie"; knockdown clips =
  `/Game/Animations_KnockDown` itself (`bOverrideKnockdownClipFolder`, folder ""): the pack's skeleton is a copy of the Epic UE5 Manny with the
  same 161 bone names / hierarchy, so the Manny originals play by bone name (verified: `Knocked_Back`, `Death_Back` from the ground).
- **Look variants (new, data):** `AEnemyCharacter::LookVariants` (`FEnemyLookVariants`: `Meshes` + `MaterialSets` of `FEnemyMaterialSet` slot-name ->
  material) picked at BeginPlay by `GetVariantSeed()` (`EnemyAnimRules::MakeVariantSeed` of the actor name + spawn point, or `VariantSeedOverride`
  >= 0 per instance): the zombie has the pack's 3 assembled bodies (`SKM_base_mesh_set2_1` helmet + kit + armor, `_2` blouse + armor + hair,
  `_3` bare body) x the 3 texture sets (`material/set_1/2/3`, slots by name; the helmet exists in set 1 only). The save stores the class only:
  a loaded zombie may come back with another look (cosmetic).
- **Alert roar (new):** `BreakPatrol` (a guard / patrol notices the squad: sight, hearing, damage, a partner's alarm) calls
  `StartAlertPerformance`: the ABP's `AlertAnimations` clip (`anim_rage` x1.2 = 2.8 s) plays and the enemy stands still facing the alarm point
  (`PerformanceLockTimer`, held in `TickBehavior` before the patrol / combat code by `TickPerformanceLock`; a hit / knockdown that replaces the clip
  ends the hold at once). Wave / horde spawns hunt from the start and do not roar. **Gameplay note:** the roar gives the squad ~2.8 s before
  the charge; tunable per ABP (`AlertPlayRate`, `AlertMaxSeconds`, or empty `AlertAnimations` = no roar).
- **Spawn entrance (new):** `UWaveSubsystem::SpawnEnemy` calls `AEnemyCharacter::PlaySpawnEntrance()` on every wave / horde / turn-based
  reinforcement spawn (a no-op for every enemy whose ABP has no `EntranceAnimations`); a placed enemy plays it at BeginPlay with the instance flag
  `bPlayEntranceWhenPlaced`. Save-game restores do not go through `SpawnEnemy` (no entrance on load). The zombie plays the last part of
  `anim_transformation` (from 14.5 s at x1.5 = ~4 s) standing, then hunts.
- **Waves:** like the PMC, the wave spawner still spawns `BP_Enemy_Frostbitten` for `frostbitten` (one class per archetype). The zombie is placed by
  hand or spawned by code; a `military_zombie` wave type (an archetype or a per-entry class) is not done - ask first.
- Code: `Private/Characters/EnemyCharacterPerformance.cpp` (new unit), pure rules `Characters/EnemyAnimRules` (tests
  `CodexTactics.Characters.EnemyAnim.*`); asset test `CodexTactics.Characters.EnemyAssets.MilitaryZombie`; smoke `MilitaryZombieSmoke`
  (`Saved/Screenshots/military_zombie_*.png`). Animation route and clip roles: [animation-setup](animation-setup.md) "Military Zombie".

## Operatives first (game designer 2026-10-25, branch agents/enemy-priority)

**Report** (packaged build, Demo 01 tutorial, the A4 lake zombie): "when it switched to combat it once attacked the barricade instead of me.
Hard-code it: with a barricade or a turret around they must ALWAYS attack the player, an operative, first; only when enough other enemies
already attack the operative may one be distracted by the barricade or the turret."

**Cause (two parts).** (1) The Godot rule `_find_blocking_barricade` (`AEnemyCharacter::FindBlockingObstacle` + `TickBehavior` step 1): ANY
live barricade / turret within 2.2 m roughly towards the target (dot > 0.15) is smashed first - Godot enemies cannot walk round a barricade,
UE enemies path round it on the navmesh. (2) The barricade's vault nav area (closed to enemies, `UNavFilter_NoVault`) was not the barricade:
`VaultNavigation::MakeVaultable` takes the box off the navmesh before adding the `UNavModifierComponent`, whose bounds then fall back to its
`FailsafeExtent` - a 2 x 2 m square round the middle of a 3 m box. The last 50 cm of each end were open navmesh under a solid box, so an
enemy going round the barricade walked into its end, stuck there within 2.2 m, and the Godot rule made it hack at the barricade. Also: small
enemies (hounds, cutters, frostbitten - the Military Zombie is a Frostbitten) weigh a turret at 0.5 x its distance, large ones take a turret
within 10 m (Godot `_find_closest_squad_member`), so a lone enemy left the operative for a turret.

**Rule now** (pure `EnemyAIRules`, tests `CodexTactics.Characters.EnemyAI.OperativesFirst.*`):
- `MayBeDistractedByObjects(OtherAttackersPerOperative, Cap)`: a barricade / turret may take an enemy off the squad only when EVERY operative it
  could go for (known, alive, not on its no-way list, not in a feared fire zone) already has at least `Cap` OTHER enemies on him (their current
  target), or when there is no such operative. Computed in `FindTarget` (registry walk), kept as `IsObjectDistractionAllowed()`.
- `SelectTarget(..., bObjectsAllowed)`: not allowed -> a turret is taken only when no usable operative / manned APC candidate is left. The
  generator keeps its Godot weight (0.4 x distance for small enemies, the wave objective - not a barricade / turret; changing it is a design
  question). A horde still ignores objects while it knows the squad.
- `ShouldSmashBlockingObstacle(bTargetIsOperative, bReachable, bAllowed)`: the barricade / turret in the way is smashed for an operative target
  only when he cannot be reached on foot or objects are allowed; any other target keeps the Godot rule.
- Reachability `AEnemyCharacter::IsTargetReachableOnFoot` (cached 0.5 s): a navmesh path query with the enemy's own filter, partial paths
  allowed, goal projected WITH that filter (a crouched operative's feet in the vault strip snap to his side, never a false "unreachable" at a
  cover spot); reachable when `PathEndReachesTarget` (the bite test `CanMelee` from the path end, 30 cm navmesh height slack). Unreachable
  also while the stuck detector has him on its no-way list (3 s without 1 m of progress -> 8 s). No navmesh / off-mesh / failed query =
  reachable (no evidence against the squad; the stuck detector decides). The query only runs for an obstacle next to an enemy that may not
  be distracted. A skipped obstacle logs `[EnemyAI] <enemy>: <obstacle> is in the way but <op> is reachable on foot - round it` once.
- Vault area = the real obstacle: `VaultRules::NavAreaExtent` (actor-space collision bounds x scale, mirrored round the pivot, + 40 cm
  `NavAreaPaddingCm` = an enemy capsule radius on X / Y, + 50 cm down) as the modifier's failsafe box. A barricade: 3.8 x 1.4 m (was 2 x 2 m).
  Applies to level objects tagged `Vault` as well (they had the same 2 x 2 m square whatever their size).
- Turrets were never reached on foot: the box cuts a navmesh hole wider than the 50 cm move query, `MoveToActor` failed ("no way to
  TurretActor" after 3 s, every time). An enemy going for a turret now walks to `FindObjectApproachPoint` (just outside it on its side, else
  the nearest navmesh point round it) and hits it from there.

**Tunable:** `Codex.Enemy.OperativeAttackersBeforeObjects` - code default **2**, game value in `Content/Data/AI/ai_tuning.json` "cvars"
(`"2"`), editable in the Wave Editor stealth / combat tab (`Tools/TrainingGround/src/utils/stealthCombat.ts` `CVARS`); `-dpcvars=` wins;
**0 = the old Godot weighting** (A/B). Turn-based enemies only ever target operatives (`TurnBasedCombatSubsystem` `ChooseTarget`), unchanged.

**Checks:** unit tests `CodexTactics.Characters.EnemyAI.OperativesFirst.{Saturation, TargetPick, BlockingObstacle, Reachability,
VaultAreaCoversBarricade, CapByData}`; smoke `EnemyPrioritySmoke` (L_MovementTest, own arena): vault area 190 x 70 cm, the enemy path round
clears the box (76 cm); a lone Frostbitten with a turret 3.6 m off and a barricade between it and the commander picks him, walks round
(3.3 s), barricade and turret untouched; cap - 1 others on the commander -> still an operative, cap on everyone -> the turret, hit within
1 s; a saturated extra one in front of the barricade smashes it; a closed ring of barricades round the commander -> unreachable (a mate
outside reachable), a lone enemy smashes its way in. `EnemyAISmoke` now checks the opposite of its old turret / barricade stages (a lone hound
ignores a close turret, a lone brute leaves a free-standing barricade alone).
