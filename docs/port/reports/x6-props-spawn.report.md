# X6 props spawn hitch — report (2026-11-18, Claude, branch `claude/x6-props-spawn`)

`CodexTactics.GeneratedPropsX6Smoke` failed in the 2026-11-18 night sweep (twice): "longest spawn frame 68.1 ms > 60 ms budget".
The designer is testing the level generator with every asset, so the level-start hitch matters in real play too.

## Cause (measured)

Per-object and per-stage timing was added to `UCodexLevelPropsSubsystem`. A new `[Props] spawn cost:` log line shows it on every spawn.
Baseline run on the unchanged code path, X6, local, `-Multiprocess`:

| Part of the 67.3 ms frame 0 | ms |
|---|---|
| Props file read + parse (64 KB) | 11.6 |
| Layout read + `FPropsLayoutView::Build` + `FPropsValidator` + turret / power rules (layout file `gen_ref_01_02_depot_1_x6.json`, **995 KB**) | 54.2 |
| Spawning the first 3 objects (incl. the first `ATurretActor`, 10.2 ms) | 1.3 |

- **Spawning itself is cheap.** Averages per object: crates 0.29-0.33 ms, barrels 0.27, wires 0.35, loot piles 0.34, generators 0.23, turrets 1.24 (the first one 10.2 ms).
- The frame slicing (20 ms budget) already kept every later frame short.
- **The hitch was the synchronous prepare.** All of it ran on the game thread inside `OnWorldBeginPlay`, before the first object spawned. On X6 it costs 45-80 ms, depending on the run and the machine load.
- The power-layer and real-props merges added props, but the bulk of the cost is reading and validating the big X6 layout.
- **Not the cause:**
  - no sync asset loads (the native classes use the engine basic shapes);
  - no nav rebuilds inside the measured frame;
  - the tactical map loads its own precomputed file, not this layout;
  - the generator / turret link pass takes 0.03 ms.

## Fix

- **Prepare off the game thread (live spawn).** `SpawnPropsFromFile(..., bLive = true)` now does only the game-thread set-up: the balance file and the CVars, about 1-2 ms.
  - It launches `PrepareSession` as a `UE::Tasks` task. The task does the file read, parse, layout view, validator, crate attachments, turret setups and power-link check.
  - `PrepareSession` is pure data: no UObject, no CVar. It works on the session it owns through a shared ref, so a dropped spawn or a torn-down world is safe.
  - The existing frame-slice ticker waits for the task to complete (non-blocking `IsCompleted`). `TakePreparedSession` then copies the dropped ids and the greybox rule, and the spawn runs over frames under `gameplay.spawn_frame_budget_ms` (20) as before.
- **Direct spawn unchanged.** `bLive = false` (tests, tools) runs the same `PrepareSession` inline and spawns everything at once.
- **Same gameplay.** The same objects, order, placement, validator, turret share and loot rolls (seeded by object id). `IsSpawnPending` / `OnPropsSpawned` keep their meaning, and save restore of generators still waits on `OnPropsSpawned`.
- **Measurement made stricter, not weaker.** The generator / turret link pass in `FinishSession` now counts into the last frame. The 60 ms budget and the smoke are untouched.
  - `GetLastMaxFrameMs` is the longest game-thread frame. Frame 0 carries the call's game-thread set-up, which is all of the prepare for a direct spawn.
  - `GetLastPrepareMs` is the prepare time, on a worker thread for a live spawn.

## Before / after (X6, local)

| Run | Longest spawn frame | Prepare | Result |
|---|---|---|---|
| Night sweep (worker, `d5c4ae34`) | 68.1 ms | - | FAIL |
| Baseline here (old code path + timing) | 67.3 ms (prepare 66.0 on the game thread) | 66.0 ms game thread | FAIL |
| After, run 1 | 20.1 ms (frame 1: 86 objects at the 20 ms slice budget) | 45.3 ms worker | PASS |
| After, run 2 | 20.2 ms | 78.8 ms worker | PASS |

The longest frame is now the configured 20 ms slice, not the layout size. The 45-80 ms of prepare work no longer blocks the game thread.

## Files

- `Source/CodexTactics/Public/LevelGen/CodexLevelPropsSubsystem.h`: `PrepareSession` / `TakePreparedSession`; `ResolveLayoutPath` is now static; doc comments for the timings.
- `Source/CodexTactics/Private/LevelGen/CodexLevelPropsSubsystem.cpp`: session split into prepare inputs / outputs / spawn state, the worker-thread prepare, the per-kind cost log, and the link pass counted into the last frame.
- `Source/CodexTacticsTests/Private/LevelGen/CodexLevelPropsSubsystemTest.cpp`: new `CodexTactics.LevelGen.Props.Subsystem.LiveSpawnOffThread`.
  - Nothing is spawned in the call's frame, and the spawn is pending.
  - After the ticker finishes: same count, same dropped ids, same ids at the same places as a direct spawn.

## Tests (local, 2026-11-18; the worker was busy)

- **`GeneratedPropsX6Smoke` passed 5 times in a row.** Longest frame on each run:
  - 20.1 ms and 20.2 ms alone;
  - 21.4 ms in a 7-smoke batch;
  - 20.2 ms in each of two `-Smart` runs.
- **Batch of 7, all PASS:** GeneratedPropsX6, GeneratedPropsSmoke (X4: 20.2 ms, was 45 ms), HitchProbeSmoke (Depot), TacticalMapSmoke, MissionGoalsSmoke, TurretPowerSmoke and DepotCampaignStartSmoke.
- **`Scripts/test.ps1 -Smart`:** 75 unit tests, 0 failed. Smokes all PASS: GeneratedProps, GeneratedPropsX6, TacticalMap, MissionGoals, LevelGenRoute.
- **`Scripts/test.ps1 -Filter CodexTactics.LevelGen.Props`:** 75 tests, 0 failed, including the new `LiveSpawnOffThread`.
- **`typesafe_triage.py --audit-diff --agent claude`:** boundary PASS.

Not verified: a PIE session in the editor, with the designer's real maps and a rendering RHI (the smokes run on `-nullrhi`).

## Lead review follow-up (second commit on the branch)

1. **Thread safety: proven, no snapshot needed beyond what was already there.**
   - `PrepareSession` is a `static` member, so the compiler stops it from reading subsystem members or the variant tables that `LoadBalanceFile` fills.
   - `ResolveLayoutPath` is now `static` too. It only uses `FPaths` / `IFileManager`.
   - The callees are `LevelPropsFile::Parse` / `ApplyOverrides`, `UsesGreybox(File, Mode)`, `FPropsLayoutView::ReadConfig` / `Build`, `FPropsValidator::ValidateResolved` / `ValidatePowerLinks` / `GetVariantHalfSize`, and `PropsGameplayRules::TurretInputOf` / `TurretPoweredShare` / `ResolveTurrets` / `IsGeneratorObject` / `GeneratorInputOf` / `TurretGeneratorOf`, plus `PropsRng::Fnv1a`.
   - A grep of `LevelPropsFile.cpp`, `PropsLayoutView.cpp`, `PropsValidator.cpp`, `PropsGameplayRules.cpp`, `LevelGenJsonRead.h` and `PropsRng.cpp` finds:
     - no function-local `static`;
     - no CVar / `IConsoleManager` / `GConfig` / `GEngine` / `GWorld` / `GetDefault`;
     - no UObject access.
   - The only UObject reflection in those files (`FLootContents::StaticStruct` in `ApplyToContents`) is spawn-time code on the game thread, not part of the prepare.
   - Codex.Props.Greybox, Codex.Props.Real and the traps side are read on the game thread before the launch. The task gets the balance JSON as a shared pointer, read-only. `TSharedPtr` defaults to `ESPMode::ThreadSafe`, so concurrent const reads are safe.
2. **`DroppedPropIds` readers:**
   - the smoke, after `!IsSpawnPending`;
   - `DumpProps`, which now prints a "spawn pending" note during the prepare;
   - the tests, which use direct spawns.
   - Nothing reads it right after a live call.
3. **No worker outlives its spawn or the subsystem.** A session has a `std::atomic<bool> bCancelled`, which the worker checks between phases: after the parse, after the layout read, after `Build`, and after validation.
   - `StopSpawnTicker` (called by a respawn, `ClearSpawnedProps` and `Deinitialize`) sets the flag and calls `Wait()` on the in-flight task. So there is never a second prepare in flight, and none runs after `Deinitialize`.
   - The ticker's world-ending branch sets the flag too.
4. **Failure parity.** The old code failed inside the call, before any session existed: no `OnPropsSpawned`, and it was never pending. A live spawn is pending until its worker finishes. So a failed live prepare now ends the spawn with `OnPropsSpawned` / `OnPropsSpawnedDynamic` (nothing spawned, `HasSpawned` false). A listener that saw it pending, such as the save restore of generators, is released. A direct spawn behaves exactly as before.
5. **No self-reference.** The `FTask` handle lives on the subsystem (`PrepareTask`), not in the session. The task holds the session, and its body is destructed right after it runs.
6. **Tests.** New `CodexTactics.LevelGen.Props.Subsystem.LiveSpawnLifecycle`:
   - a live spawn replaced by a direct one is cancelled and waited for, and never fires;
   - a failed live prepare fires `OnPropsSpawned` with nothing spawned.

   Both live tests reset the begun-play flag before cleanup.

Follow-up verification:
- `test.ps1 -Filter CodexTactics.LevelGen.Props`: 76 tests, 0 failed.
- `GeneratedPropsX6Smoke` run alone: PASS, 20.1 ms.
- Batch of X6, GeneratedPropsSmoke (X4) and TurretPowerSmoke: all PASS. X6 20.2 ms, X4 20.1 ms.
- audit-diff: PASS.
