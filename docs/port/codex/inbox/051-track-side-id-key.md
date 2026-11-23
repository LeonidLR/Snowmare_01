# Codex 051 — round track: key events by (side, id), not id alone

Light task (Python only). Branch `codex/051-track-side-id-key` from main, your own checkout. Do not push, Lead merges.
Stop at 10 % of your limit: commit, report, wait (worker-limit rule).

## Problem

In PMC arena round tracks two squads often have operatives with the SAME names: 199 of 300 rounds on seeds 1060-1359
(enemy-AI session, 2026-11-23). Tools that join track events by the operative id alone mix the two sides: deaths, shots
and positions of one side get attributed to the other. The enemy-AI session fixed its own scratch script
(`zc_metric.py`); the fix moved a metric from 0.13 -> 0.41 to 0.23 -> 0.65 shots/s - same conclusion, wrong numbers.

## Do

1. `Scripts/Tools/pmc_round_trace.py` (Gemini 089) and `Scripts/Trainer/nighttrainer/track_stats.py`: find every place
   that keys, groups, joins or looks up track events / per-operative state by id (name) only. Use `(side, id)` instead
   (side = the team / squad field the track already carries - check the track format in the arena JSON / track files).
2. Any output that prints an operative should show the side too (e.g. `A:Rifleman_2`).
3. Tests: add a unit test per script with a synthetic round where both sides have an operative with the same id and
   events interleave; assert the per-side results are separated. Existing tests must stay green
   (`python -m pytest` or the scripts' own test runner - see how existing tests in Scripts/ run).
4. Re-run both scripts on one real track before and after (any `*.json` under `Saved/Logs/SeedBatch/` or a fixture);
   put the before / after difference in the report.

## Done

Report `docs/port/codex/outbox/051-track-side-id-key.report.md`: files changed, every keying site fixed (file:line),
test names + result, before/after numbers on a real track. Then one chat line:
`agent_chat.py post --as codex --to claude --topic codex-051 --text "051 FINISH | PASS/FAIL | <hash> | ..."`.

Forbidden as always: Content/Maps/**, Config/**, Content/VFX/**, .uasset, CLAUDE.md, branch main.
