# 090 - Tactical Map Export v2 Plan (Smart Enemies T-09)

## Goal
Upgrade the tactical map format to `codex.tactical_map/2` without removing any `/1` fields, enabling enemies to reason about buildings, storeys, roof accesses, open street crossings, and cover sites surrounding mission goals.

---

## 1. Schema & Data Model Extensions (`codex.tactical_map/2`)
- **Schema Identifier**: `codex.tactical_map/2` (constant `TACTICAL_MAP_SCHEMA = 'codex.tactical_map/2'`).
- **Building & Storey Attribution**:
  - `windows[]`: Already carries `building` and `floor`.
  - `overwatch_roofs[].spots[]`: Add `building: string` and `floor: number` (level of the roof) to each spot.
  - `hiding_spots[]`: If `pos` lies within a building's footprint (`inFootprint(b, pos)`), populate `building: b.id` and `floor: number` (floor z / 300); otherwise `building: null`, `floor: 0`.
  - `chokepoints[]`: If `pos` lies within a building's footprint, populate `building: b.id` and `floor: number`.
- **Roof Access on Every Walkable Roof**:
  - Structure `TacticalRoofAccess { id: string; kind: 'stairs' | 'ladder' | 'hatch'; top: Vec3 }`.
  - Top landing points calculated in 3D world space (`at(top, roofZ)`).
  - Added to `overwatch_roofs` and `ladder_only_roofs` as `access_points[]`, and added to a comprehensive `walkable_roofs[]` list for every building with `roof_walkable`.
- **Open Crossings (`open_crossings[]`)**:
  - Route segments $\ge 10\text{ m}$ (1000 cm) having no cover within 2 m (200 cm) of the path.
  - Structure: `TacticalOpenCrossing { id, route, start: Vec3, end: Vec3, length_m, nearest_cover_cm, seeing_covers: string[] }`.
  - Lists the cover objects with line-of-sight/field-of-view overlooking the open segment.
- **Goal Sites (`goal_sites[]`)**:
  - For each mission goal in `l.objectives.goals` (or auto-placed objectives):
    - Find cover objects within 25 m (2500 cm).
    - Structure: `TacticalGoalSite { goal_id, cover_id, pos: Vec3, dist_m, facing_yaw, sector_arc_deg, cover_kind, cover_height_cm }`.
    - Stop rule: Geometry-only export in TS, leaving runtime trace validation to T-05.

---

## 2. C++ Loader Updates (`TacticalMapRules` & Subsystem)
- **Schema Compatibility**:
  - Support both `codex.tactical_map/1` and `codex.tactical_map/2`.
- **Data Structures**:
  - Add `Building` and `Floor` to `FTacticalChokepoint`, `FTacticalHidingSpot`, and `FTacticalOverwatchSpot`.
  - Add `FTacticalRoofAccess` and `AccessPoints` to `FTacticalOverwatchRoof` and `FTacticalLadderRoof`.
  - Add `FTacticalOpenCrossing` and `FTacticalGoalSite` structs to `FTacticalMapFile`.
  - Add `FTacticalWalkableRoof` and `WalkableRoofs` list.
- **Subsystem Queries**:
  - Accessors for `OpenCrossings`, `GoalSites`, and `GetGoalSites(GoalId)`.

---

## 3. Map File Regeneration
- Regenerate tactical files using `Tools/TrainingGround/scripts/export-tactical-map.ts`:
  - `L_Gen_Test_X2` -> `Content/Data/LevelGen/tactical/L_Gen_Test_X2.tactical.json`.
  - `L_Gen_Depot_X4` -> `Content/Data/LevelGen/tactical/L_Gen_Depot_X4.tactical.json`.
  - **Constraint**: NEVER overwrite `L_Gen_Depot_1_X6*` or `gen_ref_01_02_depot_1_x6*` while designer uncommitted edits exist.

---

## 4. Verification & Gates
1. **Training Ground**:
   - `npm run typecheck` in `Tools/TrainingGround`.
   - `npm test` / `node --test src/levelgen/tacticalMap.test.ts`.
2. **Engine & C++**:
   - `Scripts/test.ps1 -Filter CodexTactics.LevelGen` (automation tests).
   - Smokes: `TacticalMapSmoke`, `TacticalPmcSmoke`, `MissionGoalsSmoke`.
3. **Audit**:
   - `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini`
   - `git diff --check`
