# 098 - Walk-smoke failure digest: why do 6-9 legs per map still fail? (building generator support) - LIGHT-MEDIUM, Python
Limit rule: at 10 % of your daily limit left: stop, commit (WIP is fine), handover at the top of the report, FINISH line.
Base: newest main. Branch gemini/098-walk-smoke-digest. Worktree CodexTactics-gemday. PLAN first in chat topic `gemini-098`.
Router: L2. Push may be refused by the pre-push guard - expected; commit locally, Claude merges.

## Design gate (lead filled 2026-11-22)
- Context: `CodexTactics.LevelGenBuildingsWalkSmoke` walks the squad through every flat entry, stair and roof link of a generated
  map. After the confined-formation merge (main addfe48b) it still fails 6-9 legs per map. Reading 600 KB logs by hand costs tokens;
  the building-generator lead needs a 15-line digest.
- Inputs (read-only, real samples to develop against - copy them into a fixture folder, never edit the originals):
  - JSON report `Saved/LevelGen/buildings_walk_<Map>.json` (written by the smoke; the RESULT line names its path);
  - log lines `Smoke leg N/M <story> <kind> <from> -> <to> ...: FAIL ...` and the final `Smoke RESULT:` line;
  - samples: `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-agents\Saved\Logs\after-L_Gen_StoryShowcase_Kit-Walk.log`,
    `after-L_Gen_StoryShowcase-Walk.log`, `after-L_Gen_BgenPlans-Walk.log` and the matching `Saved\LevelGen\buildings_walk_*.json`
    in the same checkout (READ ONLY - that folder is Claude's worktree).
- Output: `Scripts/Tools/walk_smoke_digest.py <log or json> [more files] [--json]`:
  1. one header line per map: buildings passed, walk legs x/y, flat entry legs x/y, max squad/solo ratio, wall time;
  2. failures grouped by CAUSE class, parsed from the FAIL text: `ratio` (squad > 2 x solo), `follower_stuck` (STUCK, move active),
     `leader_stopped` (STOPPED, no move order running), `refused`, `other` - count per class, and per class the story ids and storeys;
  3. clusters: failures whose position is within 150 cm of each other -> one line "N fails at X Y Z (stories ..., causes ...)";
  4. the engine's `is stuck and failed to move` lines: count per StaticMeshActor name (which mesh blocks people);
  5. with two files of the same map (before / after): per-class delta.
  Plain text by default (<= 25 lines per map), `--json` for machines.
- Acceptance: pytest `Scripts/Tools/test_walk_smoke_digest.py` on small fixture excerpts (one line of each FAIL class, the RESULT line,
  two stuck lines) proves every class, the cluster rule and the delta; a run on the three real logs pasted into the report.
- Designer decision needed? none.

## Never
Change the smoke, its thresholds or C++; touch the CodexTactics-agents checkout (read only); commit the 600 KB logs (fixtures = short excerpts).

## Gate
`python -m pytest Scripts/Tools/test_walk_smoke_digest.py -q` | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` | `git diff --check`.
Allowed paths: `Scripts/Tools/walk_smoke_digest.py`, `Scripts/Tools/test_walk_smoke_digest.py`, `Scripts/Tools/fixtures/walk_smoke/**`,
`docs/port/gemini/**`.

## Report
`docs/port/gemini/outbox/098-walk-smoke-digest.report.md` (Russian): what each class means, the digest of the three real logs, test output.
FINISH in topic `gemini-098`: `098 FINISH | <status> | <commit> | done: ... | NOT done: ... | needs Claude | gate: ... | report: <path>`.
