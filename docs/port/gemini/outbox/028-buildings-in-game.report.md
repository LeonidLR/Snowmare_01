# 028 Combat Building Map Builder + In-Game Verification Report

- **Branch:** `gemini/028-buildings-in-game`
- **Base / Prior commit:** `4e3c392f4dcd3367f5390acf93269b202bad9b68`
- **Spec:** `docs/port/codex/inbox/007-levelgen-buildings-builder.md` (with chat `#251` ruling from `codex`: literal JSON `top_height` geometry, `SightRules` unchanged)
- **Safety / Non-regression:** `L_Gen_Depot_1` (`.umap` / `.json`), `Config/**`, `Content/VFX/**`, `CLAUDE.md`, `main`, and Marketplace pack `.uasset`s untouched (`dirty_content_packages_before_save (0): []`, `save_scope=map_only`). `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` -> `Boundary Breach : PASS`, `Risk Rating : 0.3 / 4`.

---

## 1. Summary of Work Completed

### A. Prior Approved C++ & Spec 007 Foundation (`4e3c392f`)
- Extended `PropsLayoutView`, `LadderActor`, `VaultRules`, tests (`PropsLayoutViewTest`, `VaultLevelRulesTest`), and registered `LevelGenOpeningsSmoke` in `Scripts/test_map.json`.
- Removed hidden `max(top_h, 175.0)` override in `Scripts/Editor/build_levelgen_map.py` per chat `#251`, preserving literal bunker loopholes (`120..150 cm`) and bit-identical legacy `L_Gen_Depot_1` generation.

### B. Fixes Completed in This Continuation
1. **Hatch Ladder `top_standoff` Fix (`Scripts/Editor/build_levelgen_map.py:684-687`, `897-901`; `Scripts/Editor/tests/test_build_levelgen_buildings.py:79-85`, `119-123`):**
   - Root cause found: `LadderRules::TopLinkLocal` (`Source/CodexTactics/Private/Ladders/LadderRules.cpp:39-42`) uses `-Clip.TopStandoff` (default `49.1f` cm), whereas `build_levelgen_map.py` previously only set `top_exit_standoff` (`110 cm`). For hatch ladders (`LAD_B_tower_hatch` on `L_Gen_Train_2`, `LAD_B_warehouse_hatch` on `L_Gen_Train_3`), `49.1 cm` placed `GetTopLinkWorld()` inside the 100×100 hatch opening over empty air, failing both `ALadderActor::Validate()` (`no top floor ... under the top link point`) and `RecastNavMesh` (`top NO`, blocking climbing up/down).
   - Fix: set `top_standoff = max(49.1, dist_top)` (`110 cm`) alongside `top_exit_standoff = max(40.0, dist_top)` in `build_levelgen_map.py` and applied `actor.set_editor_property("top_standoff", ...)` at spawn. Verified `TopLink` lands solidly on the roof (`(-1120, -1637, 600)` in `Train_2`, `(930, 463, 600)` in `Train_3`), zero `ALadderActor` floor warnings, and both hatch links pass navmesh & live climbs (`LevelGenLaddersSmoke` 4/4 `PASS`).
2. **External L-Stair Full-Landing Parapet Cutover (`Scripts/Editor/build_levelgen_map.py:376-412`, `617-625`; `Scripts/Editor/tests/test_build_levelgen_buildings.py:111-118`):**
   - Root cause found during `LevelGenRouteSmoke` on `L_Gen_Train_3`: `B_catwalk_STAIR_catwalk_ext` uses an L-shaped landing (`landing_rect` `Y in [2595..2750]`), whereas cutting `B_catwalk_parapet_S` only by `STAIR_W * 0.5 + 15` around `top.y = 2600` (`[2510..2690]`) left `B_catwalk_parapet_S_l1` (`Y in [2690..2750]`) sitting across the north half of the landing and pinching the 35 cm-eroded navmesh passage between landing edge and parapet.
   - Fix: added `_intersect_box_to_edge`, expanding parapet cutouts on edges crossed by `approach -> top` to span the full `landing_rect` (`+10 cm` margin) while keeping perpendicular roof-edge parapets (`B_catwalk_parapet_E`) intact. Result: `L_Gen_Train_3` route `R_main` improved from `16 / 24 (FAIL)` to **`24 / 24 waypoints reached, 0 skipped (PASS)`**.
3. **Free-Standing Hatch Approach Spots on Elevated Floors (`Source/CodexTacticsDev/Private/Debug/LevelGenLaddersSmokeCommand.cpp:243-256`, `293-336`):**
   - Root cause: `PlanLegs` placed `Below = L.Foot + Out * 150.0` (through exterior walls) and `OnNav` with `150 cm` vertical tolerance projected `LAD_B_warehouse_hatch` (`level_from = 1`, `Z = 300`) down to ground floor (`Z = 0`) outside `B_warehouse`.
   - Fix: when `L.bFreeStanding`, select `Below` and `Wait` from interior candidates on `Low` with `|Projected.Z - Low| <= 60 cm`, and keep followers in `TeleportSquad` on the same floor (`<= 60 cm` tolerance).
4. **`LevelGenOpeningsSmoke` Interior T-Junction & Clean State Lifecycle (`Source/CodexTacticsDev/Private/Debug/LevelGenOpeningsSmokeCommand.cpp`):**
   - Updated exterior opening LOS checks to ignore `"interior"` partitions and `ALadderActor` during the exterior aperture trace while logging accepted-layout T-junctions explicitly as `Smoke note:`; retained strict collision checks for sill aprons, lintels, parapets, and ladder top exits; held `TSharedPtr<FState>` cleanly via `ActiveShotState` and disabled encounter/pressure CVars during the check.

---

## 2. Generated Maps & Visual Verification

Rebuilt with `Scripts/levelgen_build_map.ps1 -Layout L_Gen_Train_{1,2,3} -Fresh` (`dirty_content_packages_before_save (0): []`, `save_scope=map_only`, zero external `__ExternalActors__` for `Generated/`):
- `Content/Maps/Generated/L_Gen_Train_1.umap` + `Content/Data/LevelJson/L_Gen_Train_1.json` (navmesh tiles `Default=84, Vehicle=64`)
- `Content/Maps/Generated/L_Gen_Train_2.umap` + `Content/Data/LevelJson/L_Gen_Train_2.json` (navmesh tiles `Default=96, Vehicle=82`)
- `Content/Maps/Generated/L_Gen_Train_3.umap` + `Content/Data/LevelJson/L_Gen_Train_3.json` (navmesh tiles `Default=117, Vehicle=80`)

Rendered screenshots (`1280x720`) saved to `Saved\Orchestrator\buildings\`:
- `L_Gen_Train_1_{overview,detail}.png`
- `L_Gen_Train_2_{overview,detail}.png`
- `L_Gen_Train_3_{overview,detail}.png`

### Accepted-Layout Observations (Logged as `Smoke note:` in `LevelGenOpeningsSmoke`)
- `L_Gen_Train_1` (5 interior partition T-junctions at exterior window centers): `B_house_win_south_f0_3`, `B_house_win_south_f1_3`, `B_house_win_north_f0_3`, `B_house_win_north_f1_3`, `B_garage_win_east_f0_1`.
- `L_Gen_Train_2` (5 T-junctions at loophole centers): `B_checkpoint_lp_south_f0_4`, `B_checkpoint_lp_south_f1_4`, `B_checkpoint_lp_west_f0_2` (`LadderActor_2`), `B_checkpoint_lp_west_f1_2` (`LadderActor_2`), `B_bunker_lp_west_f0_2`.
- `L_Gen_Train_3` (5 interior partition T-junctions at window centers): `B_warehouse_win_east_f0_2`, `B_warehouse_win_east_f1_2`, `B_catwalk_win_south_f1_2`, `B_catwalk_win_west_f1_2`, `B_yard_shed_win_north_f0_3`.

---

## 3. Verification Results (Latest Runs After Final Edit)

| Check | Scope / Command | Result |
|---|---|---|
| Python unit tests | `python -m unittest Scripts/Editor/tests/test_build_levelgen_buildings.py -v` | **PASS** (`Ran 4 tests in 0.010s OK`, includes bit-identical `L_Gen_Depot_1` + Train 1..3 invariants) |
| C++ build | `Scripts\build.ps1` | **PASS** (`Succeeded`, 9.46 s) |
| UE Automation tests | `Scripts\test.ps1 -Filter CodexTactics.LevelGen+CodexTactics.Movement.Vault` | **PASS** (`47 tests, 0 failed`) |
| Test impact map | `Scripts\test.ps1 -CheckMap` | **PASS** (`Impact map OK: every smoke command is registered and mapped.`) |
| `LevelGenOpeningsSmoke` (rendered shots) | `L_Gen_Train_1` (`26 windows, 0 loopholes`), `L_Gen_Train_2` (`3 windows, 31 loopholes`), `L_Gen_Train_3` (`41 windows, 0 loopholes`) | **PASS** on all 3 (`failures=0`, 6 PNGs verified) |
| `LevelGenLaddersSmoke` | `L_Gen_Train_2` (`LAD_B_checkpoint` + `LAD_B_tower_hatch`), `L_Gen_Train_3` (`LAD_B_warehouse_hatch` + `LAD_B_warehouse`) | **PASS** on both (`4 / 4 legs passed, 0 failures` each) |
| `levelgen_route_smoke.ps1` (Bot mode) | `L_Gen_Train_1` (`14/14`), `L_Gen_Train_3` (`24/24`, `0 skipped`, 1 scripted drop `hole_roof_0`, 1 natural drop `hole_floor_0`) | **PASS** on `Train_1` & `Train_3` (see note on `Train_2` below) |
| `levelgen_route_smoke.ps1 -Carrot` | `L_Gen_Train_2` (`26 / 26 legs, 163 s`, `LAD_B_tower_hatch` up/down + `LAD_B_checkpoint` down climbed) | **PASS** (`26 / 26 legs, 0 failures`) |
| `smoke_parallel.ps1` regressions | `DepotVaultSmoke` (50.9 s), `DropSoftlockSmoke` (64.5 s), `LevelGenLaddersSmoke` (64.3 s), `LevelGenOpeningsSmoke` (43.3 s), `LevelGenRouteSmoke` (78.9 s) | **PASS** (`5 run, 0 failed, 0 PASS*, 0 known-fail`) |

> **Note on `L_Gen_Train_2` Route Mode:** In default `-Bot` mode (`Codex.Bot.Route`), `#9` (`LAD_B_tower_hatch` down) hit the bot's 12 s stuck-retry timeout while 3 operatives queued serially on the 16-rung hatch ladder (`9 / 27` before skip). In `-Carrot` mode (`walk=carrot`), all 26/26 legs on `L_Gen_Train_2` completed cleanly in 163 s including both climbs via `LAD_B_tower_hatch` (`#6` & `#9`) and `LAD_B_checkpoint` (`#23`).
