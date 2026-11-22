# Plan 096 - Three Lab Layouts (Crossroads, Alley, Bridge) - T-02

## 1. Goal & Context
- **Task**: 096 - Three small lab layouts that reproduce the designer's screenshots (smart enemies T-02, layouts part).
- **Spec**: `docs/design/gdd/smart-enemies-roadmap.md:171-193` (Task T-02) and `docs/port/gemini/inbox/096-lab-layouts.md`.
- **Division of Responsibility**:
  - Gemini creates the 3 accepted layouts (`gen_lab_*.json` + `.meta.json`), generates all data exports (`tactical/`, `objectives/`, `props/`, `LevelJson/`), and adds the Vitest test in `Tools/TrainingGround/src/levelgen/labLayouts.test.ts`.
  - Claude builds the Unreal maps (`L_Lab_*.umap`) with `Scripts/Editor/build_levelgen_map.py` after merge. Gemini never runs the map builder or opens the editor.

---

## 2. Layout Specifications & Exact Numbers (T-02 Reference)

### 2.1 L_Lab_Crossroads (`gen_lab_crossroads.json`)
*Reference: `smart-enemies-roadmap.md:174-176`*
> "an open crossroads, >= 4 cover objects (crates / blocks) on its far side, a walled enclosure with one gap on the near side, attacker start 60-80 m away, a goal beyond the crossroads; Each map <= 80 x 80 m"

- **Map Bounds**: `min: [-3500, -3500]`, `max: [3500, 3500]` (70 x 70 m, <= 80 x 80 m).
- **Attacker Start**: `[-2500, 0]`, **Goal**: `[+4500, 0]`.
  - Direct distance = 70.0 m (meets the 60-80 m requirement).
- **Crossroads**: Center `[0, 0]`. Intersecting open streets of width 10 m:
  - Main street along X axis: from X = -2500 to +4500, width 10 m (Y in [-500, 500]).
  - Cross street along Y axis: from Y = -3500 to +3500, width 10 m (X in [-500, 500]).
- **Walled Enclosure (Near Side)**:
  - Located on approach side: X in [-2200, -800], Y in [-1200, 1200].
  - Height 300 cm (wall thickness 25 cm).
  - Exactly ONE gap (width 150 cm) on east wall facing the crossroads at `[-800, 0]`.
- **Far Side Cover Objects**:
  - 5 cover objects (>= 4 required):
    1. `cov_cross_1`: crate at `[1200, -300]`, height 100 cm (low mantle).
    2. `cov_cross_2`: crate at `[1200, 300]`, height 100 cm (low mantle).
    3. `cov_cross_3`: sandbag at `[1500, -600]`, height 100 cm.
    4. `cov_cross_4`: sandbag at `[1500, 600]`, height 100 cm.
    5. `cov_cross_5`: jersey at `[1800, 0]`, height 100 cm.

### 2.2 L_Lab_Alley (`gen_lab_alley.json`)
*Reference: `smart-enemies-roadmap.md:176-178`*
> "a lane 3-5 m wide along a one-storey building, a 2-storey building with a walkable roof, a parapet and a ladder of >= 7 rungs at the lane's mouth, a way round the back of the low building, the goal at the mouth; Each map <= 80 x 80 m"

- **Map Bounds**: `min: [-3000, -2500]`, `max: [3000, 2500]` (60 x 50 m, <= 80 x 80 m).
- **Attacker Start**: `[-2500, 0]`, **Goal**: `[+1500, 0]` at the lane mouth.
- **Lane**: Width 400 cm (4.0 m, meets 3-5 m range), running along X from X = -2500 to X = 1500, Y in [-200, 200].
- **One-Storey Building (`B_low`)**:
  - Footprint: X in [-1500, 1200], Y in [200, 1600], height 300 cm.
- **Way Round the Back**:
  - Flanking route north of `B_low`: path through Y in [1800, 2200] connecting X = -2000 to X = 1400.
- **Two-Storey Building (`B_two_storey`)**:
  - Located at the lane mouth: X in [1500, 2800], Y in [-1200, 1200], height 600 cm (2 storeys).
  - Walkable roof at Z = 600 cm with 100 cm parapet (`RoofParapet`, top Z = 700 cm).
  - Ladder: attached at `[1500, 100]`, 8 rungs (>= 7 required), providing ground-to-roof access.

### 2.3 L_Lab_Bridge (`gen_lab_bridge.json`)
*Reference: `smart-enemies-roadmap.md:178-180`*
> "a crossing over a cut (a drop on both sides within the drop rule), two cover objects at its near end, a climbable obstacle of a MANTLING-table height beside its far end, a building with windows onto the deck. Each map <= 80 x 80 m"

- **Map Bounds**: `min: [-3000, -2500]`, `max: [3000, 2500]` (60 x 50 m, <= 80 x 80 m).
- **Attacker Start**: `[-2500, 0]`, **Goal**: `[+2500, 0]`.
- **Cut / Drop**:
  - Trench running north-south: X in [-300, 300] (width 600 cm = 6.0 m), depth 200 cm (Z = -200 cm, within drop rule).
- **Bridge Deck Crossing**:
  - Span across cut: X in [-300, 300], Y in [-200, 200] (width 4.0 m, Z = 0).
- **Cover at Near End**:
  - Exactly 2 cover objects (crates, height 100 cm) at near end:
    1. `cov_bridge_near_1`: `[-450, -100]`.
    2. `cov_bridge_near_2`: `[-450, 100]`.
- **Climbable Obstacle at Far End**:
  - Obstacle of MANTLING-table height (100 cm, low mantle) at `[400, 250]` beside the bridge exit, with registered `mantle_edge`.
- **Building with Windows onto the Deck**:
  - Building `B_overlook`: X in [350, 1800], Y in [-1800, -250], height 300 cm.
  - Windows on north facade (Y = -250) facing directly onto the deck.

---

## 3. Implementation Plan & Deliverables

1. **`Tools/TrainingGround/src/levelgen/labLayouts.ts`**:
   - Implements `buildLabCrossroads()`, `buildLabAlley()`, `buildLabBridge()`.
   - Uses `makeEmptyDraft()`, `addBuilding()`, `addWall()`, `addCover()`, `addLadderAt()`, and `finalizeDraftToLayout()`.
   - Runs `validateGenLayout()` on each layout.
   - Exports `writeLabLayouts()` CLI/function to write files to `Content/Data/LevelGen/accepted/`.
2. **Export generation**:
   - Generate `Content/Data/LevelGen/tactical/L_Lab_*.json` via `generateTacticalMap()`.
   - Generate `Content/Data/LevelGen/objectives/L_Lab_*.json` via `generateMissionGoals()`.
   - Generate `Content/Data/LevelGen/props/L_Lab_*.json` via `autogenProps()`.
   - Write `Content/Data/LevelJson/L_Lab_*.json` configs linking `levelgen_layout`.
3. **Vitest Unit Test**:
   - `Tools/TrainingGround/src/levelgen/labLayouts.test.ts`:
     - Verifies sizes <= 80 x 80 m.
     - Verifies start-to-goal distance 60-80 m on Crossroads.
     - Verifies cover counts (>= 4 on Crossroads far side, 2 on Bridge near end).
     - Verifies ladder rungs >= 7 on Alley.
     - Verifies lane width 3-5 m on Alley.
     - Verifies climbable obstacle with mantle edge on Bridge.
     - Verifies `validateGenLayout()` has 0 errors for all 3 layouts.
4. **Report & Handover**:
   - `docs/port/gemini/outbox/096-lab-layouts.report.md`:
     - ASCII diagrams for each layout.
     - Command lines for Claude to build maps (`Scripts/Editor/build_levelgen_map.py -Layout ...`).
     - Test outputs from `npm test`.

---

## 4. Verification & Gate
- `npm test` in `Tools/TrainingGround` (all unit tests green).
- `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` (risk <= 0.2/4).
- `git diff --check` (0 whitespace errors).
