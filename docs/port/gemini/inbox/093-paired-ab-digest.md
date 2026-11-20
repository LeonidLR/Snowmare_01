# 093 - honest A/B numbers for enemy-AI changes: win rate, paired seeds, intervals (TOOL) - LIGHT, Python
Limit rule: at 10 % of your daily limit left: stop, commit (WIP is fine), handover at the top of the report, FINISH line.
Base: origin/main newest. Branch gemini/093-paired-ab-digest. Worktree CodexTactics-gemday (after 092, or alone if 092 waits).
Router: L2 (ue-l2-routine class).

## Design gate (lead filled)
- Spec: the Fable review of T-05 (docs/port/claude/outbox/WORKER_LEAD_2026-11-20.report.md, section "Fable review" + "Process
  changes adopted"): reports must always show the WIN RATE, the SEED SETS, all baseline cycles and INTERVALS; accept on held-out
  seeds; a paired A/B (same seeds, main vs branch).
- Acceptance: one command compares two result folders (e.g. main vs a branch, same seeds) and prints per scenario: defender wins
  A / B of N, attacker objective captures, attackers killed per round, share of rounds with >= 2 attackers down, paired discordant
  counts (A-only wins / B-only wins) + an exact McNemar / sign-test p, medians of on_pos / idle_max / crossfire with a bootstrap 95 %
  interval of the median and of the difference, the seed set of each side (and a loud WARNING when the seed sets differ), and an
  optional `--tune-seeds 1-10` split that prints tune vs held-out separately.
- Proved by: unittest file Scripts/Tools/test_dayloop_digest.py (new or extended) on small fixture summary files: identical
  folders -> 0 discordant, p = 1; a hand-made 5-seed pair with known wins -> exact counts; different seed sets -> the warning;
  bootstrap with a fixed RNG seed is deterministic.
- Designer decision needed? none.

## Evidence so far
- Existing: Scripts/Tools/dayloop_digest.py (per-seed T-04 metrics, medians, criteria misses, worst 3 seeds) and
  Scripts/Tools/worker_seed_batch.ps1 (summary.json / summary.txt per run). The stand-in lead used an ad-hoc paired_wins.py on the
  worker (not in the repo) for the numbers in the report's tables "T-05 paired A/B" and "T-08 first slice" - your output on those
  folders should reproduce them if you can get the folders (ask Claude; otherwise fixtures only).
- Night loop results: C:\Work\CodexTactics\Saved\Worker\nightloop\<date>\ on codex-worker (read-only for you).

## Task
1. Read dayloop_digest.py and the summary format worker_seed_batch.ps1 / the night loop writes. Extend the digest (or add
   `Scripts/Tools/paired_ab.py` that reuses its parsing) - no copy-pasted parser.
2. Standard library only (no numpy / scipy): exact binomial for the sign test, percentile bootstrap (2000 resamples, `--rng-seed`).
3. Short usage section in the script docstring + one line in docs/port/LANES.md or the T-05 brief pointing to it.
4. Stop rule: > ~3 h -> commit WIP + exact next step.

## Never
- Change the game / C++ / smoke code or the summary format the engine writes. Push or merge main. Commit Content/Data.

## Gate (all before FINISH)
`python -m unittest Scripts/Tools/test_dayloop_digest.py` (+ the existing lane_watchdog tests still pass) |
`python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` | `git diff --check`.
Allowed paths: Scripts/Tools/dayloop_digest.py, Scripts/Tools/paired_ab.py, Scripts/Tools/test_*.py, docs/port/gemini/**,
docs/port/LANES.md (one line).

## Report
`docs/port/gemini/outbox/093-paired-ab-digest.report.md`: handover, usage example with output, gate results. FINISH in chat topic
`gemini-093`: `093 FINISH | <status> | <commit> | done: ... | NOT done: ... | gate: ... | report: <path>`.
