# T-023 report - Training Ground «Пропсы уровней»: zoom / pan / F11, zone re-roll, drag-move props

Branch `worker/t023-tg-props-edit` (= T-022 `7be7d2d2` on top of origin/main). TypeScript only (`Tools/TrainingGround`), no C++, no .uasset / .umap, no Content/Data change committed.

## Handover (read first)

- **Done:** everything in the T-023 brief section: zoom (wheel at the cursor, buttons, %), pan, «Вписать»; F11 / «На весь экран» / Esc / «Свернуть» through the shared T-022
  helper; zone re-roll by box / polygon / lasso with the map page's `RegionOverlay`; drag + arrow-key move of props with auto-lock and snap to the walk surface;
  validation (game validator + fast gate) after every edit in the report panel; save through the existing «Сохранить»; confirm before leaving with unsaved edits;
  undo / redo (not asked, needed for the drag). Unit tests (21 new) + headless-browser check on `L_Gen_Test_X2` with a real save and a byte-identical restore.
- **Not done / limits:**
  1. The designer's own hands-on check on a real map (X6 untouched; I never saved / accepted / built anything on it).
  2. The **power layer is not re-rolled by a zone** (the generator network is shared by the whole map: rebuilding it would move objects outside the zone). A turret / loot zone
     re-roll only REPORTS stale generator links (`Сеть питания: N замечаний ... перебросьте слой power целиком`). Moving a turret or a generator by hand re-measures `cable_cm`.
  3. A hand move is **not constrained** by the placement rules, only by "there must be a walk surface under it" (off the map / in the canal / outside a floor = refused). A spot that
     the game would drop at load (inside a wall / a building, in a door corridor, near a ladder) is allowed and shown in the report as a NEW problem (red ring on the plan, list on the right).
  4. Hand-ADDED objects (`overrides` `add`) are not in `layers[]`, so the canvas never drew them and they cannot be picked / moved here (pre-existing; a `move` override of a layer object does follow the move).
  5. The department switch asks only through `unsavedGuard` (props edits). The map page's own dirty flag is not registered there (T-022 page keeps its own `beforeunload` + map-switch confirm).
  6. A zone re-roll of a big layer takes 8-17 s on the Depot fixture (the placement works on the whole map, then only the zone's part is taken); the busy line shows it.
  7. `docs/port/HANDOFF.md` is outside my allowed paths: the lead should add one line ("T-023: props tab zoom / F11 / zone re-roll / drag-move, `levelgen/props/zoneReroll.ts`, `propsEdit.ts`, `utils/unsavedGuard.ts`").
- **Next exact step:** the lead reviews the diff; the designer tries the tab on a COPY or on `L_Gen_Test_X2` (steps at the bottom), then `git checkout` the props file.

## What changed (file:line, current tree)

New
- `src/levelgen/props/zoneReroll.ts` - `rerollLayerInZone`: the layer is re-rolled in a CLONE with the new seed (placement keeps all its rules / density), then only fresh objects INSIDE the
  zone are taken; everything else is kept exactly (outside the zone, locked flag, hand origin, `move` / `set` override pins). Fresh objects closer than the layer's min spacing to a kept one are
  dropped (no break at the zone border); ids never collide with kept ones. `zonePreview` = the cyan / green marks.
- `src/levelgen/props/propsEdit.ts` - `moveProp` (delta -> new file, never mutates the input; snap to the walk layer under it via `resolveWalkLayer`; attached mines / loot go along; `move`
  override follows; generator cables re-measured; `locked = true` unless `lock: false`), `moveRefusal` (frozen / locked layer / `pmc_charges`), `pickProp`, `propRadius`, `setPropLocked`, `checkProps`
  (= `validateStatic` + `runFastGate`), `newIssues` (a defect the file already had is not blamed on the move), `withGate`, `PROP_SNAP_CM` (10 / 25 / 50 / 100 cm).
- `src/utils/unsavedGuard.ts` - `setUnsaved` / `confirmLeave` for the department switch.
- `src/levelgen/props/propsEdit.test.ts` - 21 tests (below).
- `docs/port/claude/outbox/T023/browser-check.mjs` - the headless-Edge driver (CDP, temp profile), `browser-check.log`, screenshots 01-13.

Changed
- `src/levelgen/props/rerollKinds.ts` - `RerollRequest.zone` (polygon, UE cm) + `RerollOutcome.zone` totals. In zone mode: placement via `rerollLayerInZone`; the 088 clearance and the validator
  drops judge ONLY the new objects (everything else is pinned for the clearance for the duration of the call and restored: `locked` flags are byte-identical afterwards); the power layer is skipped
  with a reason; leftover orphans (an object attached to a crate the zone removed) are named once at the end (layers re-rolled in the same call have already replaced theirs).
- `src/departments/PropsCanvas.tsx` - draws through `planTransform` + `viewTransform` (zoom / pan), `height` may be a CSS length, redraws on box resize; the old click-to-select handler is gone
  (picking is in the overlay now). Legend / info bar unchanged.
- `src/departments/PropsPanel.tsx` - rewritten around: history (`regenHistory`) of the props file, `RegionOverlay` (modes off / rect / poly / lasso, `pickLayer`, drag ghost, grid snap, issue rings),
  tool row («Двигать» / «Рамка» / «Многоугольник» / «Лассо», layer checkboxes, seed, «Перебросить в зоне»), zoom bar, `useFullWindow` + `FullWindowFrame` (header: undo / redo / save / dirty; drawer: layers +
  report), nudge keys (`nudgeDelta`, one coalesced undo step per run), Ctrl+Z / Ctrl+Y, validation card in the right column (debounced 350 ms; writes the gate verdict into the layers the edits touched),
  `beforeunload` + `unsavedGuard`. Save / conflict / frozen logic unchanged (PUT with `base_revision`).
- `src/departments/LevelGenDepartment.tsx` (tab switch asks), `src/TrainingGround.tsx` (department switch asks).
- `README.md` - one paragraph.

## Tests (`Tools/TrainingGround`)

| | before (T-022 report) | after |
|---|---|---|
| `npm test` | 558 tests, 558 pass | 579 tests, 578 pass, 1 fail (631 s) = 558 + 21 new. The one failure is `combatSim.test.ts` "a 10-min run took 1056 ms" (a wall-clock limit, unrelated code; it ran while the machine was loaded): the file alone passes 20 / 20 |
| `npm run typecheck` | clean | clean |

New tests (`propsEdit.test.ts`, the committed Depot layout read-only, props generated in memory, nothing written): zone re-roll keeps every object outside byte-identical and replaces the inside (crates and
barrels), deterministic per seed, keeps locked / pinned (`move` override) objects + locked layers + `pmc_charges` + other layers, the 088 clearance does not touch an unlocked crate OUTSIDE the zone that
sits on the route (and the temporary pin is gone), no new crate inside the corridor, power layer skipped, crates + mines + loot in one zone leave no orphan, crates alone name the orphans, frozen file refuses,
`zonePreview`; move: delta exact + input untouched + auto-lock + lock removable, a moved prop survives a re-roll of its layer, attached mine + override follow, refusals (frozen / locked layer / pmc / off the map /
zero / unknown id), walk-surface snap, nudge steps (10 cm / Shift 1 m / Ctrl 10 m, ArrowUp = +X) and ten nudges = ONE undo step, grid snap, generator cables; pick (nearest, hidden layer, walk-layer filter),
`checkProps` + `newIssues`, `withGate`, the unsaved guard.

## Browser check (headless Edge over CDP, temp profile, no screen takeover) - `docs/port/claude/outbox/T023/`

Dev server `vite --port 5197` from this worktree; map **`L_Gen_Test_X2`** (NOT X6; switched through the page's selector). `browser-check.log` has every number. Results:
- normal plan 800 x 618 px; wheel zoom 100 % -> 173 % (3 notches at the cursor), `+` 259 %, `-` `-` 115 %, middle-button pan, «Вписать» -> 100 % (02, 03).
- F11 -> full window (plan 1202 x 733, drawer «Слои и отчёт»), F11 -> back, F11 + Esc -> back, button «На весь экран» / «Свернуть» (04).
- drag of `crates_00000001_3` 2800,-4900 -> 1999,-3618, card shows «Заблокирован» (auto-lock) (05, 06); 3 x ArrowUp + Shift+ArrowRight -> 2029,-3518; the nudge run is ONE undo step (`Отменить (2)`), Ctrl+Z -> back to the drag
  result, Ctrl+Y forward again; the check card goes `ПРОВЕРКА…` -> `GATE: PASS`, 0 new problems (07).
- tab switch with unsaved edits -> the page asked (dialog text in the log), "stay" kept the edits.
- box zone: summary «заменится 47 ..., останется закреплённых 4», re-roll: removed 47, placed 53 (+1 refused by spacing at the border), outside untouched 122, 088 clearance removed 2 mines, validator 1 (08, 09);
  lasso zone and polygon zone (corners + Enter) drawn the same way (10, 11, 12); the polygon was drawn but not rolled.
- «Сохранить»: `Сохранено успешно (ревизия 2)` (13); the file on disk compared with the original: the dragged crate is at 2029,-3518 with `locked: true`; **104 of 104 objects outside the box + lasso zones are identical**,
  66 inside were replaced, 3 locked ones in the zones kept; `GET /api/levelgen/props` returns rev 2 and 172 objects (the same file).
- NOT exercised in the browser: the frozen-file / 409 conflict paths of Save (unchanged code), wall-clock timing on X4 / X6, wheel zoom inside the F11 window (same overlay), a prop dragged onto a roof.

## Restored (all confirmed)
- `Content/Data/LevelGen/props/L_Gen_Test_X2.props.json`: `git checkout` after the save; sha256 `0135826...ace0b4` equals the value before the run; `git status` shows no Content change.
- `Saved/TrainingGround/current_map.json` (git-ignored, the dev server's "current map"): was `L_Gen_Depot_1` (`at 2026-11-19T23:03:51.041Z`), the page switch wrote `L_Gen_Test_X2`; restored from a byte copy made before the run; sha256 `3d47ff80267d...37a1` identical, content `L_Gen_Depot_1` / `2026-11-19T23:03:51.041Z`.
- dev server + headless Edge killed; the Edge temp profile deleted.

## Manual checks for the designer (on `L_Gen_Test_X2` or a COPY, then `git checkout` the props file; never X6 first)
1. «Пропсы уровней»: wheel zoom at a cursor, `+` / `-`, middle-button pan, «Вписать»; F11 / Esc.
2. «Двигать»: drag a crate / barrel / mine / generator; the amber lock appears; arrows (10 cm / Shift 1 m / Ctrl 10 m); Ctrl+Z; «Заблокирован» removes the lock; drop a crate on a building / wall - the red ring and the list on the right.
3. «Рамка» / «Лассо» / «Многоугольник»: draw a zone, pick the layers, «Перебросить в зоне» (seed +1 each time); check outside the zone is unchanged; then «Сохранить» and read the diff of the props file.
4. Try to leave with unsaved edits (map selector, «Управление картой», another department, reload): each asks.
