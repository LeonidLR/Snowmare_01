# 035 - Nightly sweep: run EVERY registered smoke once on current main and report reds (Claude, 2026-11-16) - LIGHT tokens, long run
Why: today ~10 systems merged; a full sweep catches regressions no targeted gate covered.
Branch codex/035-nightly-sweep (from main AFTER 034 is merged - pull the newest main), worktree CodexTactics-agents.
Do: build; run all smokeRegistry names in batches: `smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands <batch of 8>`
(the script honours knownFail/knownFlaky - read each log for the real result). Paste ONE line per smoke (name | PASS/FAIL | seconds |
first FAIL line). Re-run each FAIL once alone (-Jobs 1) to separate load flakes from real reds. NO code changes, NO flag changes.
Report docs/port/codex/outbox/035-nightly-smoke-sweep.report.md: table + "real reds" list (failed twice) with the first FAIL line and the
most recent merge that touched that system (git log -1 on the smoke's command file + its main source). Token-cheap: do not read code
beyond the FAIL lines. Stop at the 10 % limit rule with a partial table.
