# Gemini 122 — X6 Sector Starts on Ground, Panic ~5x Less, and Safe Round-Restart Respawn

Model: `Hydrogen`
Branch: `gemini/122-sector2-panic` (from `origin/main` `a7ff5443`)

## 1. Sector Starts & Posts Ground/Nav Audit (`L_Gen_Depot_1_X6.sectors.json`)

Added `CheckStarts=1` to `CodexTactics.SectorBoundarySmoke` (`Source/CodexTacticsDev/Private/Debug/SectorBoundarySmokeCommand.cpp`) to line-trace (`ECC_WorldStatic`, ignoring operative pawns) and navmesh-project all 3 squad slots of every `start` and `pmc_post` across Sectors 1–6 (108 slots total) on `/Game/Maps/Generated/L_Gen_Depot_1_X6`.

### Bad Spots Found & Fixed
| Sector | Entry | Before (`pos`, `yaw`) | Problem Found (`CheckStarts=1`) | After (`pos`, `yaw`) | Verified (`122_check_starts_after`) |
|---|---|---|---|---|---|
| Sector 2 | `starts[1]` | `[3500, 5000, 0]`, `90` | `slot1` `(3380,5000)` and `slot2` `(3260,5000)` hit `StaticMeshActor_2320` (`wreck` at `[3315, 4858]`) at `ground_z=140.0` | `[3900, 5000, 0]`, `0` | All 3 slots `ground_z=0.0` on `StaticMeshActor_0`, next to crate/barrel cover |
| Sector 2 | `pmc_posts[2]` (`s2_post_3`) | `[7000, -1500, 0]`, `0` | `slot0` `(7000,-1650)` hit `CodexPropCrate_67/PropGreybox` at `ground_z=110.0` | `[7000, -1100, 0]`, `0` | All 3 slots `ground_z=0.0` on `StaticMeshActor_0` |
| Sector 5 | `pmc_posts[2]` (`s5_post_3`) | `[-6500, 2500, 0]`, `90` | `slot2` `(-6650,2500)` hit `StaticMeshActor_2788` (`wreck` at `[-6682, 2365]`) at `ground_z=140.0` | `[-6500, 2800, 0]`, `90` | All 3 slots `ground_z=0.0` on `StaticMeshActor_1` |

All other starts and posts across Sectors 1–6 traced to `ground_z=0.0` on the floor mesh.

## 2. Panic ~5x Less & Resupply Low-Ammo Suppression

### Root Cause & Changes
- **Trigger chance & per-operative cooldown** (`PanicRules.h/.cpp`, `PanicComponent.h/.cpp`):
  - Added `FPanicConfig::TriggerChance` (default `0.2f` = 5x fewer) and `FPanicConfig::PostPanicCooldownSeconds` (default `45.f` s).
  - `AttemptTriggerPanic()` checks `CooldownRemaining > 0.f` and squad/leader limits first, then rolls `CodexRandom::FRandRange(ECodexRandomStream::Combat, this, 0.f, 1.f)` **once per trigger event** (on `HoldOnChance`, drops `Stress = 85.f` so it does not re-roll every tick).
  - `RecoverFromPanic()` starts `CooldownRemaining = Config.PostPanicCooldownSeconds`. `ResetForLoad()` clears it.
- **Sector resupply low-ammo suppression** (`PanicRules::IsSectorResupplyPending`, `UPanicComponent::IsSectorLowAmmoSuppressed`):
  - While an X6 training sector is active with `ammo_mode == "resupply"` and `ReserveAmmo <= LowAmmoThreshold`, both `ProcessStress` and `OnLowAmmo()` suppress low-ammo stress growth/jolts and omit `[Out of ammo]` from panic reason.
- **Designer-tunable fields in `UGameBalanceConfig`** (`Source/CodexTactics/Public/Data/GameBalanceConfig.h`, `DA_GameBalanceConfig.uasset` NOT modified):
  - `panic_trigger_chance` (default `0.2f`, range `0.0..1.0`)
  - `panic_cooldown_seconds` (default `45.0f`, `>= 0.0`)

### Before / After Measurement (Same Seed: `Sector=2 Seed=1 PanicMeasure=1`, 90 s)
- **BEFORE** (`Codex.Panic.Legacy 1`, `Saved/Logs/Worker/122_panic_before/Smoke-SectorRoundsSmoke.log`): `PASS` (`60.6 s`), `0` panic triggers (with `ZeroAmmo` + `25 HP`, resupply refilled at `20 s` before core squad stress reached `100`).
- **AFTER** (`Codex.Panic.Legacy 0`, `Saved/Logs/Worker/122_panic_after/Smoke-SectorRoundsSmoke.log`): `PASS` (`60.5 s`), `0` panic triggers.

## 3. Safe Round-Restart Respawn (`TrainingSectorRules`, `TrainingSectorSubsystem`)

- Added `FSectorFightConfig::MinStartPostDistanceM` (`min_start_post_distance_m`, default `40.f` m) and `FSectorFightConfig::RespawnGraceS` (`respawn_grace_s`, default `3.f` s).
- Added `TrainingSectorRules::PickSafeRoundPair`: filters `(start, post)` pairs for 2D distance `>= MinDistanceM * 100` cm and no eye-height (`+160 cm`) `ECC_Visibility` line-of-sight between any of the 3 player start slots and 3 PMC post slots (`UTrainingSectorSubsystem::HasStartPostLos`). If no pair qualifies, falls back to the farthest pair and logs a warning.
- On round restarts (`RoundNumber > 1`), `UTrainingSectorSubsystem` enforces `RespawnGraceUntil = Now + Fight.RespawnGraceS`, holding PMC `bTacticalCeaseFire = true` during the grace window.
- `SectorRoundsSmoke` (`WaitRound2`) now logs and asserts `PairDistM >= Config.MinStartPostDistanceM` (`[SectorRounds] round 2 pair start 0 post 1 dist 98.5 m player 0 cm pmc 0 cm`).

## Verification

| Check | Command / Scope | Result |
|---|---|---|
| Remote build | `Scripts/build.ps1 -Remote worker` | `Result: Succeeded` (`130.91 s`, DLLs synced) |
| Unit tests | `Scripts/test.ps1 -TestFilter "CodexTactics.Training" -SkipSmokes` (runs full suite) | `1671 tests, 0 failed` (`PanicRulesTest` + `TrainingSectorRulesTest` pass) |
| Worker `slot2` smokes | `SectorBoundarySmoke`, `SectorRoundsSmoke` (`122_final_smokes`) | `SectorBoundarySmoke PASS (57.8 s)`, `SectorRoundsSmoke PASS (47.4 s)` (`round 2 dist 98.5 m`) |
| `CheckStarts=1` audit | `SectorBoundarySmoke CheckStarts=1` (`122_check_starts_after`) | `PASS (44.0 s)`, all 3 fixed spots `ground_z=0.0` |
| `PanicSmoke` | Archived map (`L_MovementTest`) | Skipped per checklist #1104 (kept maps only) |
