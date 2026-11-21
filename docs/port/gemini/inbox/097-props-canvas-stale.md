# 097 - Props tab shows another map's props when the current map has no props file (TG bug) - LIGHT, TS
Limit rule: at 10 % of your daily limit left: stop, commit (WIP is fine), handover at the top of the report, FINISH line.
Base: newest main. Branch gemini/097-props-canvas-stale. Worktree CodexTactics-gemday. PLAN first in chat topic `gemini-097`.
Router: L2.

## Design gate (lead filled)
- Bug (designer 2026-11-20, screenshot): in the Training Ground "Генератор уровней" -> «Пропсы» tab, after switching to a map that has
  NO props file, the canvas still shows the props of the previously opened map; moving a crate and pressing «Сохранить» then failed
  with "Invalid props map name" (map=""). The save part is already fixed in main 4163d7f2
  (`Tools/TrainingGround/src/departments/PropsPanel.tsx` handleSave: target = selectedMap || propsFile.map, confirm dialog when they
  differ). What is NOT fixed: the stale canvas itself.
- Acceptance: when the selected map changes, PropsPanel drops the old props file state; a map without a props file shows an empty
  canvas with a clear Russian hint («У карты <name> нет файла пропсов - добавьте пропс, чтобы создать его») and saving creates
  `Content/Data/LevelGen/props/<map>.props.json` for THAT map; a map with a file loads its own. No request goes out with map="".
- Proved by: a vitest/RTL test (or the TG's existing component-test style) for: switch A(with file) -> B(no file) -> canvas empty,
  hint shown; save on B targets B; switch back to A loads A. Plus a manual browser check on `npm run dev` with a screenshot in the report.
- Designer decision needed? none.

## Never
Change the props file format; touch other departments' behaviour; Content/Data files of real maps (use fixtures / a temp map name).

## Gate
`npm test` + typecheck in Tools/TrainingGround | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` | `git diff --check`.
Allowed paths: `Tools/TrainingGround/src/departments/PropsPanel.tsx`, related hooks/api files under `Tools/TrainingGround/src/**`
it needs, one new test file, `docs/port/gemini/**`.

## Report
`docs/port/gemini/outbox/097-props-canvas-stale.report.md` (Russian): cause (file:line), fix, test output, screenshot path.
FINISH in topic `gemini-097`: `097 FINISH | <status> | <commit> | done: ... | NOT done: ... | needs Claude | gate: ... | report: <path>`.
