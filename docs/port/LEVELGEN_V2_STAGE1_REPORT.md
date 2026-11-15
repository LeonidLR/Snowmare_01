# Level generator v2 - stage 1: region re-roll + locked districts (task 070, board 066)

Branch `feat/levelgen-region-regen` (from main 9d95c444). TypeScript only (Training Ground); no C++ / .umap / .uasset, no edits to
`Content/Data/LevelGen/accepted/**` or `Content/Data/LevelJson/**`.

## How the designer uses it (TG -> «Генератор уровней» -> «Генератор раскладок»)

1. Get a map: «Сгенерировать», or click an accepted map in «Принятые» (opens it read-only via `GET /api/levelgen/open-accepted`).
2. Toolbar «Зона»: pick «Рамка» (drag), «Многоугольник» (click corners; double-click / Enter closes; Esc / right click cancels) or
   «Лассо» (drag a free outline). The zone is drawn amber; cyan dots = objects that will be re-rolled; green squares = locked ones.
   The line «В зоне: ...» counts them (re-rolled / locked / roads + canal). «Сбросить» clears the zone; «Просмотр» turns drawing off.
3. «Перегенерировать зону»: re-rolls the zone with the seed shown (auto +1 after each click; type a seed to reproduce a result).
   «повторов до N» = how many extra seeds to try if the route breaks. The report line shows removed / added / dropped / buildings kept
   old, retries, seed, route status; «Подробности» lists every dropped object with the reason and every rejected seed.
4. «Отменить (n)» / «Вернуть (n)»: undo / redo of re-rolls and lock edits (50 steps kept, minimum 20).
5. «Закрепить район»: the current zone becomes a locked district (optional name in «имя района»); shown green with a chip under the
   toolbar; «×» on the chip removes it. Locks live in the map JSON (`regen` block) and are written by the existing «Принять».

## Rules (src/levelgen/regionRegen.ts)

- Selection by CENTRE: building footprint centre (building = whole: its walls, roof/floor ladders, mantle edges), wall midpoint,
  cover / ladder / spawn / ambush position, zone / patrol centroid.
- Never re-rolled: locked groups (centre inside a lock) and infrastructure: streets `Z_ST_*`, the canal (`cut`, `Z_cut`, cut walls,
  crossings, cut ladders). Fresh objects that would overlap a lock, a kept object, or (buildings) a road / the canal are dropped.
- The fresh content comes from `generateLayout` with `skeletonSeed = layout.seed` (same streets + canal) and the click's seed for the
  blocks. Without `skeletonSeed` the generator is unchanged (test: identical hash for seed 1).
- A selected building whose fresh twin cannot be placed is kept as it was ("оставлено старыми") - no holes, links stay valid.
- Seams: routes are spliced at the zone border (old points outside, fresh points inside); if the spliced route fails but the old one
  passes on the merged map, the old one is kept. Gate = `checkLayoutRoutes` (the evaluator's own per-leg route check, now exported):
  the mandatory route must stay completable (option `gate: 'all'` for every route); else the attempt is rejected and retried with
  `seed + k * 7919` up to N times; all fail -> map unchanged, reasons listed.
- Heights: new walls / covers / partitions / parapets / mantle edges are checked against `obstacle_heights.json` classes and snapped
  with `standardObstacleHeight` when outside (test: `runObstacleHeightGate` on the added objects = no errors). No new heights.
- Schema: optional `regen: { version: 1, locks: [{ id, name, poly }], rerolls: [{ seed, zone, replaced, added, retries }] }` in
  `codex.levelgen_layout/1`. Absent = no locks (old files load); a newer version is refused by `validateGenLayout` / `regenStateOf`.

## Files

- `Tools/TrainingGround/src/levelgen/regionRegen.ts` (shapes, groups, select-by-centre, locks, splice, re-roll), `regenHistory.ts`,
  `planView.ts` (plan transform = LayoutView's), `regionRegen.test.ts`; `generate.ts` (`skeletonSeed`), `evaluate.ts`
  (`checkLayoutRoutes`), `schema.ts` (`regen` block + validation).
- `src/departments/RegionOverlay.tsx` (new), `LevelGenDepartment.tsx` (toolbar, history, locks, open accepted), `LayoutView.tsx`
  (redraw on resize so the overlay stays aligned), `server/levelgenApi.ts` (`GET /api/levelgen/open-accepted`).

## Verification

- `npm test` (Tools/TrainingGround): 374 tests, 374 pass, 0 fail (15 new in `regionRegen.test.ts`).
- `npm run build` (tsc + tsc node + vite): green.
- Determinism: Depot seed 1, zone X -6000..-2000 / Y 2000..9000, re-roll seed 101 twice -> sha256
  `12cdced443b95f1a02bbe085dd61d52cbc1b62cd09b189aae134858366f3692f` both times; seed 202 -> `5cf14ff5...8ab7` (different).
- Probe: 4 zones x 3 seeds on Depot seed 1 (incl. the whole map) -> 12/12 accepted, 0 retries, re-roll ~45 ms.
- Page check (dev server, 2026-11-16): `docs/port/screenshots/070-levelgen-zone-before.png`, `070-levelgen-zone-after.png`,
  `070-levelgen-lock.png`. The page run found a real bug (a lock clipping the ruin -> "unknown layer" on every retry); fixed by the
  building restore + route fallback, covered by a regression test.

## Not done / open

- Stage 2-3: canvas extension x2/x4/x6, start + goals, building in UE.
- The generator is a template (set-pieces in fixed regions + jitter): a re-roll mostly redraws covers, fences, openings and partitions;
  building footprints barely move. Swapping set-pieces per region is a generator change for a later stage.
- Fence segments crossing the zone border can be dropped (overlap with the kept neighbour) -> a gap in the fence; reported in
  «Подробности».
- A route that leaves and re-enters the zone is spliced from its first to its last inside point (one span).
- Locks are saved only by «Принять» (writes `accepted/<name>.json`); there is no separate "save locks" button.
