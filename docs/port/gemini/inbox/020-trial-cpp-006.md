# 020 - TRIAL C++ task (designer 2026-11-11): the game reads the per-fighter look-sweep toggle - ONLY after 016, 018, 019, 017 are done

The designer allowed ONE trial C++ task for you. Exception to "Gemini never edits C++" - valid ONLY for this task and ONLY these
paths: Source/CodexTactics/**/Formation/**, the formation_shapes.json reader (find it: grep "formation_shapes" in Source), and their
tests in Source/CodexTacticsTests/**. Nothing else in Source/, no .uasset, Config, maps, main, push.
Spec = docs/port/codex/inbox/006-formation-scan-toggle.md (Codex will NOT do it now; it is yours). Branch `gemini/020-trial-scan-toggle`
from `interim/2026-11-12` in YOUR worktree. Do NOT merge it anywhere - Claude / Codex review and merge it on Saturday.

Checks you must run and paste into the report docs/port/gemini/outbox/020-trial-scan-toggle.report.md:
- `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1` (your worktree; if the designer's editor blocks it, say so)
- `Scripts/test.ps1 -Filter CodexTactics.Formation` (new tests: scan off -> constant look over time; key absent -> unchanged)
- `Scripts/smoke_parallel.ps1 -Commands CodexTactics.FormationLabSmoke,CodexTactics.FormationCustomSmoke -Jobs 2`
- `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` (it may flag the C++ path - quote it; this task is the
  designer's exception)
Small commits; Epic coding standard; no new tickers. If anything is unclear, stop and write the question - never guess in C++.
