# T-020 report - Training Ground «Управление картой»

Branch `claude/tg-map-management` (from origin/main 7325fc32), worktree `CodexTactics-claude`. Executor: Claude Opus 5.5.
Only `Tools/TrainingGround/**` and `docs/port/claude/**` touched. No Content/Data file committed, no C++ / maps / scalability / VFX.

## What changed, per brief item

1. **One current map for the department.** Selector «Текущая карта» at the top of «Генератор уровней» (both sub-tabs), listing every
   generated map (level JSON with `levelgen_layout` and/or a non-fixture props file) as `Map <- layout`. Default order:
   saved choice (`Saved/TrainingGround/current_map.json`, server side; localStorage only as a first-paint cache) -> map of the most
   recently accepted layout (meta `at`) -> first map by name. «Принять» makes the accepted layout's map current.
   Switching with unsaved weather/props edits asks first.
   Files: `src/levelgen/currentMap.ts` (pure rules), `src/levelgen/node/currentMapFiles.ts`, `server/levelgenApi.ts`
   (`GET /api/levelgen/maps`, `POST /api/levelgen/current-map`).
2. **«Генератор раскладок» -> «Управление картой».** First tab and default. Opens the current map's accepted layout at once
   (`openAccepted`; a candidate already on the page is just selected, so unsaved region edits are not dropped), the title shows the
   layout and `карта <Map>`. Candidate generation stays on the page ("Генерация кандидатов", does not change the current map until «Принять»).
3. **Weather section** (`src/departments/MapWeatherSection.tsx`, collapsible; collapsed it shows a one-line summary so the layout stays on top).
   Controls = the Wave Editor's own `LevelEnvironmentPanel` + `LevelColdPanel` (moved, same setters/validation) with a short Russian hint under
   every control, plus the new `environment.weather_strength` (0..2, step 0.1, default 1, "1 = как сейчас") and top-level `cold_rate_mult` (0..3).
   Writes only `environment` + `cold_rate_mult` of `Content/Data/LevelJson/<Map>.json` through `POST /api/levelgen/map-environment`
   (`src/levelgen/node/levelEnvironment.ts`): validated, atomic (temp + rename), text-preserving (`patchJsonText`: other keys, `_comment`s,
   BOM/CRLF, hand layout kept), **no write when unchanged**, explicit button «Сохранить погоду» only.
   `weather_strength` / `cold_rate_mult` equal to 1 are written as an absent key (same convention as the existing `cold_rate_mult`).
4. **«Волны»**: only the weather panel removed (App.tsx). Because the tab still holds a copy of the level, its «Применить в игру» now takes the
   file's current `environment` from the server before saving, so it can never write a stale weather block over the map page's edit.
   «Холод уровня» (cold) stays in «Волны» as the brief says (the same key is also editable on the map page; same file, same validation).
5. **«Пропсы уровней»** follows the current map: no Depot default, no file picker (the map name + file path are shown, the map is switched
   from the bar). All layers of the file are listed with kind, count, seed, gate. Canvas legend is built from the kinds present in the file
   (crates, barrels, turrets, mines, loot, power); generators are now drawn too (orange squares with cables to their turrets).
   Re-roll of one layer / «Сгенерировать всё» works for crates, barrels, turrets, mines, loot and power
   (`src/levelgen/props/rerollKinds.ts`): placement in the autogen's kind order, then the 088 route clearance, the static validator's drops
   and the power network rebuild. Locked layers, `pmc_charges`, frozen files and unknown kinds are skipped with a reason in the UI (button
   title + a line under the layer + the result note), never an error. Pinned (hand-locked) objects stay even inside the clearance.
   The route clearance was extracted from `autogen.ts` into a pure module (`routeClearance.ts`, config in `autogenConfig.ts`); autogen's
   output is unchanged (its existing 088 tests pass). `GET /api/levelgen/props` now also returns `props_autogen.json` as `autogen`.

## Gate

- `npm run typecheck`: clean (both tsconfigs).
- `npm test`: 541 tests, 541 pass, 0 fail (the known icon test did not fail on this machine/worktree). New tests, all on temp dirs / in-memory
  files / the committed Depot layout (read-only): `currentMap.test.ts` 10 (default order, list from level JSON/props/meta, persisted choice, no-op
  write), `node/levelEnvironment.test.ts` 9 (keeps other keys and `_comment`, byte-identical no-op, BOM/CRLF, add/remove blocks, validation,
  temp-dir write, path-like names refused), `props/rerollKinds.test.ts` 10 (every kind, determinism, clearance bites + pins stay, locked/pmc/unknown
  skipped, power rebuild links valid, frozen refuses, no stale attach warnings), `levelSettings.test.ts` +1 (`weather_strength`).
- `python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude`: PASS, risk 0.1/4 (run on the whole branch diff vs 7325fc32:
  11 modified files, all under Tools/TrainingGround; it only audits uncommitted changes, so I temporarily un-committed and restored HEAD).

## Browser check (dev server from the worktree on port 5191; 5173 was taken by another process, left alone)

Screenshots in `docs/port/claude/outbox/T020/`:
1. `01`/`02`: «Управление картой» opens with `L_Gen_Depot_1_X6 <- gen_ref_01_02_depot_1_x6` current and its layout loaded (title `gen_ref_01_02_depot_1_x6 карта L_Gen_Depot_1_X6`).
2. `03`: weather controls read the level JSON: on L_Gen_Depot_1 locked / Clear / start_hour 12 / snow 0.35 / cold 0 (the committed X6 level JSON has no
   `environment` block and `cold_rate_mult` 0, so it shows defaults + cold 0; the designer's uncommitted X6 edits live only in the main checkout).
3. `04`: «Пропсы уровней» on X6: 6 layers (crates 116 / barrels 47 / turrets 11 / mines 38 / loot 56 / power 9), legend with all kinds.
4. `05`: «Сгенерировать всё» on X6 re-rolled all six layers, note "Трасса (088) убрала 2 ..., Валидатор игры убрал 4, Сеть питания пересобрана".
5. `06`: «Волны» has no weather panel (blast door, horde, demo, cold remain).
Also checked in the browser: map switch persisted on the server and survived a reload; a real weather Save on `L_Gen_Test_X2.json` changed only the
`environment` block (diff: `weather`, `weather_strength` added); the file was then restored with `git checkout` (nothing of Content/Data committed).
X6 files were never saved. Not re-checked in the browser: the Wave Editor save overlay (typecheck + reasoning only).

## Open points

- Re-roll all on a big map is slow: X6 (354 x 248 m) takes about a minute and the page is busy meanwhile (placement runs on the main thread; the
  placement cost itself is pre-existing). A worker / per-layer yielding would fix the freeze; not done (out of scope).
- `weather_strength` is only written; the game side is T-021. The 1 = absent-key choice can be flipped to "always write" in `setEnvironmentKey` if T-021 wants the key present.
- Cold multiplier is editable in two places (map page and «Волны»); both write the same level JSON key. Say if «Холод уровня» should leave «Волны» too.
- «Принять» sets the current map from the maps list after the accept; a layout whose map has no level JSON / props yet still appears via the props file written by accept.
- The selector also lists the training arenas (L_Gen_Train_*) and L_Gen_BgenTest_X6 (they have `levelgen_layout`); trivial to filter if unwanted.
- Not done: HANDOFF / PORT_MATRIX / TASK_BOARD updates (outside the allowed paths).
