# 093 report - Paired A/B digest: win rate, paired seeds, intervals (TOOL)

Branch `gemini/093-paired-ab-digest` (base `origin/main` newest). Python standard library only, no numpy/scipy, no C++, no Content/Data modified or committed.

## Handover (read first)

- **Done:**
  1. Extended `Scripts/Tools/dayloop_digest.py`:
     - Added `load_folder(folder)` helper to load result JSONs and parse `summary.json` execution states without duplication.
     - Extended `row()` to extract `seed`, `scenario`, `def_side`, `atk_side`, `def_win`, `atk_capture`, `att_killed`, `att_down`, and `att_down_ge2`, preserving 100% backward compatibility for all existing keys and summaries.
     - Added `--compare <folderB>` parameter to `dayloop_digest.py` for direct paired comparison delegation.
  2. Created `Scripts/Tools/paired_ab.py`:
     - Imports parser functions directly from `dayloop_digest.py` (no copy-pasting).
     - Standard library only:
       - Exact two-sided McNemar / sign-test p-value via binomial tail probability: $\min(1.0, 2 \sum_{i=0}^{\min(b,c)} \binom{b+c}{i} 0.5^{b+c})$.
       - Percentile bootstrap with 2000 resamples (deterministic via `--rng-seed`, default 42) computing 95% confidence intervals for medians of A, B, and the difference (B - A).
       - Per-scenario analysis grouping and paired seed extraction.
       - Seed set discrepancy detection emitting a loud, multi-line `WARNING: SEED SETS DIFFER!` with exact lists of missing seeds.
       - Optional `--tune-seeds` split (e.g. `--tune-seeds 1-10`) partitioning seeds into tune, held-out, and all combined sets.
       - Outputs: defender win rates, attacker objective captures, attackers killed per round, share of rounds with >= 2 attackers down, paired discordant counts (A-only vs B-only), McNemar p-value, and behaviour medians with 95% CIs.
       - Full `--json <out.json>` serialization support.
  3. Created `Scripts/Tools/test_dayloop_digest.py`:
     - 7 unit tests (100% pass in 0.25s):
       1. `test_identical_folders`: identical folders yield 0 discordant pairs, McNemar p = 1.0, median diff 0.0 with 95% CI (0.0, 0.0), and no seed warnings.
       2. `test_handmade_5_seed_pair_exact_counts`: verified exact counts for defender wins (A: 3/5, B: 2/5), discordant counts (A-only: 2, B-only: 1, p=1.0), captures (A: 1, B: 1), attackers killed per round (A: 2.0, B: 1.4, diff: -0.6), and share >= 2 down (A: 0.6, B: 0.4, diff: -0.2).
       3. `test_different_seed_sets_warning`: verified loud warning generation, seed difference reporting, and comparison on common seeds only.
       4. `test_bootstrap_deterministic`: verified that fixed `--rng-seed` produces identical point estimates and confidence intervals across runs.
       5. `test_tune_held_out_split`: verified correct partitioning into tune, held-out, and all combined sets.
       6. `test_mcnemar_exact_binomial`: verified edge cases ($n=0 \implies p=1.0$, ties $\implies p=1.0$, asymmetric binomial tails).
       7. `test_seed_spec_parsing_and_formatting`: verified parsing and range compaction (`1-5, 7, 9-12, 15`).
  4. Added paired A/B tool pointer to `docs/port/LANES.md`.
  5. Gates passed: `python -m unittest Scripts/Tools/test_dayloop_digest.py` (PASS, 7 tests), `python -m unittest Scripts/Tools/test_lane_watchdog.py` (PASS, 9 tests), `typesafe_triage.py --audit-diff --agent gemini` (PASS, Risk 0.0/4), `git diff --check` (clean).
- **Left / not done:** Push of `gemini/093-paired-ab-digest` is blocked by the local `.git/hooks/pre-push` hook (which contains a hardcoded 2026-11-04 ALLOW regex from `GEMINI_WEEKEND_2026-11.md` that does not include `Scripts/Tools/`). Per security safety guidelines, pre-push hooks are not bypassed. Commits are on local branch `gemini/093-paired-ab-digest` (commit `3d38dac0`).
- **Needs Claude:** Update `.git/hooks/pre-push` ALLOW regex to include `Scripts/Tools/` or push `gemini/093-paired-ab-digest`.
- **Next exact step:** Claude pushes `gemini/093-paired-ab-digest` or merges it.

---

## 1. Mathematical & Statistical Design

### 1.1 McNemar Test / Exact Sign Test (Binary Outcomes)
For paired seeds $s_1, \dots, s_N$, let $Y_{A,i} \in \{0, 1\}$ be defender win in folder A, and $Y_{B,i} \in \{0, 1\}$ in folder B:
- $b$: count of seeds where A won and B lost/drew (A-only wins).
- $c$: count of seeds where B won and A lost/drew (B-only wins).
- Total discordant pairs: $n = b + c$.
- Under $H_0: p = 0.5$, the smaller discordant count $k = \min(b, c)$ follows $\text{Binomial}(n, 0.5)$.
- Exact two-sided p-value:
  $$p = \begin{cases} 1.0 & \text{if } n = 0 \\ \min\left(1.0, 2 \sum_{i=0}^k \binom{n}{i} 0.5^n\right) & \text{if } n > 0 \end{cases}$$
Calculated using Python's standard `math.comb(n, i)` without external libraries.

### 1.2 Percentile Bootstrap (Continuous Proxy Metrics)
For continuous defence metrics (`on_pos`, `idle_max`, `crossfire`, `gap`, `exposed_m`, `spread`):
- Pairs $(a_i, b_i)$ for common seeds are resampled with replacement $B=2000$ times using `random.Random(rng_seed)` to preserve the inter-seed correlation structure.
- In each replicate $r \in \{1 \dots B\}$, medians $\text{med}(A^*_r)$, $\text{med}(B^*_r)$, and difference $\text{diff}^*_r = \text{med}(B^*_r) - \text{med}(A^*_r)$ are computed.
- 95% confidence intervals are extracted via linear interpolation at the 2.5th and 97.5th percentiles of the sorted replicate distributions.

### 1.3 Seed Set Validation
- If seeds in folder A differ from folder B, a loud multi-line banner is emitted:
  ```
  WARNING: Seed sets differ for scenario 'street'!
    Folder A seeds (150): 1060-1209
    Folder B seeds (143): 1060-1202
    Common paired seeds (143): 1060-1202
    Missing in B (7): 1203-1209
    Missing in A (0): (empty)
  ```
- Paired metrics are computed strictly over the intersection of common seeds.

---

## 2. What changed (file:line)

### Modified files
- `Scripts/Tools/dayloop_digest.py`:
  - Lines 36-84: Extended `row()` to parse `seed`, `scenario`, `def_side`, `atk_side`, `def_win`, `atk_capture`, `att_killed`, `att_down`, `att_down_ge2`.
  - Lines 91-105: Extracted `load_folder(folder)` supporting both flat and nested JSON results and `summary.json` execution states.
  - Lines 108-132: Added `--compare`, `--tune-seeds`, `--rng-seed`, and `--resamples` flags in `main()`.
- `docs/port/LANES.md`:
  - Line 22: Added pointer to `Scripts/Tools/paired_ab.py`.

### New files
- `Scripts/Tools/paired_ab.py`:
  - Complete standalone paired A/B tool re-using `dayloop_digest` parser.
  - Functions: `exact_mcnemar_p`, `parse_seed_spec`, `format_seed_ranges`, `percentile`, `bootstrap_paired_medians`, `evaluate_partition`, `compare_paired_runs`, `format_report`, `compare_folders_cli`, `main`.
- `Scripts/Tools/test_dayloop_digest.py`:
  - Complete test suite with 7 test cases covering identical folders, hand-made pairs, seed set warnings, bootstrap determinism, tune/held-out split, McNemar math, and seed parsing.

---

## 3. Usage Example & Output

```bash
python Scripts/Tools/paired_ab.py Saved/Logs/Worker/main Saved/Logs/Worker/branch --tune-seeds 1-10 --rng-seed 42
```

Sample output:
```
================================================================================
PAIRED A/B DIGEST
================================================================================

================================================================================
SCENARIO: crossroads
  Seeds A (30): 1-30
  Seeds B (30): 1-30
  Common paired seeds (30): 1-30
================================================================================

--- TUNE SET (N=10 paired seeds) ---
Outcome Metrics:
  Metric                      A (main)               B (branch)             Diff (B - A) / Discordant
  --------------------------  ----------------------  ----------------------  --------------------------------
  Defender wins              8 / 10 (80.0%)         4 / 10 (40.0%)         A-only 4 / B-only 0 (McNemar p=0.1250)
  Attacker captures          2 / 10 (20.0%)         5 / 10 (50.0%)         +3 (+30.0%)
  Attackers killed / round   2.40                   1.70                   -0.70
  Share >= 2 attackers down  0.800 (8/10)           0.400 (4/10)           -0.400 (-40.0%)

T-04 Behaviour Medians (Bootstrap 95% CI):
  Metric             A (main) Median [95% CI]   B (branch) Median [95% CI] Diff (B - A) [95% CI]
  ------------------  --------------------------  --------------------------  --------------------------
  on_pos             0.67 [0.33, 1.00]          0.33 [0.00, 0.67]          -0.34 [-0.34, 0.00]
  idle_max           1.50 [1.00, 3.20]          2.70 [1.40, 4.00]          +1.20 [-0.30, 1.50]
  crossfire          25.00 [18.00, 42.00]       22.00 [15.00, 35.00]       -3.00 [-7.00, 6.00]
  ...
```

---

## 4. Gate Results

| Gate | Status | Detail |
|---|---|---|
| `test_dayloop_digest.py` | **PASS** | 7 tests pass (0.25s) |
| `test_lane_watchdog.py` | **PASS** | 9 tests pass (0.003s) |
| `typesafe_triage.py --audit-diff` | **PASS** | Boundary Breach: PASS, Risk Rating: 0.0 / 4 |
| `git diff --check` | **PASS** | Clean, no whitespace or line ending issues |
| Content/Data safety | **PASS** | 0 Content/Data files touched or committed |

---

093 FINISH | PASS (local) | 3d38dac0 | done: dayloop_digest extension, paired_ab.py, test_dayloop_digest.py, LANES.md, gates | NOT done: push blocked by local pre-push hook (needs Claude ALLOW update) | gate: unittest 7 pass, lane_watchdog 9 pass, typesafe PASS, git diff clean | report: docs/port/gemini/outbox/093-paired-ab-digest.report.md
