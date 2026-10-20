# Enemy AI, registry and pack tactics

> `AEnemyCharacter` + `AEnemyAIController` (tag `Enemy`) implement the Godot enemy behaviour; every enemy registers in `UEnemyRegistrySubsystem`,
> which per-frame queries must use. Pack flanking is a real detour (`EnemyTacticsRules::FlankPoint`). Perception details (patrol, hearing, scent) are in
> [stealth-lure-and-scent](stealth-lure-and-scent.md); the parity notes (affinities, re-path throttle, melee close-in) are in `docs/port/KNOWN_GAPS.md`.
>
> Key files:
> - `Combat/EnemyRegistrySubsystem`, `EnemyRegistryRules`, `Characters/EnemyTacticsRules`, `Characters/EnemyCharacter`, `EnemyAIController`
> - Data `Content/Data/AI/ai_tuning.json`; smokes `EnemyAISmoke`, `EnemyTacticsSmoke`, `EnemyArchetypeSmoke`, `PmcFighterSmoke` (Frozen PMC Fighter, last section)
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
