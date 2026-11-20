# T-022 - Training Ground «Управление картой»: bigger plan + F11 full window, move buildings / fences / map elements (исполнитель: субагент Sonnet)
Designer 2026-11-19: «Карту расширить и добавить возможность при F11 максимизировать на все окно и разворачивать обратно.
Кроме того, добавь возможность внутри карты передвигать определенные здания, заборы и другие элементы карты внутри самой
карты, чтобы потом это отображалось в Unreal.» TS only (Tools/TrainingGround). Branch `claude/tg-map-edit`, worktree
`CodexTactics-claude` (create the branch from origin/main). Status lines: chat topic `claude-T022`. Base includes T-020
(current map, «Управление картой», src/departments/LevelGenDepartment.tsx, LayoutView.tsx, LayersPanel.tsx, levelgen/layers.ts).

## 1. Bigger plan + F11
- The plan (LayoutView in «Управление картой») today is a ~470 px wide box (designer's screenshot); make it use the free width
  of the centre column and a taller default height (fill the viewport height minus the toolbars, min ~600 px).
- **F11** (and a toolbar button «На весь экран» / «Свернуть») toggles the plan to fill the whole browser window (fixed overlay
  with the plan, its zoom / floor / layer controls and the layers panel as a collapsible side drawer); F11 again or Esc
  returns. preventDefault F11 so the browser does not go into its own fullscreen; only when the map page is active and focus
  is not in a text input. The view (zoom / pan) is kept across the toggle; "Вписать" fits to the new size.

## 2. Move map elements, persisted -> Unreal
- First diagnose what the layers panel's pencil tool (active layer: select / move / add / delete; `editLayout`, `pickEntity`,
  `onLayerAction`) can already move, per entity type (zone, building, wall/fence, cover/obstacle, crossing, ladder, mantle_edge,
  objective, prop). Report the table.
- Make these movable by drag on the plan when their layer is active: **buildings** (the whole building with everything that
  belongs to it - footprint, storeys, openings, roof items, its ladders / stairs; rotation kept), **fences / walls** (a whole
  segment; endpoints draggable to change length if cheap), **obstacles / cover** (large / medium / small), **bridges / crossings**,
  **ladders** (with their building constraint). Arrow keys nudge the selection (Shift = 1 m, plain = 10 cm... pick sane steps
  and show them); grid snap optional (toggle, the layout grid). Undo / redo through the page's existing history (regenHistory).
- Validation after every move: re-run the page's existing checks (layoutValidator, wall conflicts, route / evaluation as the
  page does after a zone regen) and show problems the way the page already does; never silently accept an overlap.
- Persistence: the edit changes the candidate layout in memory; «Принять» writes the accepted layout
  (Content/Data/LevelGen/accepted/<name>.json, the existing accept path) and «Собрать в Unreal» builds the map from it (existing
  path, task 072) - that is how the change reaches Unreal. Mark the plan dirty and confirm before switching maps with unsaved moves.
  Props / tactical / objectives files that depend on positions: after «Принять», tell the user which derived files are now stale
  (props on a moved building, tactical spots) and offer the existing regeneration buttons; do not regenerate them silently.
- DATA RULE: the designer has uncommitted edits to the X6 files in the main checkout; your tests use fixtures / temp dirs only,
  never Content/Data files; do not commit any Content/Data change.

## Gate
`npm test` + `npm run typecheck` (report counts); unit tests for the move operations (building moves with its children,
fence segment move, nudge step, undo/redo, validation flags an overlap) on fixtures; run the dev server from the worktree and
check in a browser (F11 in and out, drag a building and a fence on a fixture / non-X6 map, undo) - screenshots to
docs/port/claude/outbox/T022/. Do not press «Принять» on X6. `python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude`.

## Never / allowed
Never: C++, Content/Maps, Config/DefaultScalability.ini, Content/VFX, Content/Data commits, push / merge main.
Allowed: `Tools/TrainingGround/**`, `docs/port/claude/**`. Report: `docs/port/claude/outbox/T022-tg-map-edit.report.md`.

## T-023 (same executor, AFTER T-022; designer 2026-11-19, repeated request) - «Пропсы уровней»
Designer: «На закладке пропсы уровней добавить возможность zoom in, zoom out, панирование. Также максимизировать окно на F11.
... добавить возможность выбора при помощи полигона, лассо, бокс-селекта в определенной зоне генерировать другую раскладку
пропсов. Добавить возможность переставлять определенные пропсы внутри карты, чтобы они потом точно так же отображались в Unreal.»
- PropsCanvas (src/departments/PropsCanvas.tsx): zoom in / out (wheel at the cursor + buttons + %), pan (middle button / space +
  drag, like the map page's PlanView), «Вписать»; F11 full window + Esc / F11 back - SHARE one fullscreen helper with T-022.
- Zone re-roll: box / polygon / lasso selection (REUSE the map page's RegionOverlay / regionRegen selection code - same tools,
  same look) -> «Перебросить в зоне»: re-roll the chosen layer(s) (or all unlocked) ONLY inside the zone with a new seed; outside
  objects untouched; locked layers / locked objects / pmc_charges untouched; 088 route clearance + validator applied after.
- Move props: drag a prop (crate, barrel, turret, mine, loot, generator / power) on the canvas; arrow-key nudge; snap to the
  walk surface / floor it stands on; mark it locked=true by default after a manual move (so a re-roll does not move it back;
  the lock is visible and can be removed). Validation (validator.ts / fastGate) after the move, shown in the report panel.
- Persistence: the props file is saved with the existing «Сохранить» (revision check, PUT /api/levelgen/props); the game reads
  props from that JSON at OnWorldBeginPlay (no map rebuild) - that is how moves reach Unreal. Confirm before leaving with unsaved edits.
- Gate: unit tests (zone re-roll keeps outside + locked, move + auto-lock, nudge) on fixtures; browser check on a non-X6 map
  (zoom / pan / F11 / box + lasso re-roll / drag + save on L_Gen_Test_X2 then `git checkout` it back); screenshots to
  docs/port/claude/outbox/T023/. Report: docs/port/claude/outbox/T023-tg-props-edit.report.md (or one report for both).
