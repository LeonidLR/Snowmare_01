# 093 Plan: Paired A/B Digest (Win rate, paired seeds, intervals)

## 1. Context & Objective
Following the Fable review of T-05 (`WORKER_LEAD_2026-11-20.report.md`), all enemy-AI evaluation reports must provide honest numbers on paired seeds:
1. Defender win rate (A and B of N)
2. Attacker objective captures
3. Attackers killed per round
4. Share of rounds with >= 2 attackers down
5. Paired discordant counts (A-only wins / B-only wins) + exact two-sided McNemar / sign-test p-value
6. Medians of T-04 defence metrics (`on_pos`, `idle_max`, `crossfire`) with 95% bootstrap confidence intervals for the medians and for their difference
7. Seed sets comparison with a loud WARNING when seed sets differ
8. Optional `--tune-seeds 1-10` split reporting tune vs held-out subsets separately
9. Standard library only (no numpy / scipy)

## 2. Files
- `Scripts/Tools/dayloop_digest.py`:
  - Enhance `row()` to extract `seed`, `scenario`, `def_side`, `atk_side`, `def_win`, `atk_capture`, `att_killed`, `att_down`, `att_down_ge2` while preserving 100% backward compatibility.
  - Extract `load_folder()` helper to load all result JSONs and summary states.
  - Support `--compare <folder_b>` option delegating to `paired_ab`.
- `Scripts/Tools/paired_ab.py` (new):
  - Imports `load_folder`, `row`, `defender_side` from `dayloop_digest.py` (no copy-pasting).
  - Exact McNemar test: `exact_mcnemar_p(b, c) = min(1.0, 2.0 * sum(comb(n, i) for i in range(k+1)) / 2^n)` where $n = b+c$, $k = \min(b,c)$.
  - Percentile bootstrap: 2000 resamples with `--rng-seed` (default 42) for medians of A, B, and B - A.
  - Formatter for seed sets and seed ranges (`1-10, 15, 20-30`).
  - Loud warning if seed sets differ.
  - CLI parser with `--tune-seeds`, `--rng-seed`, `--resamples`, `--json`.
- `Scripts/Tools/test_dayloop_digest.py` (new):
  - Unittest suite covering:
    1. Identical folders: 0 discordant, p = 1.0, diff = 0.
    2. Hand-made 5-seed pair with known wins: exact verification of win rates, captures, kills, >= 2 down, discordant 2 vs 1, exact p.
    3. Different seed sets: loud WARNING verified, only common seeds compared.
    4. Deterministic bootstrap: fixed RNG seed produces identical output.
    5. Tune vs held-out split: `--tune-seeds` correctly partitions and calculates subsets.
    6. Standalone dayloop digest backwards compatibility.
- `docs/port/LANES.md`:
  - Add one line describing `paired_ab.py`.
- `docs/port/gemini/outbox/093-paired-ab-digest.report.md`:
  - Handover, usage examples, gate verification, and final FINISH line.

## 3. Verification Gate
1. `python -m unittest Scripts/Tools/test_dayloop_digest.py`
2. `python -m unittest Scripts/Tools/test_lane_watchdog.py`
3. `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini`
4. `git diff --check`
