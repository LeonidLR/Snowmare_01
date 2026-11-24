# CodexTactics — Technical Debt & Project Health Report

> **Status**: 🟡 **WARNING** | **Updated**: 2026-11-24T07:05:48Z | **HEAD**: `ba8c544c`

## 1. Executive Summary

| Metric | Current Value | Threshold / SLA | Status |
|:---|:---:|:---:|:---:|
| **Known Fails** | 2 (expired: 0) | 0 expired (> 7 days) | 🟢 OK |
| **Automation Unit Tests** | 420 passed / 420 total | 100% pass | 🟢 PASS |
| **Active Smokes (Registry)** | 40 pass, 1 known-fail, 0 fail | 41 active | 🟢 PASS |
| **Disabled/Archived Smokes** | 277 this week | Tracking | ℹ️ |
| **TODO / Debt Markers** | 3 total (stable) | Controlled growth | ℹ️ |
| **Last Build Duration** | N/A | Fast iteration | ℹ️ |
| **Last Merge Gate** | 🟢 PASS | All green | 🟢 |

## 2. Known Fails & 7-Day Expiry Tracking

| Name | Type | Reason / Tracking | Owner | First Seen | Days Active | SLA Status |
|:---|:---|:---|:---|:---:|:---:|:---:|
| `CodexTactics.LevelGen.MissionGoals.CommittedFiles` | unit_test | WIP input data: Content/Data/LevelGen/objectives/L_Gen_Depot_1_X6.objectives.json | designer | 2026-11-23 | 1 | 🟢 Active (6d left) |
| `CodexTactics.LevelGen.Props.Power.GeneratedMaps` | unit_test | WIP input data: Content/Data/LevelGen/props/L_Gen_Depot_1_X6.props.json | designer | 2026-11-23 | 1 | 🟢 Active (6d left) |

## 3. Smokes & Tests Disabled This Week

Total archived/disabled checks: **277**.

| Check Name | Map Context | Status | Retired In Commit |
|:---|:---|:---|:---:|
| `ClimateSmoke` | `/Game/Environment/Weather/Test/L_WeatherRainTest` | archived_with_map | `0ea16841` |
| `RainSmoke` | `/Game/Environment/Weather/Test/L_WeatherRainTest` | archived_with_map | `0ea16841` |
| `LevelGenBuildingsWalkSmoke` | `/Game/Maps/Generated/L_Gen_BgenPlans` | archived_with_map | `0ea16841` |
| `CameraMinZoomDepotSmoke` | `/Game/Maps/Generated/L_Gen_Depot_1` | archived_with_map | `0ea16841` |
| `ClickPreviewSmoke` | `/Game/Maps/Generated/L_Gen_Depot_1` | archived_with_map | `0ea16841` |
| `ConePerfSmoke` | `/Game/Maps/Generated/L_Gen_Depot_1` | archived_with_map | `0ea16841` |
| `CorpseCostSmoke` | `/Game/Maps/Generated/L_Gen_Depot_1` | archived_with_map | `0ea16841` |
| `CorpsePersistSmoke` | `/Game/Maps/Generated/L_Gen_Depot_1` | archived_with_map | `0ea16841` |
| `DepotCampaignStartSmoke` | `/Game/Maps/Generated/L_Gen_Depot_1` | archived_with_map | `0ea16841` |
| `DepotVaultSmoke` | `/Game/Maps/Generated/L_Gen_Depot_1` | archived_with_map | `0ea16841` |
| `DoorBreachSmoke` | `/Game/Maps/Generated/L_Gen_Depot_1` | archived_with_map | `0ea16841` |
| `DropSoftlockSmoke` | `/Game/Maps/Generated/L_Gen_Depot_1` | archived_with_map | `0ea16841` |
| `FallDropSmoke` | `/Game/Maps/Generated/L_Gen_Depot_1` | archived_with_map | `0ea16841` |
| `FormationMantleSmoke` | `/Game/Maps/Generated/L_Gen_Depot_1` | archived_with_map | `0ea16841` |
| `HitchProbeSmoke` | `/Game/Maps/Generated/L_Gen_Depot_1` | archived_with_map | `0ea16841` |
| ... | _and 262 more archived checks_ | | |

## 4. Code Debt Markers (TODO / FIXME / HACK / TEMP)

Total markers found in tracked codebase: **3**

| Directory / Module | Marker Count | Share |
|:---|:---:|:---:|
| `Source/CodexTactics` | 0 | 0.0 % |
| `Source/CodexTacticsDev` | 0 | 0.0 % |
| `Source/CodexTacticsTests` | 0 | 0.0 % |
| `Scripts` | 3 | 100.0 % |
| `Content/Data` | 0 | 0.0 % |
| `Other` | 0 | 0.0 % |

<details><summary>Recent debt marker samples (click to expand)</summary>

- `Scripts/Editor/setup_indoor_cover_animation.py:431`: TODO(indoor-cover): not reliable yet - the two bodies disagree on CrouchL (0.65 / 0.78 s) and StandR (1.38 / 0.72 s): th
- `Scripts/Editor/setup_indoor_cover_animation.py:548`: TODO(indoor-cover): add a C++ editor helper UOperativeAnimGraphLibrary::FillAimOffsetSamples(UBlendSpace*, const TArray<
- `Scripts/Editor/setup_indoor_cover_animation.py:578`: return "cross", f"{result[-1] if isinstance(result, tuple) else result}; used {used}; corners left out (TODO helper): {d
</details>

## 5. Build Duration Trend & Merge Gate Runs

### Recent Builds

_No recent build logs found._

### Merge Gate Runs

_No merge-gate run logs found._
