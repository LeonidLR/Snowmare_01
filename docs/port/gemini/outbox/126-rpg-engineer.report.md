# Gemini 126 — RPG 2 Rockets/Round Cap & Engineer M16 Second Weapon

Model: `Hydrogen`
Branch: `gemini/126-rpg-engineer` (from `origin/main` `d4856826`)

## 1. RPG Cap (Max 2 Rockets Per Round, Both Sides)

- **Data**:
  - `Content/Data/Weapons/weapons_tuning.json`: `weapons.rpg7.default_reserve_ammo` changed from `3` to `1` (`1` loaded + `1` spare = `2` total).
  - `Content/Data/LevelJson/L_Gen_Depot_1_X6.sectors.json`: added `"launcher_rockets_per_round": 2` at top level (read by `FSectorFightConfig::LauncherRocketsPerRound`).
  - `Content/Data/AI/pmc_operators.json`: already had `"ammo": { "rpg7": 1 }` (`1` loaded + `1` spare = `2`).
- **Pure Rules** (`Source/CodexTactics/Public/Combat/LauncherRules.h`, `Private/Combat/LauncherRules.cpp`):
  - Added `LauncherRules::DefaultRocketsPerRound = 2`, `CapAmmo(Clip, Reserve, MaxRockets)`, and `CapReserveToAdd(CurrentClip, CurrentReserve, AddCount, MaxRockets)`.
- **Runtime Enforcement**:
  - `OperativeCharacterWeapons.cpp`: `InitArsenal`, `EquipWeapon`, and `AddAmmo` (including `ExtraAmmo` loot) clamp launcher clip + reserve to `LauncherCap(*this)` (`LauncherRocketsPerRound` when in an active sector, else `2`).
  - `OperativeCharacterSniper.cpp`: `AddArsenalWeapon` clamps launcher clip + reserve to `2`.
  - `TrainingSectorSubsystem::RefillReserves`: already skips `Op->IsLauncherEquipped()` and only refills `CurrentWeapon`, so launchers are never refilled mid-round in `ammo_mode == "resupply"`.

## 2. Engineer Second Weapon (`m16`) & Auto Range Switch

- **Loadout**:
  - `Content/Data/Weapons/weapons_tuning.json`: `"loadouts.engineer"` updated to `["shotgun", "m16", "pistol", "grenade"]` (shotgun in hands at spawn; reuses existing `m16` asset, no `.uasset` changes needed).
- **Pure Rule** (`Source/CodexTactics/Public/Combat/SidearmRules.h`, `Private/Combat/SidearmRules.cpp`):
  - Added `shotgun_switch_range_m` (`12.f`) and `shotgun_switch_back_range_m` (`15.f`) to `FSidearmConfig` and `weapons_tuning.json` (`sidearm` block).
  - Added `SidearmRules::ChooseShotgunOrRifle(...)`: `<= 12 m` -> `SwitchToShotgun`; `> 15 m` -> `SwitchToRifle`; `(12 m, 15 m]` hysteresis -> `Keep`; player-picked weapon (`bPlayerPicked`) -> `Keep`.
- **Runtime** (`OperativeCharacterWeapons.cpp`, `OperativeCharacterCombat.cpp`):
  - Added `AOperativeCharacter::UpdateShotgunRifleChoice()`, called in `UpdateAutoCombat` before `FindShootTarget` when `bAutoFire && !bRaging` (respects `PlayerWeaponPickId` / `bPicked`).
  - Updated `AutoSwitchOnEmpty()` to fall back through `m16` / `shotgun` before `pistol`.

## Verification

| Check | Scope / Command | Result |
|---|---|---|
| Remote build | `Scripts/build.ps1 -Remote worker` | `Result: Succeeded` (`606.69 s`, DLLs synced) |
| Unit tests | `Scripts/test.ps1 -TestFilter "CodexTactics.Weapons" -SkipSmokes` (full suite) | `1671 tests, 0 failed` (`LauncherRulesTest`, `PistolRulesTest`, `WeaponAnimLayerTest` updated & pass) |
| Worker `slot2` smoke (`RpgCapEngineer=1`) | `SectorRoundsSmoke` (`126_rpg_engineer`) | `PASS (58.6 s)`: `rpg7` init `2`, `AddAmmo(5)` capped at `2`, empty switches off `rpg7`, `22 s` resupply leaves `rpg7` at `0`; Engineer `m16` at `30 m`, `shotgun` at `8 m` |
| Worker `slot2` smoke (G019) | `SectorRoundsSmoke` (`126_rounds`) | `PASS (53.9 s)` |
| `ShotgunSmoke` / `RpgPmcSmoke` | Archived map (`L_MovementTest`) | Skipped per checklist #1104 (kept maps only) |
