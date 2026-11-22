# 096 - Three small lab layouts that reproduce the designer's screenshots (smart enemies T-02, layouts part) - MEDIUM, TS / data
Limit rule: at 10 % of your daily limit left: stop, commit (WIP is fine), handover at the top of the report, FINISH line.
Base: newest main. Branch gemini/096-lab-layouts. Worktree CodexTactics-gemday. PLAN first in chat topic `gemini-096`.
Router: L2-L3. Push may be refused by the pre-push guard - expected; commit locally, Claude merges.

## Design gate (lead filled)
- Spec: section **T-02** of `docs/design/gdd/smart-enemies-roadmap.md` (read it in full: L_Lab_Crossroads, L_Lab_Alley, L_Lab_Bridge,
  each <= 80 x 80 m, exact contents listed there). Changes vs that text (lead 2026-11-21): YOU do the layouts + data files;
  CLAUDE builds the Unreal maps (step 2 of T-02) - you never run build_levelgen_map.py / the editor.
- Acceptance: three accepted layouts `Content/Data/LevelGen/accepted/gen_lab_crossroads.json`, `gen_lab_alley.json`,
  `gen_lab_bridge.json` (+ `.meta.json`), authored with the Training Ground's own data model / CLI (hand-placed, locked buildings;
  look how other accepted layouts and their meta look, and at the TG "Генератор уровней" code under Tools/TrainingGround/src/levelgen),
  their exports `Content/Data/LevelGen/{tactical,objectives,props}/L_Lab_*.json` and `Content/Data/LevelJson/L_Lab_*.json`, made
  with the same export scripts other maps use (Tools/TrainingGround/scripts/*). The TG validator is green on all three
  (whatever validate / test command the TG uses for accepted layouts). If the generator data model cannot express an element
  (walled enclosure with one gap, parapet, ladder >= 7 rungs, drop-rule cut), ship the layout without it and list the missing
  module in the report (stop rule of T-02).
- A vitest test that loads the three layouts and checks the T-02 acceptance numbers (sizes, cover count, start distance 60-80 m,
  ladder rungs, goal placement) on the data.
- Designer decision needed? none (OQ-17 decided 2026-11-19: lab maps + generator arenas).

## Never
Touch any existing map, accepted layout or export (X2 / X4 / X6, Depot, BgenPlans); run the editor or build maps; Content/Maps.

## Gate
`npm test` + typecheck in Tools/TrainingGround | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` | `git diff --check`.
Allowed paths: `Content/Data/LevelGen/accepted/gen_lab_*`, `Content/Data/LevelGen/{tactical,objectives,props}/L_Lab_*`,
`Content/Data/LevelJson/L_Lab_*.json`, `Tools/TrainingGround/src/**/*.test.ts` (one new test file), `docs/port/gemini/**`.

## Report
`docs/port/gemini/outbox/096-lab-layouts.report.md` (Russian): per map what is in it (ASCII sketch), what the data model could not
express, the exact command for Claude to build each map.
FINISH in topic `gemini-096`: `096 FINISH | <status> | <commit> | done: ... | NOT done: ... | needs Claude | gate: ... | report: <path>`.
