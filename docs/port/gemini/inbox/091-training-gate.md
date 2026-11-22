# 091 - No night run on dead metrics; the night machine runs scenario regression (smart enemies T-10)
Dispatched by the lead 2026-11-19 (designer approved T-01..T-12: "Да, передавай").
The full task (design gate, evidence, task, never, gate, allowed paths) is section **T-10** of `docs/design/gdd/smart-enemies-roadmap.md`
(section 6 "Tasks"), plus the "Common to every task" lines just above T-01 there. Read the GDD
`docs/design/gdd/smart-enemies.md` parts that section names. That text is binding; this file only fixes the number and branch.
Branch `gemini/091-training-gate`. Worktree `CodexTactics-gemday`. Base: origin/main newest.
Limit rule: at 10 % of your limit left: stop, commit (WIP is fine), handover at the top of the report, FINISH line.
Report: `docs/port/gemini/outbox/091-training-gate.report.md`; FINISH line in chat topic `gemini-091` (TASK_TEMPLATE format).
Starts after 089 (was: after 090). PLAN first.
Refresh 2026-11-20 (lead): order now 089 -> 091 -> 094 (smoke repeats) -> 090 (designer: «Да, передай»). Facts since the brief: 088 merged;
T-03 scripted opponent + T-04 position metrics are in main (PmcCrossroadsSmoke / PmcAlleySmoke play the scripted opponent); G011 fixed
the arena's dead post_moves / overwatch_kills counting (main 9726082b); paired A/B tool Scripts/Tools/paired_ab.py (your 093) - use it
for any before / after numbers; T-05 / T-08 are NOT in main (worker/* branches, ships off). Push note: the pre-push guard refuses
gemini/* changes outside its allowlist (Scripts/ is outside) - commit locally, FINISH in chat, Claude merges; never edit the hook. Worker-ops installs the schedule after the merge.
Scope addition (designer 2026-11-19, roadmap section 5a "day loop + night regression"): `scenario_regression.py` also gets a quick DAY mode -
`--scenario X --seeds 20..50`, target < ~15 min wall, short digest: pass rate, the worst 3 seeds, one failed seed's decision trace. The night
mode (16:30-08:00 on the worker: regressions since yesterday, stable, flaky seeds) stays as section T-10 describes.

## Allowed paths
- `Scripts/Trainer/**`
- `Scripts/Tools/scenario_regression.py`
- `Scripts/smoke_parallel.ps1`
- `docs/tools/night-trainer.md`
- `docs/port/gemini/**`
- `gemini`
