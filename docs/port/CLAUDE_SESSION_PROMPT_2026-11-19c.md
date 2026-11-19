# Claude (lead) resume, 2026-11-19 ~12:00 (supersedes 19b)

Read first: CLAUDE.md, MEMORY.md (start with `current-state`, `focus-bgen-and-enemies`, `dont-ask-editor-closed`,
`editor-force-close-permission`, `backup-before-merge-main`, `lead-not-implementer`), docs/port/TASK_TEMPLATE.md (lead review
checklist), docs/design/gdd/smart-enemies-roadmap.md (sections 5, 5a, 6). Reply to the designer in Russian.

## Designer's standing orders (11-19)
- FOCUS for the next days: the building generator and smart-enemy training only. Everything else -> evening list.
- Economy: the 5-hour window filled fast today (30 % in the first hour). Lead = review / merge / dispatch; code is written by Sonnet
  subagents (Agent: subagent_type "ue-l3-major" + model "sonnet" - the router hook blocks l2 for C++ tasks; pass model explicitly).
  One or two subagents at a time, not more. Fresh lead session when context > 30 %.
- Never ask whether the editor is closed: detect it (UnrealEditor command line containing
  `Unreal Projects\CodexTactics\CodexTactics.uproject`, without -game / -run=) after every C++ merge and build main at once;
  if open: save-all + force-close (memory editor-force-close-permission).
- Keep codex-worker busy (the designer watches it). Worker-ops session is ARCHIVED: the lead drives the worker
  (Scripts/Tools/worker_sync.ps1 -Ref <ref> -Build -Jobs 5 -Commands ...; one run at a time, it takes the worker lock).
- Codex LEFT for good. Antigravity unavailable today (its tasks -> Sonnet subagents). Grok: ~7 %/day left until ~11-23.
- The chat task board (Tools/AgentChat, Scripts/Tools/task_board.py) must show every running task with its executor: Claude-run
  tasks have files docs/port/claude/inbox/<L><3 digits>-slug.md (executor in the title); moves in docs/port/RESOURCES.md
  ("<owner> <num> -> claude T004"); post a status line to topic claude-<id> on each step (in-progress window = 2 h).
  After editing task_board.py RESTART the chat server with stdout/stderr redirected (as Scripts/AgentChatApp.ps1 does) - a bare
  pythonw start drops every request.

## State (main 93128c75 + build of fe15cbb0 started ~11:55)
Merged today: mirror 7713954e, Codex 048 00cab89d, T-11 spawn-at-post 510a035b, T-11b PMC-owned charges fe15cbb0, task board fixes.
Day sweep on main f62bc353 (300 smokes): 2 FAIL (CorpseCostSmoke known; SightConditionsSmoke), 4 PASS* (VehicleCombat, DepotVault,
PmcRoofDefence, MantleAlign). PmcRoofDefenceSmoke is RED on built main 510a035b (guard 0.48 / 0.29 < 0.6, spread 4.8 / 3.0 < 5) -
pre-existing; check whether Grok G009 fixes it, else a task in the T-05 / T-06 area.
Designer answered ALL smart-enemies OQs (GDD commits da1c6e3a, 825032ec): nothing blocks the roadmap.

## In flight
| Who | What | Next for the lead |
|---|---|---|
| Worker (terminal tab "Worker day sweep", c2) | Grok G009 gate: worker_sync -Ref origin/grok/G009-hold-until-range, 10 PMC smokes, results Saved/Logs/Worker/g009-gate | read results; G009 accept = posted defenders hold until in-range, PmcRoofDefence / Stalemate / ArenaTactical / TacticalPmc PASS; review diff (3 commits aa7e090f) + merge |
| Sonnet subagent of the OLD lead session (dies with it; check its branch) | T-04 on branch claude/t04-pmc-position-metrics from Codex's WIP (preserved on codex/049-pmc-position-metrics) in CodexTactics-agents | check branch + docs/port/claude/outbox/T04-pmc-position-metrics.report.md; if unfinished, relaunch a Sonnet subagent from its last commit |
| Sonnet subagent of the OLD lead session | 088 props route clearance on gemini/088-props-route-clearance in CodexTactics-gemday (from Antigravity's WIP; X6 props file must NOT be committed) | same: check branch + docs/port/gemini/outbox/088-props-route-clearance.report.md; review + merge; then rebuild X4 props map and run LevelGenRouteSmoke on the worker |
| Session "S2.2: office doors, windows, wiring behind a toggle" (local_3dfcbfbc) | S2.2 on claude/building-generator: A 203b184b, B 37881ccd (offices on the band plan behind a TG toggle, off by default), C validator codes running | it reports to the lead; worker builds when C++ changes |
| Session "Генератор зданий" (local_93362388) | paused while S2.2 runs (same worktree); next: T-02 builder gaps (kill_zone intent, mantle actor, roof ambush points, mantle in routes) | dispatch after S2.2 |
| Session "T-02: three lab maps" (local_8a5516b3) | done: bf31b6bd layouts; worker built L_Lab_Crossroads / Alley / Bridge, TacticalMapLab* 3/3 PASS | none |

Worker results today: rooms[].anchor run on bgen bf31b6bd - LevelGenBuildingsSmoke L_Gen_BgenTest_X6 41/41 (was 28/41); lab maps built
on the worker only. Building generator is NOT merged into main: only after the designer's own test (he said "позже").

## Queue (smart enemies first two weeks, roadmap 6)
Sonnet subagents, one or two at a time, after the 5-hour reset: T-03 scripted opponent (CodexTactics-claude worktree is free) ->
T-12 traversal smoke (after T-04, CodexTactics-agents) -> T-01 clip decision traces (was Antigravity 089; Python) -> T-10 training
gate + day mode (was 091) -> T-09 tactical export v2 (was 090, TS) -> T-05 firing positions (HEAVY, lead designs, Sonnet writes).
Grok after G009: guard_share None on X4 / PmcRoofDefence guard regression (if G009 does not fix it).
Worker: nights 16:30-08:00 = scenario regression sweep (STOP file blocks run_night_trainer); day = per-change runs.

## Traps
- Git Bash: Python's /tmp is not Bash's /tmp (use cygpath -w); `\b` / `\a` in printf / heredoc text eat backslashes.
- `git pull --rebase --autostash` in main rewrites the designer's files: back up (Saved/Backups/designer-wip-*), ff-only merges.
- The designer's 9 uncommitted X6 files in main: never commit / overwrite.
- smoke_parallel cannot run a map other than test_map.json's: UnrealEditor-Cmd directly (see C:\Users\User\anchors_run.ps1 on
  the worker for a working example incl. levelgen_build_map.ps1 + a direct smoke).
