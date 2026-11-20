# T-022 report - Training Ground «Управление картой»: bigger plan, F11 full window, moving map elements

Branch `worker/t022-tg-map-edit` (base origin/main 19a90f52). TypeScript only (`Tools/TrainingGround`), no C++, no Content/Data, no .uasset / .umap.

## Handover (read first)

- **Done:** section 1 (bigger plan + F11 full window + shared helper for T-023) and section 2 (diagnosis, drag-move of buildings / fences / covers /
  bridges / ladders, wall end handles, nudge keys, grid snap, undo / redo, validation after every move, dirty flag + confirms, stale-files notice).
  Unit tests (17 new) + browser check (headless Edge, screenshots in `docs/port/claude/outbox/T022/`).
- **Left / not done:** (1) the designer's own hands-on check on the real map (X6) - not done by me (never touched X6, never pressed «Принять»);
  (2) building ROTATION by a gesture (the rotation is kept on a move; the old rotate buttons still work for covers / free walls only);
  (3) moving the props / the tactical map WITH a moved building (only reported as stale, never regenerated silently - brief);
  (4) zones / roads / canal / mantle edges / routes are still not movable (the brief did not ask; table below);
  (5) T-023 not started (brief: after T-022).
- **Next exact step:** the lead reviews the diff, the designer plays with it on a copy of a map (steps at the bottom), then T-023 starts from
  `useFullWindow` + `FullWindowFrame` (see "Shared helper for T-023").
- `docs/port/HANDOFF.md` is outside my allowed paths (`docs/port/claude/**`): the lead should add one line there ("T-022: map page F11 + move tools,
  `levelgen/mapEdit.ts`, `mapChecks.ts`, permission of the move tool in `layers.ts` MAP_MOVE because layers.json is not mine to change").

## 1. Diagnosis: what the pencil tool could move BEFORE the change

`editLayout` + `pickLayer` (`layers.ts`, `LevelGenDepartment.tsx`), measured by calling `editLayout(..., { op: 'move' })` on every entity of the accepted
depot maps (`gen_ref_01_02_depot_1`, `_x4`; layers.json `edit` per layer in brackets):

| Entity type (layer) | layers.json `edit` | Moved by the pencil before? | Why |
|---|---|---|---|
| cover (obstacles_large / medium / small) | select, move, delete, add, reroll | **yes** (all 100 % of them) | `editLayout` move: `cover` |
| wall / fence / curb (obstacles_*) | same | **yes** - ONE segment only (fence rings broke apart), no end points, no mantle edges | `editLayout` move: `wall` |
| prop (mines / loot / special) | select, move, delete, add | yes, via `editProps` (props file) | not shown on the map panel (T-020) |
| objective (start / goals) | select, move, delete, add | yes, by the objectives overlay (`moveObjective`), not by the layer tool | own tool |
| **building** (buildings) | select, delete, reroll | **no** - `слой не поддерживает действие move` | no `move` in layers.json; no mover for the many parts of a building |
| wall of a building (buildings; kind building / interior / partition / parapet) | same | **no** (same refusal) | |
| **ladder** (buildings / canal) | select, delete, reroll / select | **no** | |
| **crossing** bridge / ramp (bridges) | select | **no** | |
| zone (roads, blocks), cut + cut_wall (canal), mantle_edge (mantle), route / point (routes), analysis layers | select / none | no | not requested |

Also missing for everything: keyboard nudge, grid snap, a drag ghost of the real shape, Ctrl+Z, checks after a move, dirty flag.

## 2. What changed (file:line, current tree)

New files
- `src/levelgen/mapEdit.ts` - the move operations (pure, input never changed): `moveBuilding` (whole building: footprint + rooms, openings, doors, partitions,
  parapets, hatches, stairs, holes, floors_detail, mantle edges; its walls `building===id`; its ladders; top-level mantle edges; route points on its
  floors / roof; goals `building===id`; ambush / spawn points inside the footprint; rotation kept; refused outside the map bounds; a ladder joining two
  buildings stays, covers inside stay - both reported in the message), `moveWalls` (+ `family`: the whole fence ring `F_<ring>_<side>_<n>`, mantle edges that name
  the wall follow), `moveWallEnd` (end handle, >= 50 cm), `moveCover`, `moveLadder` (building constraint `ladderAttachProblems`: stays inside the footprint
  (hatch ladder) or within 4 m of the outline, top stays on the roof / floor), `moveCrossing` (must still overlap the canal), `nudgeDelta` (10 cm / Shift 1 m /
  Ctrl 10 m), `snapMoveDelta` / `snapReference`, `SNAP_STEPS_CM` (0.5 m, 1 m, 5 m, 10 m = the plan grid).
- `src/levelgen/mapChecks.ts` - `mapIssues` (building on building, off the map, on the canal walls (same band as `validateGenLayout`), detached ladder / bridge,
  cover in a building / on a wall / on a cover), `validateMapEdit` (= mapIssues + the schema's `validateGenLayout`, what the accept path refuses),
  `newProblems(before, after)` (a defect the generator already had is not blamed on the move; shipped maps have 1 overlap + ~30 cover warnings).
- `src/utils/fullWindow.ts` + `src/components/FullWindowFrame.tsx` - the shared full-window helper (below).
- `src/levelgen/mapEdit.test.ts` - 17 tests.

Changed
- `src/levelgen/layers.ts`: `MAP_MOVE` / `layerMovable` / `movableTypes` (~l.55-70) - the move permission of the layers that had none, in CODE (layers.json is the
  designer's data file and I may not commit Content/Data: buildings -> building + ladder, bridges -> crossing); `LayoutEdit` gets `move.family` and `move_end`;
  `editLayout` move branch (~l.425-460) now dispatches to mapEdit, a click on a building wall moves the building; `pickMoveEntity` (~l.357).
- `src/levelgen/regenHistory.ts`: `historyPushCoalesced` (a run of arrow keys on one entity = ONE undo step); undo / redo drop the coalesce key.
- `src/departments/RegionOverlay.tsx`: drag ghost of the real shape + delta label, grid snap (`snapStep`), wall end handles (`handles`, `move_end`), `height` may be a CSS length.
- `src/departments/LayoutView.tsx`: `issueMarks` (red / amber squares), redraw on height change (not only width), `height` number | string.
- `src/departments/LayersPanel.tsx`: «Выбрать / двигать» for the new move layers + a hint line with the nudge steps.
- `src/departments/LevelGenDepartment.tsx`: bigger plan (`showRef` default off, height = viewport - 260, >= 600 px, `planHeightFor`); F11 / «На весь экран» via
  `useFullWindow` + `FullWindowFrame` (header: Свернуть, floor bar, undo / redo, «Принять», «не записано»; the layers panel is the drawer «Слои»); snap / «весь забор» /
  nudge controls; Ctrl+Z / Ctrl+Y / arrows (`nudgeKey`); `applyLayout` checks every move (`newProblems`) and prints "ПРОБЛЕМЫ после перемещения: ..."; markers + banner
  `mapBanner` (dirty flag, new problems since the layout was opened / accepted, stale notice); «Принять» asks before accepting NEW errors / wall conflicts; `changeMap`
  and `openAccepted` ask before dropping unsaved layout edits; `beforeunload` guard; the route check after a move / nudge / undo is deferred 350 ms (it costs 2-8 s on the
  big maps; «Принять» waits for it: `Candidate.evStale`).
- `server/levelgenApi.ts`: the accept response now carries `derived` { props: regenerated | created | kept_hand | error, objectives, tactical } (the page's stale notice).
  Nothing else of the accept path changed.
- `README.md`: one paragraph.

### Persistence (existing path, unchanged)
The move changes the candidate layout in memory (history step). «Принять» writes `accepted/<name>.json` + meta (as before); «Собрать в Unreal» builds from it (task 072).
The accept server ALSO already rewrites the tactical map, the objectives file and the props file when it is `source: "auto"`: after «Принять» the page says which of these were
regenerated and that a hand-made props file was NOT (with the number of its props standing on moved buildings), that the PMC plan is not recomputed (switch the «ЧВК»
layer on and accept again) and that the Unreal map needs «Собрать в Unreal». Nothing is regenerated by the page itself.

## Shared helper for T-023
- `src/utils/fullWindow.ts` (pure): `fullWindowAction(key event, { enabled, full, escExits })` -> 'toggle' | 'exit' | null (F11 only when the page is active and the focus is not in a
  text field; a `<select>` / checkbox keeps F11 working), `isTypingTarget`, `isKeyConsumer` (for the page's own shortcuts), `planHeightFor(viewport, reserved, min)`.
- `src/components/FullWindowFrame.tsx`: `useFullWindow(enabled, escExits)` -> `{ full, toggle, setFull }` (F11 preventDefault, Esc, locks the page scroll) and
  `<FullWindowFrame full onExit header drawer drawerTitle>` - it keeps the children in the same React position (only classes change), so a canvas is not re-created.
  T-023: `const { full, toggle, setFull } = useFullWindow(true)`, wrap the PropsCanvas + its toolbar in the frame, pass the layers / settings as `drawer`.

## Tests (`Tools/TrainingGround`)

| | before (clean 19a90f52) | after |
|---|---|---|
| `npm test` | 541 tests, 541 pass, 0 fail (433 s) | 558 tests, 558 pass, 0 fail (586 s) - 541 + 17 new |
| `npm run typecheck` (`tsc` + `tsc -p tsconfig.node.json`) | clean | clean |

New tests (`src/levelgen/mapEdit.test.ts`, fixtures generated in memory from the depot reference + the shipped accepted maps read-only; nothing is written):
whole-building move with all its parts (4 buildings, every coordinate pair of the building shifted by exactly the delta, walls / ladders / mantle edges of OTHER
buildings untouched, input not mutated, move-back = original), carried route / goal / ambush points + a ladder between two buildings stays + covers stay, a ROTATED building
(re-roll with rotation) moves rigidly and keeps its rotation, fence segment vs whole ring vs mantle edges vs building wall refused, wall end points (min length, bounds),
cover move, ladder constraint, bridge must still cross the canal, the move permission per layer (layers.json untouched: `buildings` has no `move` there), pick of a building by
its wall, nudge steps, grid snap, undo / redo (a 10-key nudge run = one step, undo breaks the run), validation flags an overlap as a NEW problem (building on building, cover on a
wall / in a building, torn-off ladder) and does not blame old defects, the checks on all shipped maps (< 1.5 s each), F11 / Esc rules, plan height.

## Browser check (done, headless Edge via CDP; no screen takeover, temp profile)
Dev server from this worktree (`vite --port 5199`), map `L_Gen_Depot_1` (NOT X6; the server's current map was X6, I switched through the page's selector; nothing accepted /
built / saved, only the in-memory layout moved). Screenshots + log: `docs/port/claude/outbox/T022/` (`browser-check.log` has the numbers).
- bigger plan: 782 x 690 px in a 1600 x 950 window (was ~470 x 520 next to the reference image);
- F11 -> full window (plan 1204 x 780, drawer «Слои», zoom 173 % kept in and out), F11 again -> back, F11 + Esc -> back; screenshots 05-07;
- building B_hall_n dragged with its walls (message lists what moved), new problems flagged, Ctrl+Z undoes (03, 04); 3 x Shift+Arrow = ONE undo step; snapped drag lands the
  centre on the 1 m grid and the overlap with B_office_s is marked red (04b); the checkbox keeps focus and the arrows still work;
- fence F_compound_N_0 dragged INSIDE the full window (08: end handles visible), undone (09).
Not exercised in the browser: wall end-handle drag release, bridge / ladder drag, «Принять» confirm dialogs (covered by unit tests or by reading only).

## Manual checks for the designer (on a COPY or L_Gen_Depot_1, then `git checkout` the file; never X6 before this passes)
1. «Управление картой»: the plan fills the centre column; F11 -> whole window, wheel zoom, F11 / Esc -> back, zoom unchanged.
2. Pencil on «Здания / структуры»: drag a building (also by its wall): ghost + metres; the stairs / doors / ladder go with it; Ctrl+Z; arrows (10 cm, Shift 1 m, Ctrl 10 m).
3. Drag it onto another building: red marker + «Новые проблемы»; «Принять» asks "Всё равно принять?".
4. Pencil on «Преграды ...»: drag a fence (tick «весь забор» for the ring), drag an end handle, drag a crate; «Мосты»: move a bridge along the canal (refused when off the canal).
5. Tick «сетка»: the centre lands on the grid. Switch the map while edited: the page asks. «Принять» on a copy: the blue note lists what is stale; «Собрать в Unreal» builds.
