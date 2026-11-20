# Plan 089: Decision timeline for designer clips (smart enemies T-01)

## 1. Goal & Context
Implement `Scripts/Tools/pmc_round_trace.py` per roadmap task **T-01** (`docs/design/gdd/smart-enemies-roadmap.md`) and audit 4.1 (`docs/design/gdd/smart-enemies-audit.md`).
The tool analyzes an arena round from its `arena.json`, `round_*.track.json`, and execution log (`[PmcAI]` lines), producing a 1-page markdown timeline for designers detailing:
1. Scenario name, objective location, radius, and vertical level.
2. Initial posts and spawn points assigned to each defender.
3. Detailed order sequence with scores from spawn to first contact (and through engagement).
4. Seconds standing in the open per defender.
5. First contact and first-shot timestamps.
6. Evaluation and verdicts on hypotheses **H1-H4** (`HOLDS`, `DOES NOT HOLD`, `CANNOT TELL`).

## 2. Architecture of `Scripts/Tools/pmc_round_trace.py`
- **CLI Interface**:
  ```bash
  python Scripts/Tools/pmc_round_trace.py <arena.json> --round N [--track <track.json>] [--log <job.log>] [--out <output.md>]
  ```
  - Auto-discovery for track file: looks for `round_<N>.track.json` in `<arena_dir>`, `<arena_dir>/<stem>.arena/`, or `<stem>/`.
  - Auto-discovery for log file: looks for `<stem>.log` in the arena directory or parent directories.
- **Data Ingestion & Re-use**:
  - Re-uses `nighttrainer/track_stats.py` (`UNIT_TO_M`, `_pos`, close-range stats logic).
  - Parses `arena.json` (`codex.pmc_arena/1` schema) for round metadata, sides, roles, and defence metrics.
  - Parses `round_<N>.track.json` (`codex.pmc_arena_track/1` schema) for frames, states (`[yaw, stance, hp, state]`), and events (`tactic`, `shot`, `hit`, `kill`, `objective`).
  - Parses log file for `[PmcAI]` entries: posts placed (`placed on a <role> post ... (yaw, score, danger, post_id)`), orders evaluated (`Operator -> Order (score; threat ...)`), failures, and fire rules (`opens fire ... attention off ...`).
- **Analysis Modules**:
  - `TimelineBuilder`: Combines track `tactic` events, log decisions, and combat events into a chronological timeline up to and after first contact.
  - `OpenTimeEstimator`: Calculates time spent standing in the open before and during contact (tracking frames where `stance == "stand"` and cover is absent).
  - `HypothesisEvaluator`:
    - **H1**: Ground-objective scenario without defence site pulling all defenders onto the capture point.
    - **H2**: Defenders abandoning tactical posts at long range when `bEverContact` turns true.
    - **H3**: Lack of cover slots near defenders (`bCoverAvailable` false / cover 0.00).
    - **H4**: Delayed firing caused by `ShouldHoldFire`, attention rules, or weapon range band mismatch.
- **Formatting**:
  - Generates clear, structured Markdown tables and narrative summaries.

## 3. Unit Test Suite `Scripts/Trainer/tests/test_round_trace.py`
- Test cases covering:
  - CLI argument parsing and auto-discovery.
  - Track stats and timeline generation on committed fixtures (`sample_arena_objective_result.json`, `real_arena_round.track.json`).
  - Log parsing for `[PmcAI]` post assignments, order evaluations, and fire events.
  - Calculation of open standing time and first-shot detection.
  - Correct verdict logic for hypotheses H1-H4.
- Gate: `python -m unittest Scripts/Trainer/tests/test_round_trace.py`.

## 4. Execution on Designer Rounds & Report
- Run the tool on the designer's clip rounds:
  - Round 1: `Saved/Reports/worker-2026-11-18/pmc-v6-qd/gen_0003/jobs/g0003_c02_eval_archive_0.arena.json` round 1.
  - Round 2: `Saved/Reports/worker-2026-11-18/pmc-v6-qd/gen_0003/jobs/g0003_c01_eval_archive_0.arena.json` round 0 (or `pmc-v5` clip round).
- Generate markdown traces and document the findings in `docs/port/gemini/outbox/089-pmc-round-trace.report.md`.
- Formulate the Claim | Evidence table and the final FINISH line.

## 5. Gates & Constraints
- `python -m unittest discover -s Scripts/Trainer/tests` (must pass 100%).
- `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` (PASS).
- `git diff --check` (clean).
- Only allowed paths modified: `Scripts/Tools/pmc_round_trace.py`, `Scripts/Trainer/tests/test_round_trace.py`, `Scripts/Trainer/fixtures/`, `docs/port/gemini/outbox/`.
- Local commits on `gemini/089-pmc-round-trace`, never push main.
