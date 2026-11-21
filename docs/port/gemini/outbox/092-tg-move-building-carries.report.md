# 092 report - Training Ground «Управление картой»: carry props and tactical spots with moved building

Branch `gemini/092-move-building-carries` (base `origin/main` newest). TypeScript only (`Tools/TrainingGround`), no C++, no Content/Data committed, no .uasset / .umap.

## Handover (read first)

- **Done:**
  1. Pure functions `carryPropsWithBuilding(props, buildingBefore, delta)` and `carryTacticalWithBuilding(tactical, buildingId, delta)` in `Tools/TrainingGround/src/levelgen/moveDerived.ts`.
  2. Unit tests in `Tools/TrainingGround/src/levelgen/moveDerived.test.ts` (8 new tests, 100% pass; test suite increased from 579 to 587 tests, 0 failures).
  3. UI integration in Training Ground «Управление картой» (`LevelGenDepartment.tsx`):
     - Moving a building and clicking «Принять» records the moved building ID and translation delta into `staleNote`.
     - In the stale note banner, an opt-in button is offered: `«Перенести пропсы и точки вместе со зданием»` (`data-carry-derived`).
     - Clicking the button carries every prop inside/on the building footprint (ground, storeys, roof, attached props, overrides, cables), runs static validation (`validateStatic`) and route clearance (088), saves to disk via existing `PUT /api/levelgen/props`, and displays the outcome.
     - Offers `«Отменить перенос»` (`data-undo-carry`), which restores previous props state.
  4. Browser check on `L_Gen_Test_X2` driving headless Edge over CDP (`browser-check.mjs`):
     - Generated 5 screenshots in `docs/port/gemini/outbox/092/` (`01-before-move.jpg` through `05-carry-undone.jpg`).
  5. Gates passed: `npm test` (587 pass), `npm run typecheck` (clean), `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` (Boundary check PASS, Risk Rating 0.1 / 4), `git diff --check` (clean). No Content/Data files committed.
- **Left / not done:** None for task 092. Next task: 093 (`gemini/093-paired-ab-digest`).
- **Next exact step:** Switch branch to `gemini/093-paired-ab-digest` from fresh `origin/main` and implement paired A/B digest.

---

## 1. Analysis: How derived files are keyed

### `props.json` keying
- Level props do **NOT** have an explicit `building` property.
- Ownership is determined by:
  1. **2D Footprint containment**: `inFootprint(buildingBefore, prop.pos, 0)` checks whether the prop's `pos` lies within the building's bounding footprint polygon from before the move.
  2. **Layer association**: Props on building roof or intermediate floors have `walk_layer === 'roof:<buildingId>'` or `walk_layer === 'floor:<buildingId>:<floorIndex>'`.
  3. **Attachment hierarchy**: Props attached to containers (e.g. `attach: 'crate_01'`) inherit the moved status of their parent container.
  4. **Manual overrides**: Hand-placed overrides matching moved prop IDs are translated in lockstep so designer adjustments persist.
  5. **Cables / generator wires**: When a prop with a connected generator cable moves, the cable's endpoint is translated and its length recomputed.
  6. **Locked props**: A locked prop on/inside the building footprint belongs to the building; it moves with the building and stays locked (`locked: true`).
  7. **Outside props**: Props outside the building footprint remain byte-for-byte untouched.

### `tactical.json` keying
- Tactical spots have explicit building ownership attributes:
  1. **Windows**: `window.building === buildingId`.
  2. **Overwatch positions**: `roof.building === buildingId`.
  3. **Ladder-only roofs**: `roof.building === buildingId`.
  4. **Drop ambush spots**: `spot.surface === 'roof:<buildingId>'` or `spot.layer === 'roof:<buildingId>'` or `surface.includes(buildingId)`.
  5. **Hiding spots**: `spot.building === buildingId` or `spot.cover.startsWith('wall:<buildingId>_')`.
- Non-matching tactical entries remain byte-identical.

### Tactical API vs Pure Functions
- The dev server does not expose a standalone HTTP endpoint for arbitrary client-side writes to `.tactical.json` (`PUT /api/levelgen/tactical` does not exist; tactical maps are generated on the server during map accept/build via `export-tactical-map.ts`).
- Per brief instructions (*"no new endpoint unless the tactical file has none - then report it and stop at the pure functions"*):
  - Pure function `carryTacticalWithBuilding(tactical, buildingId, delta)` is implemented and verified with exhaustive unit tests in `moveDerived.test.ts`.
  - Props saving uses the existing `PUT /api/levelgen/props` endpoint with revision checking.

---

## 2. What changed (file:line)

### New files
- `Tools/TrainingGround/src/levelgen/moveDerived.ts`:
  - `carryPropsWithBuilding(props, buildingBefore, delta)`: translates ground/storey/roof props, attached props, overrides, cables.
  - `carryTacticalWithBuilding(tactical, buildingId, delta)`: translates windows, overwatch spots, drop spots, hiding spots.
  - `isPropOnBuilding(prop, buildingBefore)`: helper testing footprint and walk_layer.
- `Tools/TrainingGround/src/levelgen/moveDerived.test.ts`:
  - 8 unit tests validating all requirements:
    1. props inside moved footprint move by delta, outside props byte-identical.
    2. locked props inside building move and remain locked; unlocked stay unlocked.
    3. attached props cascade with parent container; hand overrides shift.
    4. generator cables re-measured when linked prop moves.
    5. zero delta or non-existent building leaves props unchanged.
    6. tactical spots belonging to building move; others identical.
    7. props validator (`validateStatic`) and route clearance run on carried props.
    8. undo restores props and tactical map byte-for-byte.
- `docs/port/gemini/outbox/092/`:
  - `browser-check.mjs`: CDP script driving headless Edge.
  - `browser-check.log`: Execution log.
  - `01-before-move.jpg`: Map view before move.
  - `02-building-moved.jpg`: Plan with `B_hall_n` selected and moved.
  - `03-stale-banner-with-carry-button.jpg`: Stale banner showing «Перенести пропсы и точки вместе со зданием».
  - `04-props-carried.jpg`: Banner showing carried props (6 шт.), validation PASS, and «Отменить перенос».
  - `05-carry-undone.jpg`: Banner showing carry undone and carry button restored.

### Modified files
- `Tools/TrainingGround/src/levelgen/mapEdit.ts`:
  - Re-exports `carryPropsWithBuilding`, `carryTacticalWithBuilding`, `isPropOnBuilding`.
- `Tools/TrainingGround/src/departments/LevelGenDepartment.tsx`:
  - Tracks moved buildings and deltas in `staleNote.buildingMoves`.
  - Implements `carryDerivedProps()` calling `carryPropsWithBuilding`, `validateStatic`, and route check.
  - Implements `undoCarryDerivedProps()` to restore previous props state.
  - Adds buttons `[data-carry-derived]` and `[data-undo-carry]` in stale note banner.

---

## 3. Gate results

| Gate | Status | Detail |
|---|---|---|
| `npm test` | **PASS** | **587 pass**, 0 fail, 26 suites (baseline was 579 pass, +8 new tests) |
| `npm run typecheck` | **PASS** | 0 errors across `tsconfig.json` and `tsconfig.node.json` |
| `typesafe_triage.py --audit-diff` | **PASS** | Boundary Breach: PASS, Risk Rating: 0.1 / 4 |
| `git diff --check` | **PASS** | Clean, no whitespace or merge marker issues |
| Content/Data safety | **PASS** | Reverted test changes with `git checkout Content/Data/LevelGen/`; 0 Content/Data modifications |

---

092 FINISH | PASS | 58c05aac | done: carryPropsWithBuilding, carryTacticalWithBuilding, UI button + undo, tests, browser check | NOT done: none | gate: npm test 587 pass, typecheck clean, audit-diff PASS | report: docs/port/gemini/outbox/092-tg-move-building-carries.report.md
