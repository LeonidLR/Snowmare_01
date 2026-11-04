# T11 Report: Depot x2 Generator Extension Mode

**Branch:** `gemini/t11-depot-x2-generator`  
**Area:** `Tools/TrainingGround/src/levelgen/` (TypeScript generator)  
**Deliverables:** Generator extend mode, candidate layouts, unit tests, no map rebuild (`.umap` untouched).

---

## 1. What Changed
- **`Tools/TrainingGround/src/levelgen/generate.ts`:**
  - Added `ExtendOptions` interface and `extendLayout(o: ExtendOptions): GenLayout` function.
  - Takes an accepted layout (`gen_ref_01_02_depot_1.json`), freezes all base elements byte-identically in coordinates and IDs.
  - Extends bounds from `[-6200, -8850]..[6200, 8850]` (124 m x 177 m, ~2.19 ha) to `[-6200, -8850]..[6200, 26550]` (124 m x 354 m, ~4.39 ha), doubling the area.
  - Walkable seam at $Y = 8850$:
    - Level -1 (cut trench floor): open and extends seamlessly eastward into the new trench corridor.
    - Level 0 (ground north of $X = 1333$, North Street): open across seam for infantry and squads.
  - Extended elements in $Y \in [8850, 26550]$:
    - Cut trench continuation with retaining walls (`CUTWALL_N_ext_*`, `CUTWALL_S_ext_*`), two crossings (`X_bridge_ext` at $Y \approx 16000$, `X_ramp_ext` at $Y \approx 22000$), and two south-wall trench ladders (`LAD_cut_ext_1`, `LAD_cut_ext_2`).
    - New set pieces: `B_warehouse_ext` (3 floors, roof +3, stairwell), `B_gallery_ext` (roof +1, ladder overwatch), `B_compound_ext` with `B_shed_ext` (fenced repair lot), `B_ruin_ext` (2 floors, external stairs to roof +2, partition doors), `B_pump_ext` with 2 silos (tank farm), `B_steelworks_ext` (3 floors, roof +3, stairwell), and `Z_deadend_ext` (enclosed dead-end alley).
    - Mandatory route `R_main`: begins with base prefix (spawn $\to$ compound $\to$ North street), crosses seam at $[1350, 8850]$, explores warehouse, crosses bridge to south side, traverses ruin (climbing external stair to roof +2), advances past pump house, along south street, and finishes in the dead-end alley.
    - 3 variants: `V1_ext_cut` (trench kill zone), `V2_ext_gallery` (roof overwatch), `V3_ext_warehouse` (warehouse roof).
    - Ambush points: 47 total spots across both sectors (12 concealed 14-30 m off route).
    - Edge spawn points and patrol loops added.
- **`Tools/TrainingGround/src/levelgen/node/files.ts`:**
  - Exported `CANDIDATES_DIR = path.join(ROOT, 'Content/Data/LevelGen/candidates')`.
- **`Tools/TrainingGround/src/levelgen/cli/levelgen.ts`:**
  - Added `--extend <name>` option to load an accepted layout from `Content/Data/LevelGen/accepted/` and generate extended candidate layouts. Defaults output to `Content/Data/LevelGen/candidates/` when extending.
  - Added preview PNG generation via `renderCandidatePng` (PIL).
- **`Content/Data/LevelGen/candidates/`:**
  - Generated candidates for seeds 1, 2, and 3:
    - `gen_ref_01_02_depot_x2_1.json` (+ `.eval.json`, `.png`)
    - `gen_ref_01_02_depot_x2_2.json` (+ `.eval.json`, `.png`)
    - `gen_ref_01_02_depot_x2_3.json` (+ `.eval.json`, `.png`)
    - `report.md` (summary report for all 3 candidates).
- **`Tools/TrainingGround/src/levelgen/levelgen.test.ts`:**
  - Added 4 test suites verifying:
    1. Base layout coordinates and objects are 100% byte-identical in the extended layout.
    2. All validators (`validateGenLayout`, `validateLayout` on ground layer, and `auditLayout`) pass with 0 errors.
    3. Mandatory bot route covers both old ($Y \le 8850$) and new ($Y > 8850$) parts and is completable.
    4. Determinism for identical seeds and variation between different seeds.
- **`docs/port/TASK_BOARD.md`:**
  - Marked T11 row as completed: `✅ gemini/t11-depot-x2-generator`.

---

## 2. How Tested & Results
1. **Candidate Generation:**
   `npm run levelgen -- --extend gen_ref_01_02_depot_1 --seed 1 --seeds 3`
   - Generated 3 candidates in `Content/Data/LevelGen/candidates/` in 57.8 s.
   - All 3 candidates scored ~0.793, routes and all 3 variants completely passable (COMPLETABLE).
   - Formations simulated: column 40 %, diamond 36 %, echelon_left 19 %, echelon_right 5 %.
   - Area: 4.39 ha (~2.0x base area of 2.19 ha).
2. **Unit Tests:**
   `node --test src/levelgen/levelgen.test.ts`
   - **22/22 PASS** (0 failed, duration 49.7 s).
3. **TypeScript Compilation:**
   `npm run typecheck`
   - **0 errors** (`tsc && tsc -p tsconfig.node.json` exited 0).
4. **Golden Check:**
   `npm run golden -- --check`
   - **0 regressions** (`golden files up to date`).
5. **Pre-push / Map safety:**
   - No `.umap` files modified.
   - `Scripts/levelgen_build_map.ps1` was NOT run.

---

## 3. Open Questions for Claude
- Candidate seed 1, 2, or 3 can be selected for final map merge with the user's hand edits.
- When building the map in Unreal, the builder will combine the base depot map actors with the new extended actors ($Y > 8850$).
