# Chokepoint & Kill Zone Analyzer Report (#49)

**Branch:** `gemini/chokepoint-analyzer`  
**Date:** 2026-11-07  
**Context:** `Tools/TrainingGround/src/levelgen/` (TypeScript / Vite / React)  
**Governing Documents:** `docs/design/KILL_ZONE_AMBUSH.md`, `docs/port/GEMINI_PLAN_2.md` §D, `docs/port/TASK_BOARD.md` (#49, #46a)

---

## 1. Overview & Purpose

As part of the PMC tactical behavior roadmap (#49 "Kill Zone Ambush" and #46a "Trap Planner"), the level generator requires an automated, deterministic spatial analyzer capable of identifying lethal chokepoints directly from layout geometry (`schema codex.levelgen_layout/1`).

The analyzer detects corridors **1–3 m wide** between structures **$\ge 3$ m tall**, evaluates and ranks ambush positions on both flanks (height, cover from corridor entrances, line of sight along the corridor), calculates composite kill zone ratings, and produces a 2D continuous danger heatmap for visual inspection and AI decision making.

---

## 2. Core Implementation

### A. Layout Geometry & Block Extraction (`chokepoints.ts`)
- **Block Aggregation (`extractTallBlocks`):** Scans all layout buildings and walls, filtering out low obstacles and preserving blocks with height $\ge 300$ cm (e.g. +3m floor 1, +6m floor 2, +9m industrial roofs, perimeter walls).
- **Corridor Detection (`findChokepointCorridors`):**
  - Iterates over all pairs of tall blocks.
  - Identifies facing spans separated by a clearance between 100 cm and 300 cm (1.0 m – 3.0 m).
  - Enforces minimum corridor length of 150 cm to discard accidental corner nooks.
  - Computes exact corridor centerline, endpoints, width, and bounding box along X and Y axes.

### B. Ambush Perch Scoring Model (`scoreAmbushPosition`)
For each detected corridor, the analyzer generates candidate perches along both flanking structures (roof centers, entrance corners, exit corners, and adjacent covers):
1. **Height Score ($S_{\text{height}}$):**
   - Base scoring: $\text{clamp}((\text{heightCm} / 900) \times 100, 0, 100)$.
   - Floor 1 (+3m): 70 / 100.
   - Floor 2 (+6m): 85 / 100.
   - Roof Level (+9m): 100 / 100.
2. **Cover Score ($S_{\text{cover}}$):**
   - High elevation relative to horizontal offset creates a steep downward firing angle ($> 45^\circ$), giving shooters natural head-glitch cover and placing targets inside a downward blind spot.
   - Standalone ground covers with height $\ge 140$ cm provide solid chest cover (75–90).
3. **Line-of-Sight Score ($S_{\text{los}}$):**
   - Evaluates sightline clarity down the full corridor corridor length.
   - Center perches with unobstructed longitudinal coverage receive 100 / 100.
   - Corners with partial angle occlusion receive 85 / 100.
4. **Total Composite Score:**
   $$\text{Total} = \text{round}(0.40 \times S_{\text{height}} + 0.30 \times S_{\text{cover}} + 0.30 \times S_{\text{los}})$$

### C. Kill Zone Classification & Rating
- Evaluates flanking quality: $\text{avg}(\text{leftBestScore}, \text{rightBestScore})$.
- Adds corridor length bonuses (longer corridors trap targets deeper before they can retreat).
- Adds narrowness multipliers (width $< 2.0$ m severely restricts lateral dodging).
- **Classifications:**
  - `fatal_funnel`: Length $\ge 12$ m, rating $\ge 85$, high elevated perches on both flanks.
  - `crossfire_alley`: Length $\ge 6$ m, high crossfire positions on both sides.
  - `elevated_ambush`: High one-sided firing perch over narrow passage.
  - `standard`: Narrow passage without dominant elevation advantage.

### D. 2D Danger Heatmap Model (`generateChokepointHeatmap`)
- Discretizes the corridor footprint into a 200 cm grid.
- Calculates normalized intensity ($0.0 \dots 1.0$) based on kill zone rating and proximity to high-scoring ambush perches (amplifying intensity up to 1.0 within 4 m of roof perches).

---

## 3. UI & Tooling Integration

1. **Standalone CLI Runner (`src/levelgen/cli/chokepoints.ts`):**
   - Command: `npm run chokepoints [--layout <path>] [--out <path>]`
   - Formats human-readable console breakdown and exports full JSON analysis.
2. **Interactive Canvas Rendering (`LayoutView.tsx`):**
   - Added prop `showChokepoints?: boolean`.
   - Renders red/amber semi-transparent heatmap cells over kill zones.
   - Draws dashed corridor centerlines with category and dimension tags.
   - Displays circular perch markers with elevation tags (`+3m`, `+6m`, `+9m`) and total scores.
3. **Level Generator Department (`LevelGenDepartment.tsx`):**
   - Added toggle button: `🔥 Узкие места (Kill Zones)`.
   - Embedded evaluation card summarizing detected corridors, flanking block heights, best perch ratings, and max danger level.

---

## 4. Run on Accepted Depot Reference (`gen_ref_01_02_depot_1.json`)

**CLI Command:**
```bash
node src/levelgen/cli/chokepoints.ts Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json --out Saved/TrainingGround/chokepoints/gen_ref_01_02_depot_1.chokepoints.json
```

**Results:**
- **Execution Time:** 1–2 ms.
- **Corridors Detected:** 2 major kill zones.
- **Active Heatmap Cells:** 30 grid cells (200 cm grid).

```
=== CHOKEPOINT & KILL ZONE ANALYSIS: gen_ref_01_02_depot_1 ===
Corridors detected (1-3 m wide between blocks >= 3 m): 2
- Narrow corridors (<= 2 m): 0
- Crossfire kill zones (rating >= 75): 2
- Peak Kill Zone rating: 100 / 100
- Average corridor width: 2.8 m | Average length: 13 m

[#1] Corridor: CHOKE_B_hall_n_B_warehouse
    Class: FATAL_FUNNEL | Kill Zone Rating: 100/100
    Dimensions: width 3.0 m (300 cm), length 18.0 m (1800 cm)
    Flanking Blocks:
      * Left:  B_hall_n (height: 9 m) -> best perch score: 100/100
      * Right: B_warehouse (height: 9 m) -> best perch score: 100/100
    Ambush Perches (6):
      - [LEFT] B_hall_n_roof_center @ [4100, 550] +9m: score 100 (h:100, cov:100, los:100)
      - [LEFT] B_hall_n_roof_entrance @ [3200, 550] +9m: score 95 (h:100, cov:100, los:85)
      - [LEFT] B_hall_n_roof_exit @ [5000, 550] +9m: score 95 (h:100, cov:100, los:85)
      - [RIGHT] B_warehouse_roof_center @ [4100, 850] +9m: score 100 (h:100, cov:100, los:100)
      - [RIGHT] B_warehouse_roof_entrance @ [3200, 850] +9m: score 95 (h:100, cov:100, los:85)
      - [RIGHT] B_warehouse_roof_exit @ [5000, 850] +9m: score 95 (h:100, cov:100, los:85)

[#2] Corridor: CHOKE_B_ruin_annex_B_ruin
    Class: CROSSFIRE_ALLEY | Kill Zone Rating: 100/100
    Dimensions: width 2.5 m (250 cm), length 8.0 m (800 cm)
    Flanking Blocks:
      * Left:  B_ruin_annex (height: 3 m) -> best perch score: 82/100
      * Right: B_ruin (height: 6 m) -> best perch score: 95/100
    Ambush Perches (6):
      - [LEFT] B_ruin_annex_roof_center @ [-3400, 900] +3m: score 82 (h:70, cov:75, los:100)
      - [LEFT] B_ruin_annex_roof_entrance @ [-3800, 900] +3m: score 77 (h:70, cov:75, los:85)
      - [LEFT] B_ruin_annex_roof_exit @ [-3000, 900] +3m: score 77 (h:70, cov:75, los:85)
      - [RIGHT] B_ruin_roof_center @ [-3400, 1150] +6m: score 95 (h:85, cov:100, los:100)
      - [RIGHT] B_ruin_roof_entrance @ [-3800, 1150] +6m: score 90 (h:85, cov:100, los:85)
      - [RIGHT] B_ruin_roof_exit @ [-3000, 1150] +6m: score 90 (h:85, cov:100, los:85)
```

---

## 5. Verification Results

1. **Unit & Integration Tests (`npm test`):**
   - 250 tests passed across all suites (0 failures).
   - Dedicated test suite `src/levelgen/chokepoints.test.ts`:
     - `extractTallBlocks`: filters low obstacles, correctly identifies tall structures.
     - `findChokepointCorridors`: verifies width (1–3 m) and block facing criteria.
     - `scoreAmbushPosition`: validates elevation, cover, and line of sight calculation.
     - `generateChokepointHeatmap`: verifies cell generation and normalized intensity bounds.
     - `analyzeChokepoints`: complete integration test on `gen_ref_01_02_depot_1.json`.
2. **Static Typecheck (`npm run typecheck`):**
   - Clean pass (exit code 0).
3. **Golden File Check (`npm run golden -- --check`):**
   - Golden files up to date (exit code 0).
4. **Vite Production Build (`npm run build`):**
   - Successfully compiled 2,691 modules into distribution bundle (exit code 0).
