# 092 - a building moved on the map plan takes its props and tactical spots with it (TOOL) - MEDIUM, TS
Limit rule: at 10 % of your daily limit left: stop, commit (WIP is fine), handover at the top of the report, FINISH line.
Base: origin/main newest (>= b9507af7, includes T-022/023). Branch gemini/092-move-building-carries. Worktree CodexTactics-gemday.
Before switching branches in the worktree: no UnrealEditor-Cmd / build may run on it.
Router: L3 (ue-l3-major class) - TS only.

## Design gate (lead filled)
- Spec: T-022 brief docs/port/claude/inbox/T022-tg-map-edit-fullscreen.md section 2 ("Persistence": derived files are listed
  stale, never regenerated silently) + the T-022 report review point "props / tactical do not follow a moved building".
- Acceptance (what the designer sees): in «Управление картой» he drags a building; after «Принять» the page offers ONE button
  «Перенести пропсы и точки вместе со зданием» (per moved building, or all). Pressed: every prop standing on / inside that
  building's footprint (any storey, roof) and every tactical spot / overwatch spot / firing-gap id of that building moves by the
  same delta (rotation is not edited by T-022, so translation only). Props outside the building do not move. Not pressed: the
  current behaviour (listed stale) stays.
- Proved by: unit tests in src/levelgen/mapEdit.test.ts (or a new moveDerived.test.ts) on fixtures: (1) props inside the moved
  footprint move by the delta, outside ones byte-identical; (2) a locked prop inside the building moves too (it belongs to the
  building) and stays locked; (3) tactical spots with that building id move, others identical; (4) after the carry the props
  validator (validator.ts / fastGate) and the 088 route clearance run and their new problems are shown; (5) undo restores both files.
- Designer decision needed? none (the opt-in button keeps today's behaviour as the default).

## Evidence so far
- src/levelgen/mapEdit.ts:114 - "NOT moved (the page lists them as stale): the props file, the tactical map, the analysis, the PMC plan."
- moveBuilding / translateBuilding (mapEdit.ts:82-160) already move the layout parts; reuse the same delta.
- Props are saved by the existing «Сохранить» (PUT /api/levelgen/props, revision check); tactical by the existing export path.

## Task
1. Read mapEdit.ts, the T-022 / T-023 reports (docs/port/claude/outbox/T022-*.report.md, T023-*.report.md) and how the page builds
   the stale list after «Принять». Write in the report which derived file is keyed how (prop -> building? by position only?).
2. Pure functions `carryPropsWithBuilding(props, buildingBefore, delta)` and `carryTacticalWithBuilding(tactical, buildingId, delta)`
   with tests; footprint test = the building's polygon from the layout BEFORE the move (a prop on the roof counts).
3. The button + undo through the page's history; the save still goes through the existing «Сохранить» / export (no new endpoint
   unless the tactical file has none - then report it and stop at the pure functions).
4. Stop rule: > ~4 h -> commit WIP + exact next step.

## Never
- Change / weaken a test, its timing or fixture to make it pass. Touch C++, Content/Maps, Content/VFX, Config/DefaultScalability.ini.
- Commit any Content/Data file (the designer has uncommitted X6 files in the main checkout; tests use fixtures / temp dirs only).
- Press «Принять» / «Сохранить» on any X6 map in the browser check. Push or merge main.

## Gate (all before FINISH)
`npm test` + `npm run typecheck` in Tools/TrainingGround (counts in the report; 579 pass on b9507af7) | browser check on
L_Gen_Test_X2 (drag a building, «Принять» into a temp copy or revert with `git checkout` after, the carry button, undo) -
screenshots docs/port/gemini/outbox/092/ | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` | `git diff --check`.
Allowed paths: Tools/TrainingGround/**, docs/port/gemini/**.

## Report
`docs/port/gemini/outbox/092-tg-move-building-carries.report.md`: handover, what changed (file:line), gate results, test counts
before -> after. FINISH one line in chat topic `gemini-092`:
`092 FINISH | <status> | <commit> | done: ... | NOT done: ... | gate: ... | report: <path>`.
