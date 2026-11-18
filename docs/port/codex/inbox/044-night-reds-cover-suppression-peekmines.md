# 044 - Night sweep reds: CoverCrouchSmoke, SuppressionSmoke, PeekMinesSmoke (Claude, 2026-11-18) - LIGHT, C++ / smoke setup
Branch codex/044-night-reds from the newest main, worktree CodexTactics-agents. 10 % RULE: at 10 % of your limit left - stop, commit what
you have, post the FINISH line with status "partial", wait for the reset.
Why: the worker's night sweep (2026-11-18 00:00-02:00, main 34a00a48; log Saved/Reports/worker-2026-11-18/sweep-20261118/sweep.log in the
main checkout) failed these three twice (also on the solo re-run). PauseBarrelOrderSmoke / TutorialSmoke are already fixed in main 4db03229,
LevelGenRamp / CorpseCost / FormationMantle / GeneratedPropsX6 belong to others - do NOT touch them.
Do, one commit per smoke, smallest correct fix, NEVER loosen an assertion:
1. CodexTactics.CoverCrouchSmoke - reproduce once (`$env:CODEX_SMOKE_REMOTE='off'`, `-Jobs 1`), read the first FAIL line, find the cause
   (game regression or stale smoke expectation after the 11-16/17 merges: first aid by wound tier, formation jitter, corner pick, mantling).
2. CodexTactics.SuppressionSmoke - same.
3. CodexTactics.PeekMinesSmoke - known lead: the Lake map has no props file (Content/Data/LevelGen/props/), so the mines the smoke expects are
   not there. Fix the smoke set-up (spawn its own mines / use a map that has a props file) - do not create props files for real maps.
Gate: build (Scripts/build.ps1 -Remote worker, on exit 2 local); each fixed smoke x2 PASS; Scripts/test.ps1 -Smart;
`python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex` PASS.
Report docs/port/codex/outbox/044-night-reds.report.md: per smoke the cause with file:line, the fix, game or smoke, gate numbers.
Allowed: Source/** (the fix), Source/CodexTacticsDev/Private/Debug/** (smokes), the report. No .uasset, Content/**, Config/**.
FINISH line in chat topic codex-044 (status | last commit | done | NOT done | needs Claude | gate | report).
