# 004-scout — enemy spawn hitch
Snapshot: main 418b3f1d, 2026-11-15. Code-only hypotheses; no profiling, builds, editor, smokes or logs read.
Paths below are relative to Source/CodexTactics/Private unless explicitly stated otherwise.

## Spawn chain (file:line list)
- GameFlow/GameFlowSubsystem.cpp:80,91: HandleStateChanged -> OnGameFlowChanged.Broadcast; Combat/WaveSubsystem.cpp:87 subscribes HandleGameFlowChanged.
- Combat/WaveSubsystem.cpp:684,692: WaveCombat -> StartWave; :151,182 -> SpawnLevelWave.
- Combat/WaveSubsystem.cpp:350,357,361: nested roster/count loops -> FindFreeSpawnSpot -> SpawnEnemyInternal (:430).
- Combat/WaveSubsystem.cpp:448,455: resolve variant/base Blueprint synchronously -> World->SpawnActor<AEnemyCharacter> (:469) -> InitializeArchetype / PlaySpawnEntrance (:473,475).
- Characters/EnemyCharacter.cpp:72,94-103: BeginPlay -> archetype/balance, knockdown, ladder, patrol/guard and look setup.
- Alternative queued route: Combat/WaveSubsystem.cpp:128 -> ProcessPendingSpawns (:380,396), minimum interval 0.2 s (:398); this does not budget SpawnLevelWave's immediate loop.
- Ambient pressure: Combat/CombatPressureSubsystem.cpp:228,290 -> TriggerEvent (:1164) -> Waves->SpawnLevelEnemy (:1048) -> Combat/WaveSubsystem.cpp:427,430.
- PMC encounter: AI/PmcEncounterSubsystem.cpp:89,123 -> SpawnEncounter (:133,304) -> AI/PmcSquadSubsystem.cpp:2015,2043 SpawnFighter -> Core/CodexTacticsGameMode.cpp:365,376 SpawnOperative -> SpawnActorDeferred.

## Top 3 suspects, ranked
1. **Cold synchronous class/body/animation loads.** Combat/WaveSubsystem.cpp:448,455; AI/PmcSquadSubsystem.cpp:2037,2063,2086,2106; Characters/OperativeAnimInstance.cpp:67,84; OperativeAnimInstanceShotgun.cpp:191. First use can block on package loading/deserialization and mesh/AnimBP initialization; weapon sets are bound per anim instance. Fix: async prepare the actual roster, bodies, weapons and skeleton-specific clips before gameplay, retain references and verify mesh readiness. Risk: high (startup lifecycle, fallback and GC).
2. **Unbudgeted batch spawn plus placement work.** Combat/WaveSubsystem.cpp:357-364,640,662-675; AI/PmcEncounterSubsystem.cpp:113-123,213,304. A scripted wave constructs the entire roster in one call, multiplying nav projection/collision and BeginPlay cost; PMC also performs synchronous path queries and loops due squads. Fix: use a per-frame spawn/time budget shared by wave/pressure/PMC while preserving seeded order and wave-start semantics. Risk: med.
3. **Per-enemy ladder loading/root extraction.** Characters/EnemyCharacter.cpp:97 -> EnemyCharacterLadder.cpp:21 -> Ladders/LadderClimbComponent.cpp:210,227-229,236-248. The loaded flag is component-local; each climber resolves three clips and extracts/corrects root tracks, even when packages are already warm. Fix: preload clips and cache immutable prepared tracks per world, keyed by clip identities AND mesh-relative rotation. Risk: med.

## First-frame / pre-warm findings
- Body variants are swapped during BeginPlay: Characters/EnemyCharacterPerformance.cpp:55,76 SetSkeletalMeshAsset; Characters/EnemyCharacterArchetype.cpp:246 creates a dynamic material instance. These amplify batch cost; CPU/DDC/render-resource cost is unmeasured.
- Additional synchronous balance reads: Characters/EnemyCharacterArchetype.cpp:135,268; Combat/WaveSubsystem.cpp:189; Core/CodexTacticsGameMode.cpp:392,411,461,472,482,492.
- AI possession sets crowd avoidance and refreshes nav filters (Characters/EnemyAIController.cpp:34-45); patrol spreads initial sight checks across 0.2 s (EnemyCharacterPatrol.cpp:59-60). No heavy custom PostInitializeComponents or outside-constructor ConstructorHelpers found in inspected spawn files; EnemyCharacter.cpp:56 is inside its constructor.
- Core/GameplayPreloadCatalog.cpp:82,172 collects initial/animation paths only. Source search found definitions but no gameplay callers or enemy-loading coordinator; GameMode StartPlay still calls SpawnSquad directly (:132). No enemy actor pool or enemy Niagara pre-warm found; weather/UI async loading and unrelated pools are not enemy preparation. Constructor component allocation also still occurs per SpawnActor (EnemyCharacter.cpp:49).

## Existing probe (two lines)
- Source/CodexTacticsDev/Private/Debug/HitchProbeSmokeCommand.cpp:69,79,198 measures OnBeginFrame-to-OnEndFrame wall-clock CPU-frame time after setup, plus timed first-use roster/PMC spawns; not GPU duration or startup cost.
- Same file :29,108-114 fails if max measured frame, any individual spawn, or same-frame spawn sum exceeds 250 ms; this gate can miss noticeable 16-33 ms stutters.

## Open questions
- Which route/type and map reproduce the symptom: scripted wave, pressure or PMC; first encounter only or warm repeats? Code alone cannot rank their measured contribution.
- Are variant mesh/DDC, AnimBP evaluation or render/PSO creation responsible after assets are resident? No timing claims without a targeted trace.
- Is the async preload WIP intentionally absent from current main? The catalog alone does not provide preparation or a readiness gate.
- What is the target frame budget for small stutters? The existing 250 ms probe is too coarse for normal frame pacing.

Done by Codex, files read: 17
