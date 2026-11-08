# Report: T1. Training Ground - Real Levels & Horde Threshold

**Task:** T1. Training Ground - the Waves tab shows the real levels (+ the horde threshold field)  
**Branch:** `gemini/t1-training-ground-levels`  
**Date:** 2026-11-04  

## Summary of Changes

1. **Level Name Update (`Content/Data/LevelJson/L_Gen_Depot_1.json`)**:
   - Updated `level_name` from `"Generated ref_01_02_depot (seed 1)"` to `"0.1 Depot"`.

2. **Schema Update (`Content/Data/Schemas/level_config.schema.json`)**:
   - Added `horde_after_firefights` property: integer, minimum 1, default 6 (`"UE: horde triggers after N real-time firefights (default 6, min 1)"`).

3. **Training Ground TypeScript Types (`Tools/TrainingGround/src/types.ts`)**:
   - Added `horde_after_firefights?: number` to `LevelConfig` interface.

4. **Level List Categorization & Sorting Module (`Tools/TrainingGround/src/utils/levelList.ts`)**:
   - Implemented `buildLevelList`, `classifyLevelGroup`, and `extractCampaignMapLeaves`.
   - Groups levels into 4 categories with clean priority:
     - **Campaign**: Levels listed in `campaign.json` (ordered per campaign sequence).
     - **Generated**: Generated levels (e.g. `L_Gen_Depot_1.json`).
     - **Labs & tests**: Laboratory and test levels (`L_FormationLab.json`, `L_PmcArena.json`, etc.).
     - **Legacy stage_XX**: Legacy battles (`stage_01.json` .. `stage_12.json`, `level_01_outpost.json`).
   - Ignores `stage_template.json`.

5. **Dev Server Endpoints (`Tools/TrainingGround/vite.config.ts`)**:
   - `/api/list-stages`: Reads `campaign.json` and all level JSON files from `Content/Data/LevelJson/` and returns the categorized level list with group metadata and wave counts.
   - `/api/get-stage`: Supports querying by `id` or `file`, correctly resolving target level files.
   - `/api/save-stage`: Saves directly to the selected target file (query `id`/`file` or payload identifier). Removed the legacy unconditional clobbering of `level_01_outpost.json`.

6. **Level Settings UI (`Tools/TrainingGround/src/components/LevelSettingsPanels.tsx` & `src/utils/levelSettings.ts`)**:
   - Added `getHordeEnabled`, `setHordeEnabled`, `getHordeAfterFirefights`, `setHordeAfterFirefights`, `validateHordeAfterFirefights`.
   - Created `LevelHordePanel` component showing "Орда включена" (`horde_enabled`) and numeric input "Horde after N firefights" (`horde_after_firefights`, min 1, default 6).
   - Rendered `LevelHordePanel` in `App.tsx` level settings grid alongside `BlastDoorPanel`, `DemoPanel`, and `LevelColdPanel`.

7. **Dynamic Dropdown in Waves Tab (`Tools/TrainingGround/src/App.tsx`)**:
   - Replaced hardcoded `stage_01`..`12` `<select>` with dynamic grouped `<optgroup>`s (Campaign, Generated, Labs & tests, Legacy stage_XX) displaying each level's `level_name`.
   - Updated `handleSaveConfig` to pass the active level id in `/api/save-stage?id=...`.

8. **Automated Unit Tests (`Tools/TrainingGround/src/utils/levelList.test.ts` & `src/utils/levelSettings.test.ts`)**:
   - Added tests for `classifyLevelGroup`, `extractCampaignMapLeaves`, `buildLevelList` (sorting and template filtering).
   - Added real data verification test confirming all repo levels in `Content/Data/LevelJson/` and `campaign.json` parse and group correctly.
   - Added tests for `horde_after_firefights` default, min, validation, and JSON roundtrip.

---

## Verification & Test Results

All tests run inside `Tools/TrainingGround`:

1. **Unit tests (`npm test`)**:
   - Total tests: **250 passed, 0 failed, 0 skipped** (duration: 82.7s).
2. **Typecheck (`npm run typecheck`)**:
   - `tsc && tsc -p tsconfig.node.json`: **0 errors**.
3. **Golden check (`npm run golden -- --check`)**:
   - Golden files up to date: **0 diffs**.

---

## Open Questions / Notes
- None. `L_Gen_Depot_1.json` is ready with title `"0.1 Depot"` and belongs to `Generated` group until Claude integrates it into `campaign.json`.
