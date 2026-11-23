# Gemini 100 Plan — Diagnosis of 6 Failing X4 Stairs (No Gameplay Fix)

## 1. Goal & Boundaries
- **Objective**: Thoroughly diagnose why 6 out of 19 stairs fail during `PmcTraversalSmoke` on `/Game/Maps/Generated/L_Gen_Depot_X4` across all 4 passes (PMC solo, PMC formation, Team 0 solo, Team 0 formation).
- **Scope**: DIAGNOSIS ONLY. No gameplay, movement, AI, navmesh, or levelgen code modifications.
- **Allowed Paths**:
  - `Source/CodexTacticsDev/Private/Debug/PmcTraversalSmokeCommand.cpp` (diagnostic log lines only if needed)
  - `docs/port/gemini/outbox/100-*`
- **Output**: Detailed diagnostic report in Russian `docs/port/gemini/outbox/100-x4-stairs-diagnosis.report.md` with log evidence, classification, comparison against passing stairs, and proposed minimal fixes.

## 2. Failing Stairs & Baseline Data (from 095 log `Saved/Logs/Worker/20261123-001643/Smoke-PmcTraversalSmoke.log`)
1. `B_hall_n_stair_0` (B_hall_n, 0->3, H=900cm):
   - Link: `(5550, -3130, 0) -> (5550, -1843, 900)`
   - Start: `(5550, -3230, 0)`, Goal: `(5550, -1643, 910)` (Inward = (0, 1))
   - Result: `stuck (at link start), no march seen`. Feet at `(5550, -3192, 0)` (62 cm from link start).
2. `STAIR_in_8` (B_ruin, 0->1, H=300cm):
   - Link: `(-2200, 1350, 0) -> (-2629, 1350, 300)`
   - Start: `(-2100, 1350, 0)`, Goal: `(-2829, 1350, 310)` (Inward = (-1, 0))
   - Result: `timed out (walking), no march seen [budget 23 s]`. Feet at `(-2707, 1675, 359)` (~3.3m from goal, upper storey).
3. `B_x1_8_t7_block_stair_0` (B_x1_8_t7_block, 0->1, H=300cm):
   - Link: `(-7500, -7200, 0) -> (-7500, -6771, 300)`
   - Start: `(-7500, -7300, 0)`, Goal: `(-7500, -6571, 310)` (Inward = (0, 1))
   - Result: `stuck (walking), no march seen`. Feet at `(-7509, -6686, 2)` (ground level under goal).
4. `B_x1_16_t7_block_stair_0` (B_x1_16_t7_block, 0->1, H=300cm):
   - Link: `(-4700, -11830, 0) -> (-4700, -11401, 300)`
   - Start: `(-4700, -11930, 0)`, Goal: `(-4700, -11201, 310)` (Inward = (0, 1))
   - Result: `stuck (walking), no march seen`. Feet at `(-4641, -11225, 2)` (ground level under goal).
5. `B_x2_4_t8_hall_stair_0` (B_x2_4_t8_hall, 0->3, H=900cm):
   - Link: `(11978, -6270, 0) -> (10691, -6270, 900)`
   - Start: `(12078, -6270, 0)`, Goal: `(10491, -6270, 910)` (Inward = (-1, 0))
   - Result: `timed out (walking), no march seen [budget 33 s]`. Feet at `(10400, -6037, 902)` (~2.5m from goal, upper storey).
6. `B_x2_25_t8_block_stair_0` (B_x2_25_t8_block, 0->1, H=300cm):
   - Link: `(-7400, -16248, 0) -> (-7400, -15819, 300)`
   - Start: `(-7400, -16348, 0)`, Goal: `(-7400, -15619, 310)` (Inward = (0, 1))
   - Result: `stuck (walking), no march seen`. Feet at `(-7401, -15623, 2)` (ground level under goal).

## 3. Investigation Steps
1. **Analyze PmcTraversalSmokeCommand Logic**:
   - Inspect start and goal placement (`PmcTraversalSmokeCommand.cpp:462-466`).
   - Understand `StairInwardCm` (default 200 cm) and how it projects past `top`.
   - Inspect movement command dispatch, path following, arrival condition, and stuck detection.
2. **Inspect Building Layouts in JSON**:
   - Read `Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x4.json` for all 6 buildings.
   - Check bounding boxes, floor cutouts, slab positions, walls, openings/doors around `pos` and `top`.
   - Inspect why flight 0 fails while flight 1 (`_stair_1`) passes for the 3 block buildings (`B_x1_8_t7_block`, `B_x1_16_t7_block`, `B_x2_25_t8_block`).
   - Compare `B_hall_n_stair_0` and `B_x2_4_t8_hall_stair_0` against passing hall stairs (`B_x1_19_t7_hall_stair_0`, `B_x2_16_t8_hall_stair_0`).
   - Check `STAIR_in_8` in `B_ruin` and compare against `STAIR_ext_7`.
3. **Classify Each Failure**:
   - Class (a): Smoke goal artefact (e.g. goal placed inside a wall, beyond a railing, or off-navmesh).
   - Class (b): Navmesh / nav link gap (e.g. nav link disconnected, ground 2D partial path chosen because goal projection falls on ground navmesh).
   - Class (c): Layout data error (e.g. inverted direction, wrong coordinates, doorway blocking).
   - Class (d): Other.
4. **Propose Minimal Fixes**:
   - Detail where and what should be fixed (e.g. in levelgen layout generator, smoke goal positioning, or nav link generation) without editing gameplay code.
5. **Quality Gate & Report**:
   - Run `Scripts/Tools/gemini_gate.ps1 -Task 100 -SonnetReview`.
   - Ensure clean verification and no rule regressions.
