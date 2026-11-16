# Plan 076: Generated Maps Automatic Props Placement

- **Task:** 076 (`generated-map-props`)
- **Governing Spec:** `docs/port/gemini/inbox/076-generated-map-props.md`, `.gemini/task.md`
- **Base Commit:** `main` (branch `gemini/076-generated-props`)
- **Allowed Paths:**
  - `Tools/TrainingGround/src/levelgen/props/**`
  - `Tools/TrainingGround/src/departments/PropsPanel.tsx`
  - `Tools/TrainingGround/src/departments/LevelGenDepartment.tsx` (props toggle only)
  - `Tools/TrainingGround/server/levelgenApi.ts` (save hook only)
  - `Tools/TrainingGround/scripts/**`
  - `Content/Data/LevelGen/props_autogen.json`
  - `Content/Data/LevelGen/props/L_Gen_Depot_X4.props.json`
  - `Content/Data/LevelGen/props/L_Gen_Depot_1_X6.props.json`
  - `Content/Data/LevelGen/props/L_Gen_Test_X2.props.json`
  - `docs/port/gemini/outbox/076-generated-props.plan.md`
  - `docs/port/gemini/outbox/076-generated-props.report.md`

---

## 1. Diagnosis & Root Cause

1. **Why Generated Maps Lack Props:**
   - In `Source/CodexTactics/Private/LevelGen/CodexLevelPropsSubsystem.cpp:100`, runtime props loading calls:
     `GetDefaultPropsPathForMap(MapName) = Content/Data/LevelGen/props/<MapName>.props.json`.
   - In `Content/Data/LevelGen/props/`, only `L_Gen_Depot_1.props.json` existed.
   - For generated and expanded maps (`L_Gen_Depot_X4`, `L_Gen_Depot_1_X6`, `L_Gen_Test_X2`), no props files were authored or generated, leaving the maps devoid of barrels, crates, mines, turrets, and loot.
2. **Current Layers Integration:**
   - Layers 7, 8, 9 (mines, loot, special items) and obstacle layers 4, 5, 6 already render and edit props via `LayersPanel` and `LayersOverlay` when a props file is loaded.
   - What is missing is the deterministic automatic generation pass to create, scale, and save props files for generated, expanded, and re-rolled maps.

---

## 2. Density Model & Reference Scaling (`props_autogen.json`)

1. **Depot Reference Baseline:**
   - Map: `L_Gen_Depot_1` (`gen_ref_01_02_depot_1.json`).
   - Bounds: `[-6200, -8850]` to `[6200, 8850]` -> 124 m x 177 m = 21,948 m² (~2.195 units of 10,000 m²).
   - Baseline mix on Depot:
     - **Crates:** 37 placed (density mode 0.25 / 100 m² eligible area, ~16.8 / 10,000 m² total).
     - **Barrels:** 8 placed (all explosive clusters, ~3.64 / 10,000 m²).
     - **Turrets:** 2 placed (~0.91 / 10,000 m², cap 12).
     - **Mines:** 25% doors, 10% crates, 4 free field mines in the cut (~1.82 / 10,000 m²).
     - **Loot:** 600 total budget (~273 / 10,000 m²).
2. **Proportional Area Scaling for Expanded Maps:**
   - Area Factor $F = \text{Area}_{\text{map}} / 21948\text{ m}^2$:
     - $X2$ (~43,750 m², $F \approx 2.0$): 16 barrels, 4 turrets, 8 field mines, 1200 loot budget.
     - $X4$ (~87,792 m², $F \approx 4.0$): 32 barrels, 8 turrets, 16 field mines, 2400 loot budget.
     - $X6$ (~131,936 m², $F \approx 6.0$): 48 barrels, 12 turrets (cap), 24 field mines, 3600 loot budget.
   - All scaling parameters and caps will be stored in `Content/Data/LevelGen/props_autogen.json`.

---

## 3. Architecture & Key Rules

1. **Deterministic Autogen (`autogen.ts`):**
   - Pure function `generatePropsForLayout(options)`.
   - Seeded from `layout.seed ?? 1`.
   - Respects validator and fast-gate keep-outs (walls, non-enterable buildings, doorway corridors, ladder tops/feet, route corridors, spawn and goals).
2. **Expanded Map Preservation Rule:**
   - For an expanded map derived from a base layout/props file (e.g. `L_Gen_Depot_1.props.json`), all props located within the unchanged original area bounds are retained and marked `locked: true`.
   - `placement.ts:makeInput` automatically collects them into `input.kept`.
   - `rerollAll` places only the delta props required to reach the target density in new areas.
3. **Protection & Non-Overwrite Rule:**
   - Generated files have `"source": "auto"`.
   - If a file exists without `"source": "auto"`, overwriting is strictly refused (protecting authored files like `L_Gen_Depot_1.props.json`).
4. **Target Map Name Resolution:**
   - Maps matching an existing `Content/Data/LevelJson/<MapName>.json` (via `levelgen_layout`) resolve to that exact `<MapName>` (`L_Gen_Depot_X4`, `L_Gen_Depot_1_X6`, `L_Gen_Test_X2`).
   - Otherwise, resolves via `mapNameFor(layoutName)`.

---

## 4. Implementation Steps & 45-Minute Priority

- **Step 1:** Create `Content/Data/LevelGen/props_autogen.json` with configuration and density parameters.
- **Step 2:** Implement `Tools/TrainingGround/src/levelgen/props/autogen.ts` and enhance `layoutView.ts` to support objectives/goals keep-outs.
- **Step 3:** Add unit tests in `Tools/TrainingGround/src/levelgen/props/autogen.test.ts` (determinism, fast gate, expansion preservation, non-overwrite).
- **Step 4:** Implement `Tools/TrainingGround/scripts/autogen_props.ts` and generate the 3 required props files:
  - `Content/Data/LevelGen/props/L_Gen_Depot_X4.props.json`
  - `Content/Data/LevelGen/props/L_Gen_Depot_1_X6.props.json`
  - `Content/Data/LevelGen/props/L_Gen_Test_X2.props.json`
- **Step 5:** Add server hook in `Tools/TrainingGround/server/levelgenApi.ts` for `/api/levelgen/accept`.
- **Step 6:** Run full TG test suite and TypeScript check.
- **Step 7:** Compile report `076-generated-props.report.md`, commit, and send chat FINISH message.
