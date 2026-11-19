# T-020 - Training Ground: «Управление картой» (current map, weather per map, props of the current map) (исполнитель: субагент Sonnet)
Designer 2026-11-19, urgent ("мне тяжело управлять проектом, когда ещё ничего толком не подключено"). TS only (Tools/TrainingGround).
Branch `claude/tg-map-management`. Worktree `CodexTactics-claude`. Status lines: chat topic `claude-T020`.

## What the designer sees today (lead diagnosis, main f62ffd0c+)
1. Level generator -> «Генератор раскладок»: on opening nothing is shown until «Сгенерировать». Accepted layouts are listed
   (`/api/levelgen/accepted`) and open only by a click (`openAccepted`, src/departments/LevelGenDepartment.tsx ~427); nothing
   remembers or opens the map he works on (L_Gen_Depot_1_X6 <- accepted/gen_ref_01_02_depot_1_x6.json; the level JSON
   `Content/Data/LevelJson/<Map>.json` has `levelgen_layout` pointing at its accepted layout).
2. «Пропсы уровней (T5)» (src/departments/PropsPanel.tsx): defaults to L_Gen_Depot_1 (`files.find(f => f.map === 'L_Gen_Depot_1' ...)`
   ~line 71), whose props file has only crates + barrels. L_Gen_Depot_1_X6.props.json has crates, barrels, turrets, mines,
   loot, power. Re-roll (single / all) only supports crates / barrels (~195, ~231); the canvas legend lists only Ящики / Бочки
   (PropsCanvas.tsx ~637) although it draws turrets / mines / loot.
3. Weather lives in the «Волны» tab (src/components/LevelSettingsPanels.tsx ~298-454, used from App.tsx); the designer wants
   it OUT of «Волны» (that tab is for waves only) and IN the map management page.

## Lead design
- **One "current map" for the whole «Генератор уровней» department**: a map selector at the top of the department (both
  sub-tabs), listing every generated map that has a level JSON with `levelgen_layout` and / or a props file (show the map name
  L_Gen_Depot_1_X6 and its layout name). Default = the last one the designer picked (server-side
  `Saved/TrainingGround/current_map.json`, so it survives a browser change; localStorage as a cache only), else the most
  recently accepted layout (meta `at`). The name is always visible.
- Sub-tab «Генератор раскладок» is renamed **«Управление картой»**. On opening it shows the current map's accepted layout at
  once (reuse `openAccepted`), with its name. Generating candidates stays available on the same page (a section / button), it
  does not change the current map until «Принять».
- **Weather section on «Управление картой»** for the current map, writing its `Content/Data/LevelJson/<Map>.json`
  `environment` block through the server (atomic write, keep every other key and the `_comment`s; no write when unchanged):
  тип погоды на старте `environment.weather` (+ climate arctic/temperate as today), `environment.locked`, `start_hour`,
  `override_carry` (move the existing controls of LevelSettingsPanels' weather panel, do not duplicate the logic),
  **сила воздействия погоды** `environment.weather_strength` (number 0-2, step 0.1, default 1: how strongly the weather changes
  the cold; the C++ side is added by task T-021 in parallel - write the key, show "1 = как сейчас"), **коэффициент холода**
  `cold_rate_mult` (top-level key of the level JSON, 0-3, default 1). Short Russian hints under each control.
- **Remove the weather panel from «Волны»** (only weather; leave the rest of that tab alone - the designer will list more later).
- **«Пропсы уровней»** follows the current map (no Depot_1 default): its props file is loaded automatically; the layer list
  shows every layer of the file with its kind and count; the legend lists every kind drawn (crates, barrels, turrets, mines,
  loot, power...); re-roll of a single layer / all unlocked layers works for every kind the autogen supports
  (src/levelgen/props/autogen.ts `generatePropsForLayout` - reuse it per layer; keep locked layers and `pmc_charges` untouched;
  after a re-roll apply the 088 route clearance the same way the CLI does, see autogen.ts "Task 088"). If a kind cannot be
  re-rolled alone, say so in the UI instead of an error.
- IMPORTANT data rule: the designer has UNCOMMITTED edits in the main checkout to the X6 files (accepted/gen_ref_01_02_depot_1_x6*.json,
  props/L_Gen_Depot_1_X6.props.json, LevelJson/L_Gen_Depot_1_X6.json, ...). Your code must never overwrite a file it did not
  change on the user's explicit Save; tests use fixtures / temp dirs, never those files. Do not commit any Content/Data change.

## Gate
`npm test` and `npm run typecheck` in Tools/TrainingGround (report counts; the known icon test failure stays the only one);
new unit tests for the current-map resolution (default order) and the environment-block write (keeps other keys, no-op when
unchanged); run the app (`npm run dev`, see its vite config for the port / the server middleware) and check in a browser that
(1) «Управление картой» opens with L_Gen_Depot_1_X6 shown, (2) weather controls read the X6 level JSON values, (3) «Пропсы уровней»
shows the X6 props with all 6 layers, (4) «Волны» has no weather panel. Screenshots into docs/port/claude/outbox/T020/ (PNG).
Do not press Save on the X6 files during the check. `python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude`.

## Never
Touch C++ / Content/Maps / Config/DefaultScalability.ini / Content/VFX; commit Content/Data files; push or merge main.

## Allowed paths
`Tools/TrainingGround/**` (src, server, tests), `docs/port/claude/**`.
Report: `docs/port/claude/outbox/T020-tg-map-management.report.md`.
