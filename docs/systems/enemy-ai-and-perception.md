# Enemy AI, registry and pack tactics

> `AEnemyCharacter` + `AEnemyAIController` (tag `Enemy`) implement the Godot enemy behaviour; every enemy registers in `UEnemyRegistrySubsystem`,
> which per-frame queries must use. Pack flanking is a real detour (`EnemyTacticsRules::FlankPoint`). Perception details (patrol, hearing, scent) are in
> [stealth-lure-and-scent](stealth-lure-and-scent.md); the parity notes (affinities, re-path throttle, melee close-in) are in `docs/port/KNOWN_GAPS.md`.
>
> Key files:
> - `Combat/EnemyRegistrySubsystem`, `EnemyRegistryRules`, `Characters/EnemyTacticsRules`, `Characters/EnemyCharacter`, `EnemyAIController`
> - Data `Content/Data/AI/ai_tuning.json`; smokes `EnemyAISmoke`, `EnemyTacticsSmoke`, `EnemyArchetypeSmoke`
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

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

