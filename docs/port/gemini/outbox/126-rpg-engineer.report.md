# Gemini 126 — RPG 2 Rockets/Round Cap & Engineer M16 Second Weapon

Model: `Hydrogen`
Branch: `gemini/126-rpg-engineer` (rebased onto `origin/main` `3c789a86`; conflict in `SectorRoundsSmokeCommand.cpp` resolved keeping both 125 `AccuracyMeasure` and 126 `RpgCapEngineer`)

## 1. RPG Cap (Max 2 Rockets Per Round, Both Sides)

- **Data**:
  - `Content/Data/Weapons/weapons_tuning.json`: `weapons.rpg7.default_reserve_ammo` changed from `3` to `1` (`1` loaded + `1` spare = `2` total).
  - `Content/Data/LevelJson/L_Gen_Depot_1_X6.sectors.json`: added `"launcher_rockets_per_round": 2` at top level (read by `FSectorFightConfig::LauncherRocketsPerRound`).
  - `Content/Data/AI/pmc_operators.json`: already had `"ammo": { "rpg7": 1 }` (`1` loaded + `1` spare = `2`).
- **Pure Rules** (`Source/CodexTactics/Public/Combat/LauncherRules.h`, `Private/Combat/LauncherRules.cpp`):
  - Added `LauncherRules::DefaultRocketsPerRound = 2`, `CapAmmo(Clip, Reserve, MaxRockets, FiredThisRound)`, and `CapReserveToAdd(CurrentClip, CurrentReserve, AddCount, MaxRockets, FiredThisRound)` so `FiredThisRound + Clip + Reserve <= MaxRockets`.
- **Runtime Enforcement (Player + PMC)**:
  - `OperativeCharacter.h` / `OperativeCharacterLauncher.cpp`: added per-operative `RocketsFiredThisRound` counter, incremented on every `FireLauncherAt` shot, and reset only at round start in `UTrainingSectorSubsystem::TryStartRound` (`ResetLauncherRoundCounter()`).
  - `OperativeCharacterWeapons.cpp` / `OperativeCharacterSniper.cpp`: `InitArsenal`, `AddArsenalWeapon`, `EquipWeapon`, and `AddAmmo` (including `ExtraAmmo` loot) enforce `cap - fired - held` (`CapAmmo` / `CapReserveToAdd`).
  - **PMC carriers**: `UPmcSquadSubsystem::SpawnFighter` spawns an `AOperativeCharacter` (so `RocketsFiredThisRound` starts at `0`) and sets `"rpg7"` reserve via `AOperativeCharacter::AddAmmo`, which enforces the same `cap - fired - held` budget.
  - `TrainingSectorSubsystem::RefillReserves`: skips `Op->IsLauncherEquipped()` and only refills `CurrentWeapon`, so launchers are never refilled mid-round in `ammo_mode == "resupply"`.

## 2. Engineer Second Weapon (`m16`) & Auto Range Switch

- **Loadout**:
  - `Content/Data/Weapons/weapons_tuning.json`: `"loadouts.engineer"` updated to `["shotgun", "m16", "pistol", "grenade"]` (shotgun in hands at spawn; reuses existing `m16` asset, no `.uasset` changes needed).
- **Pure Rule** (`Source/CodexTactics/Public/Combat/SidearmRules.h`, `Private/Combat/SidearmRules.cpp`):
  - Added `shotgun_switch_range_m` (`12.f`) and `shotgun_switch_back_range_m` (`15.f`) to `FSidearmConfig` and `weapons_tuning.json` (`sidearm` block).
  - Added `SidearmRules::ChooseShotgunOrRifle(...)`: `<= 12 m` -> `SwitchToShotgun`; `> 15 m` -> `SwitchToRifle`; `(12 m, 15 m]` hysteresis -> `Keep`; player-picked weapon (`bPlayerPicked`) -> `Keep`.
- **Runtime** (`OperativeCharacterWeapons.cpp`, `OperativeCharacterCombat.cpp`):
  - Added `AOperativeCharacter::UpdateShotgunRifleChoice()`, called in `UpdateAutoCombat` before `FindShootTarget` when `bAutoFire && !bRaging` (respects `PlayerWeaponPickId` / `bPicked`).
  - Updated `AutoSwitchOnEmpty()` to fall back through `m16` / `shotgun` before `pistol`.

## Verification (Post-Rebase on `origin/main` `3c789a86`)

- **Rebase & Conflict Resolution**:
  - `SectorRoundsSmokeCommand.cpp`: kept both 125 (`AccuracyMeasure=1`, `Rounds=`) and 126 (`RpgCapEngineer=1`).
- **`VehicleCombatSmoke` 3x vs 3x Comparison**:
  - Rebased `gemini/126-rpg-engineer` (`25c934e7`): **PASS 3/3** (`62.9 s`, `58.0 s`, `57.6 s`).
  - `origin/main` (`3c789a86`): **PASS 2/3, FAIL 1/3** (`r1 FAIL 127.4 s`, `r2 PASS 65.7 s`, `r3 PASS 81.2 s`). `r1` failed on a pre-existing smoke issue (same in prior merge-gate baseline logs): dismounted `Medic-Sapper` (`Aggressive` posture, M16 at ~32 m) killed the dead-zone hound `Close` (`BP_Enemy_Hound_C_1`) in Stage 6 (`0 shots, 14 refusals` by the APC turret), failing `!IsDead(Close)`. Confirmed not caused by 126.

| Check | Scope / Command | Result |
|---|---|---|
| Dispatched build | `machine_dispatch.py build --lane CodexTactics-126` | `Result: Succeeded` (`1182.01 s`, exit `0`) |
| `VehicleCombatSmoke` (3x on rebased 126) | `machine_dispatch.py smoke --smokes VehicleCombatSmoke,VehicleCombatSmoke,VehicleCombatSmoke --jobs 1` | `PASS 3/3` (`62.9 s`, `58.0 s`, `57.6 s`) |
| `VehicleCombatSmoke` (3x on `origin/main` `3c789a86`) | `machine_dispatch.py smoke --smokes VehicleCombatSmoke,VehicleCombatSmoke,VehicleCombatSmoke --jobs 1` | `PASS 2/3` (`r1 FAIL 127.4 s` — `Medic-Sapper` killed `Close`; `r2 PASS 65.7 s`, `r3 PASS 81.2 s`) |
| Impact map | `Scripts/test.ps1 -CheckMap` | `Impact map OK` |
| Unit tests | `Scripts/test.ps1 -Filter "CodexTactics.Combat+CodexTactics.Weapons+CodexTactics.Training+CodexTactics.Characters" -SkipSmokes` | `325 / 326 PASS` (1 pre-existing upstream FAIL: `CodexTactics.Weapons.Schema.NoLegacyReads` in `OperativeCharacterHealth.cpp` lines 448 & 455, untouched by 126) |
| `SectorRoundsSmoke` | `machine_dispatch.py smoke --smokes SectorRoundsSmoke --jobs 1` | `PASS` (`63.0 s`) |
| `SectorRoundsSmoke` (`RpgCapEngineer=1`) | `Scripts/smoke_parallel.ps1 -Commands SectorRoundsSmoke -SmokeArgs "RpgCapEngineer=1" -Remote off -Jobs 1` | `PASS` (`64.1 s`) |
| `SectorRoundsSmoke` (`AccuracyMeasure=1 Rounds=1`) | `Scripts/smoke_parallel.ps1 -Commands SectorRoundsSmoke -SmokeArgs "AccuracyMeasure=1 Rounds=1" -Remote off -Jobs 1` | `PASS` (`87.0 s`) |
| `ShotgunSmoke` / `RpgPmcSmoke` | Archived map (`L_MovementTest`) | Skipped per checklist #1104 (kept maps only) |
