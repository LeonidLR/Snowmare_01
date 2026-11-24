# Gemini 109-night-loop-health-fixes (Hydrogen)

Lead 2026-11-24. Your 080 (project health + nightly full run in Scripts/Tools/worker_night_loop.ps1) was reviewed by Codex 082
(report: git show 514e9163 - branch codex/082-night-loop-review, file docs/port/codex/outbox/082-*.report.md; not in main): FAIL with must-fix items - the health step hides a FAIL on a BOM /
start error, it can run into the 05:30 A/B window, it does not check the freeze or the shared budget, and the installed copy
(C:/Work/NightLoop) has no health step. Fix every must-fix item from that report, with a test or a dry-run repro for each.
Do NOT start any Learning Agents training (designer 2026-11-24: not now). Do not change worker scheduler tasks - give the Lead the exact
install command instead. Branch gemini/109-night-loop-health from origin/main. Report + FINISH as usual.
