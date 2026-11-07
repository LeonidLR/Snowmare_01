# Task #34: Replace Spitter with Frostbitten in Enemy Rosters

- **Branch**: `gemini/t34-spitter`
- **Scope**: Data-only change in enemy rosters / pressure compositions. No C++ classes removed (`ASpitterCharacter` / `EEnemyArchetype::Spitter` intact). No `stage_*.json` modified.

---

## 1. Roster Audit and Changes

### 1.1 `Content/Data/LevelJson/L_FormationLabCombat.json`
- **Before**:
  - Line 5: `... every 2 minutes a group of 1-3 (Hound, Frostbitten, Spitter, Cutter, Marksman) ...`
  - Line 24: `"types": { "HOUND": 1, "FROSTBITTEN": 1, "SPITTER": 1, "CUTTER": 1, "MARKSMAN": 1 }`
- **After**:
  - Line 5: `... every 2 minutes a group of 1-3 (Hound, Frostbitten, Cutter, Marksman) ...`
  - Line 24: `"types": { "HOUND": 1, "FROSTBITTEN": 2, "CUTTER": 1, "MARKSMAN": 1 }`
- **Rationale**: Replacing `SPITTER: 1` with `FROSTBITTEN` transfers Spitter's 1-share weight to Frostbitten (`1 + 1 = 2`), maintaining total weight 5 (40% Frostbitten, 20% Hound, 20% Cutter, 20% Marksman) without altering the relative probabilities of the remaining archetypes.

### 1.2 `Content/Data/CombatLab/presets.json`
- **Presets Updated**:
  - `default`
  - `patrols_only`
  - `ambushes_only`
  - `coordinated_only`
- **Changes**: In all four presets, `"SPITTER": 1` was removed from `"types"` and `"FROSTBITTEN"` weight set to `2`.

### 1.3 `Content/Data/AI/horde.json`
- **Audit**: Inspected line-by-line.
  - The runtime horde composition already consists solely of:
    ```json
    "composition": {
      "FROST_HOUND": 5,
      "FROSTBITTEN": 3
    }
    ```
  - Spitter is not part of the active spawn composition (only cited in line 2 comment as an engine archetype). No changes required.

### 1.4 `Content/Data/Environment/environment.json`
- **Audit**: Inspected line-by-line.
  - `environment.json` does not declare enemy spawn rosters or wave compositions; it defines environmental simulation and weather suitability rules.
  - The `"pairs"` entry `{ "from": "SPITTER", "to": "CUTTER", "ratio": 1 }` and `"suitability"` table define how Spitter transitions to Cutter during snowstorms and are verified by unit tests (`EnvironmentTuningTest.cpp:386-392`). Preserved intact to prevent test regression.

---

## 2. Verification Evidence

### 2.1 JSON Schema / Syntax Validation
```powershell
python -c "import json, sys; [json.load(open(f, 'r', encoding='utf-8')) for f in sys.argv[1:]]; print('ALL JSON VALID')" Content/Data/Environment/environment.json Content/Data/AI/horde.json Content/Data/CombatLab/presets.json Content/Data/LevelJson/L_FormationLabCombat.json
# Result: ALL JSON VALID
```

### 2.2 TypeScript Automated Test Suite (`Tools/TrainingGround`)
```bash
npm test
# Result: 245 tests, 0 failed, 245 passed (including pressureSettings and combatLab presets verification)
```

### 2.3 C++ Automation Tests
```powershell
powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Filter "CodexTactics.AI"
# Result: 51 tests, 0 failed.

powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Filter "CodexTactics.LevelJson"
# Result: 7 tests, 0 failed.
```

### 2.4 Smoke Tests (`smoke_parallel.ps1`)
```powershell
powershell -ExecutionPolicy Bypass -Command "& Scripts/smoke_parallel.ps1 -Commands HordeSmoke,FormationLabCombatSmoke,CombatLabAmbushSmoke -Jobs 2 -ExtraArgs '-Multiprocess'"
```
**Results:**
- `HordeSmoke`: **PASS** (41.7 s)
- `FormationLabCombatSmoke`: **PASS** (47.2 s)
- `CombatLabAmbushSmoke`: **PASS** (43.1 s)
*(All 3 passed on first run; 0 failed).*
