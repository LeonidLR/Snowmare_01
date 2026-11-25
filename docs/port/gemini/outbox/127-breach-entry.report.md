# Task 127 — Breach Entry Regression Report

- **Branch / Worktree**: `gemini/127-breach-entry` at `C:/Work/Lanes/CodexTactics-127` (base `69ed0f28d`, not pushed)
- **Model / Review**: Hydrogen (`self-review only` — Neon model switch unavailable in session)

## 1. Smoke Reproduction & Bisect Findings

1. **Missing role assertions in `DoorBreachSmoke`**:
   - Baseline `DoorBreachSmoke` on `69ed0f28d` passed (`42.9s`, `L_Gen_StoryShowcase_Kit`, `S01_open_f0_d_gable_u0`) only because it checked `Out < -40.f` (`Inside >= Expected`), not distinct corner/middle destinations, separation, or entry order.
   - Added `c2: entry roles within 1 m, distinct, in order` + `at bCleared` snapshot logging to `Source/CodexTacticsDev/Private/Debug/DoorBreachSmokeCommand.cpp`.
   - Re-running on unpatched code reproduced the regression (`FAIL 57.9s`):
     - At `bCleared`: `Medic-Sapper (2540.88, -6551.79)` (`46 cm` from right corner `(2495, -6550)`), `Engineer (2772.39, -6563.95)` (`74 cm` from left corner `(2845, -6550)`), `Commander (2669.75, -6474.29)` (`24 cm` from middle `(2670, -6450)`).
     - `0.3s` later (after `EndBreach("the room is clear")` restored `SetFollowersHolding(false)`): `SquadSubsystem::TickFormationFollow` + `ApplyFormationConfined` (introduced in `386add9b7`, `feat(formation): confined entry order`) immediately pulled `Engineer` off his left corner back toward Commander/doorway to `out -90 lateral 39` (`149 cm err`), failing `c2`.

2. **Additional causes inspected in `386add9b7..69ed0f28d` & designer X6 demos**:
   - `CodexTacticsPlayerControllerBreach.cpp`: `OrderBreach` passed `Leader` instead of `nullptr` to `Squad->HandOverLeadForOrder(...)`, leaving an armed `PendingLeader` active so `UpdatePendingLeader()` swapped leaders ~2s after stacking and cancelled the breach (`EndBreach("the leader / mode changed")`), causing the room-entry click to execute `OrderGroupMove` (`2026-11-25_L_Gen_Depot_1_X6_{100842,120852,153205}.jsonl`).
   - `MeasureRoom` (`CodexTacticsPlayerControllerBreach.cpp`): `ECC_Visibility` trace ignored only `APawn`, not `ADoorActor`, letting openable door leaves (`b072ce958` / `b2b5e513`) shrink measured room bounds and trigger false `1b79de7ed` closet filing or `IsInsideRoom` failures. (`1b79de7ed` closet-filing unit tests `Entry.RoomClamp` and `Entry.ClosetFile` are preserved.)

## 2. Changes

- [SquadOrderRules.h](file:///C:/Work/Lanes/CodexTactics-127/Source/CodexTactics/Public/Characters/SquadOrderRules.h) & [SquadOrderRules.cpp](file:///C:/Work/Lanes/CodexTactics-127/Source/CodexTactics/Private/Characters/SquadOrderRules.cpp): Added pure helper `ShouldHoldClearedBreachSpots(bCleared, bSoloMode, bFollowersHoldingBefore)`.
- [SquadSubsystem.h](file:///C:/Work/Lanes/CodexTactics-127/Source/CodexTactics/Public/Characters/SquadSubsystem.h) & [SquadSubsystem.cpp](file:///C:/Work/Lanes/CodexTactics-127/Source/CodexTactics/Private/Characters/SquadSubsystem.cpp): Added `ArmPostActionHold` / `ClearPostActionHold` (reusing `bPostFightHold` + `KeepsPostFightHold`, keeping `AreFollowersHolding()` unchanged while holding followers at their cleared spots until the leader moves or regroups).
- [CodexTacticsPlayerControllerBreach.cpp](file:///C:/Work/Lanes/CodexTactics-127/Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerBreach.cpp):
  - `MeasureRoom` now ignores `ADoorActor` alongside `APawn`.
  - `OrderBreach` calls `Squad->ClearPostActionHold()` and `Squad->HandOverLeadForOrder(nullptr, ...)` (updating `Leader` to `Squad->GetLeader()` if handed over).
  - `EndBreach` arms `Squad->ArmPostActionHold` when `ShouldHoldClearedBreachSpots(...)` is true.
- [SquadOrderRulesTest.cpp](file:///C:/Work/Lanes/CodexTactics-127/Source/CodexTacticsTests/Private/Characters/SquadOrderRulesTest.cpp): Added `ShouldHoldClearedBreachSpots` assertions to `PostFightHold`.
- [DoorBreachSmokeCommand.cpp](file:///C:/Work/Lanes/CodexTactics-127/Source/CodexTacticsDev/Private/Debug/DoorBreachSmokeCommand.cpp): Added `c2` entry role/distance (`<= 100 cm`), pairwise separation (`>= 100 cm`), and start-order (`#1 -> #2 -> #3`) assertions.

## 3. Verification (All Run After Final Edit)

- **Build (`machine_dispatch.py build`)**: `Result: Succeeded` (749.49s, exit 0).
- **CheckMap (`Scripts/test.ps1 -CheckMap`)**: `Impact map OK: every smoke command is registered and mapped.`
- **Unit tests (`Scripts/test.ps1 -Filter "CodexTactics.Tactics.DoorwayBreach+CodexTactics.Squad.Orders+CodexTactics.Formation.Confined" -SkipBuild -SkipSmokes`)**: **43 tests, 0 failed**.
- **Smokes (`machine_dispatch.py smoke --jobs 1`)**:
  - `DoorBreachSmoke`: **PASS** (`39.6s`; `c: 3/3 inside`, `c2: R(Medic-Sapper 42 cm err), L(Engineer 73 cm err), C(Commander 24 cm err), minSep 136 cm, orderOk 1`, `d: Solo PASS`).
  - `InteriorExitSmoke`: **PASS** (`36.3s`).
  - `DoorsSmoke`: **FAIL** (`37.6s`) — 3 pre-existing missing door-ID checks (`S09_broken_back_windows_f0_d19`/`d20`, `S04_rubble_escape_f0_d19` renamed to `d_court`/`d_gable_uL` in `gen_story_showcase.json`).
  - `CornerEntrySmoke`, `FlankBreachSmoke`: skipped/blocked by `Scripts/smoke_parallel.ps1` as archived-map smokes (`L_MovementTest`, requires explicit `-ForceMap`).
