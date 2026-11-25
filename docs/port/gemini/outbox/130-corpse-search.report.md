# Task 130 — Search Fallen Bodies (Hostile PMC & Squad) + Fresnel Hover

## Root Causes Found
1. **Hostile PMC deaths never spawned remains (`OperativeCharacterHealth.cpp:111-127`)**: `!IsPlayerTeam()` registered the body with `UCorpseSubsystem` and returned early without calling `SpawnRemainsLoot()`.
2. **Sector round squad deaths skipped remains (`OperativeCharacterHealth.cpp:128-131`)**: `if (bSectorRound) { return; }` exited before `SpawnRemainsLoot()`, so fallen player squad members were not searchable in X6 sector fights.
3. **Off-screen / headless corpses never refitted their click box (`LootCrateActor.cpp:641`)**: `TickRemainsFit()` skipped `RefitRemainsBox()` unless the body rendered in the last `0.5s` or used `AlwaysTickPoseAndRefreshBones`.
4. **Hovering a corpse showed no highlight (`CodexTacticsPlayerController.cpp:461` & `OperativeCharacterOverlays.cpp:278`)**: `PlayerTick` only updated the Hand cursor and never applied a Fresnel highlight overlay to `RemainsBody`; additionally, `AOperativeCharacter::UpdateSilhouette()` runs `ClearBodyOverlays(TEXT("dead"))` every tick and would immediately strip any overlay placed on a dead operative's mesh.

## Changes Made
- **`Content/Data/Combat/death_ragdoll.json`**: Added `"corpse_loot"` tuning block (`ammo_mag_min_fraction: 0.2`, `ammo_mag_max_fraction: 0.6`, `medkit_chance: 0.1`, `grenade_chance: 0.05`).
- **`Source/CodexTactics/Public/Interactables/CorpseLootRules.h`, `Source/CodexTactics/Private/Interactables/CorpseLootRules.cpp`**:
  - Added `FCorpseRandomLootConfig`, `ParseCorpseLootConfig`, `GetCorpseLootConfig`, `SetCorpseLootConfigForTesting`, and `BuildHostileRemainsContents(...)` (deterministic seeded roll: 20–60% of a magazine for lootable weapons both carried by the body and usable by the searcher, 10% medkit, 5% grenade).
  - Own squad members continue to use `BuildRemainsContents(Gear)` (exact carried gear).
- **`Source/CodexTactics/Public/Interactables/LootCrateActor.h`, `Source/CodexTactics/Private/Interactables/LootCrateActor.cpp`**:
  - Added `ConfigureHostileRemains`, `IsHostileRemains`, `IsRemainsSearched`, `RollHostileLootForSearcher`, and `bRemainsSearched` once-only enforcement across `IsLooted()`, `FinishOpening()`, `HandleDialogClosed()`, `TakeItem()`, and `TakeAll()`. Empty-rolled hostile corpses still allow one walk-up `0.6s` search and then transition to `[Searched]`.
  - Added `SetRemainsHovered(bool)` / `IsRemainsHovered()` applying a light `/Game/VFX/Materials/M_TargetFresnel.M_TargetFresnel` overlay MID on `RemainsBody` (`Color(1.0, 0.85, 0.45, 0.35)`, `Intensity = 0.25`) while unlooted, restoring previous overlay on unhover or completion.
  - Updated `TickRemainsFit()` to refit without waiting on `WasRecentlyRendered(0.5f)`.
- **`Source/CodexTactics/Private/Characters/OperativeCharacterOverlays.cpp`**: Preserved active `ALootCrateActor` corpse hover overlays (`HoverCrate->IsRemainsHovered()`) inside `ClearBodyOverlays()` while still clearing silhouette/hit/defense overlays on corpses.
- **`Source/CodexTactics/Public/Core/CodexTacticsPlayerController.h`, `Source/CodexTactics/Private/Core/CodexTacticsPlayerController.cpp`**: Added `HoveredRemainsActor` tracking in `PlayerTick` to toggle `SetRemainsHovered(true/false)` as the cursor enters/leaves a remains body.
- **`Source/CodexTactics/Private/Characters/OperativeCharacterHealth.cpp`**: Called `SpawnRemainsLoot()` for hostile PMC deaths (configuring hostile weapon/clip data and seed) and for sector-round player squad deaths.
- **`Source/CodexTactics/Private/Training/TrainingSectorSubsystem.cpp`**: Cleaned up `CorpseLoot`-tagged `ALootCrateActor`s on `RespawnRound()`.
- **`Source/CodexTacticsTests/Private/Interactables/CorpseLootRulesTest.cpp`**: Added `HostileConfigAndAmmoRange` and `HostileSearchedOnceAndHoverState`.
- **`Source/CodexTacticsDev/Private/Debug/CorpseSearchSmokeCommand.cpp` & `Scripts/test_map.json`**: Added and registered X6 smoke `CorpseSearchSmoke` on `/Game/Maps/Generated/L_Gen_Depot_1_X6`.

## Verification
- `powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -CheckMap`: OK.
- `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1`: `Result: Succeeded` (0 errors, 0 new warnings, 0 new unity clashes).
- `powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Filter "CodexTactics.Loot" -SkipBuild`: **10 tests, 0 failed** (including `HostileConfigAndAmmoRange` and `HostileSearchedOnceAndHoverState`).
- `powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands CorpseSearchSmoke -Map "/Game/Maps/Generated/L_Gen_Depot_1_X6" -Jobs 1 -Remote off`: **PASS** (`57.1s`):
  - Spawned both hostile PMC remains (`LootCrateActor_7`) and fallen squad member remains (`LootCrateActor_8`, `7 carried stacks`) in X6 Sector 2 round.
  - Verified `M_TargetFresnel` overlay applied to `RemainsBody` on hover and removed on unhover.
  - Verified walk-to-body search and `LootAll()` granted `+7 M16 rounds` (20–60% of 30) and marked the body `[Searched]`.
  - Verified second search attempt refused (`HandleDirectInteraction` returned false, second world click did not open loot or grant ammo).
