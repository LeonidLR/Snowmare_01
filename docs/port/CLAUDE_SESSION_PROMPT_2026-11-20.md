# Claude (lead) plan for 2026-11-20 morning - the designer is travelling, offline until after lunch (supersedes 19c)

Read first: CLAUDE.md, MEMORY.md (start with `current-state`, `results-over-speed`, `focus-bgen-and-enemies`,
`dont-ask-editor-closed`, `backup-before-merge-main`, `lead-not-implementer`), docs/port/TASK_TEMPLATE.md, the roadmap
docs/design/gdd/smart-enemies-roadmap.md (5, 5a, 6). Reply to the designer in Russian. He reads ONE short summary after lunch.

## Designer's standing orders (11-19)
- Focus: building generator + smart-enemy behaviour. "I don't want to rush anyone, I want a result. Do as you see fit."
  -> accept a behaviour fix only on repeated seeds (day loop 20-50 seeds, or x10 repeats), never on one green run.
- Code is written by Sonnet subagents (Agent: subagent_type "ue-l3-major", model "sonnet"); the lead designs, reviews, merges.
  One or two subagents at a time.
- **codex-worker is OFF for at least 4 hours tomorrow (hours unknown).** Never block on it:
  - before every worker step check reachability with a short timeout:
    `ssh -o ConnectTimeout=8 -o BatchMode=yes codex-worker "echo ok"` (no answer = offline; do NOT start worker_sync / build -Remote).
  - while it is off: build + smoke LOCALLY in main (Scripts/build.ps1, Scripts/smoke_parallel.ps1 -Remote off -Jobs 3,
    Scripts/Tools/worker_seed_batch.ps1 runs locally too: it resolves the repo from its own folder, e.g.
    `powershell -File Scripts/Tools/worker_seed_batch.ps1 -Command CodexTactics.PmcCrossroadsSmoke -Map /Game/Maps/Generated/L_Gen_Depot_1 -LevelJson L_PmcArenaDepot.json -ExtraCmdArgs "Seed={seed} Out={out}" -Seeds (1..20) -Jobs 3`
    from a PowerShell session (-File does not parse int arrays; use `& script -Seeds (1..20)`).
    Subagents: tell them `build.ps1` locally and `smoke_parallel.ps1 -Remote off` while the worker is off.
  - queue worker-only runs (lab maps L_Lab_*, big sweeps, -Jobs 5 seed batches) in a list and run them when it answers again.
- His editor will be closed: detect it yourself (UnrealEditor command line with `Unreal Projects\CodexTactics\CodexTactics.uproject`,
  no -game / -run=); never ask. His 9 uncommitted X6 files in main: never commit / overwrite; back up before merges
  (latest backup Saved/Backups/designer-wip-20261119-121729 + hashes).

## State at the end of 11-19 (main d765203f + 3f5cc62a + 8e460b4d + board)
Merged 11-19: 088 props route clearance (route_clear_m 6 m - OPEN QUESTION to the designer: approved was 3 m), Grok G009,
T-04 position metrics, Grok G010 (PmcRoofDefence 6/10 -> 10/10), T-03 scripted opponent (walks, script_sprint false: a sprinting
body cannot fire), 3f5cc62a PmcCrossroads / PmcAlley smokes now use the scripted opponent.
Full sweep on 540e2ec4: 304 smokes, 2 FAIL (PmcRoofDefence -> fixed by G010; CorpseCost known), 6 PASS*.
Tools: Scripts/Tools/worker_seed_batch.ps1 (seeded / repeated runs), Scripts/Tools/dayloop_digest.py (digest of a result folder),
worker wrapper C:\Users\User\dayloop.ps1 (-Smoke -From -To -Extra).

Day-loop baselines (self-play attackers, Depot stand-in, 30 seeds; Saved/Logs/Worker/):
| scenario | on_pos p50 | idle p50 | crossfire p50 | gap p50 | spread p50 |
|---|---|---|---|---|---|
| A-00A crossroads (dayloop-crossroads-1) | 0.0 | 3.3 s | 11.7 deg | 0.67 s | 4.9 m |
| A-00B alley (dayloop-alley-1) | 0.0 | 1.7 s | 24.5 deg | 1.18 s | 10.3 m |
With the scripted opponent (3f5cc62a): the 3-seed probe dayloop-crossroads-script3b - check it; then take the 30-seed
SCRIPT baselines for both scenarios FIRST tomorrow (locally if the worker is off): those are what T-05 is judged against.

## In flight
| Who | What | Next for the lead |
|---|---|---|
| Sonnet subagent (started by lead session 19c, dies with it) | T-05 firing positions, branch claude/firing-positions in CodexTactics-agents, brief docs/port/claude/inbox/T005-firing-positions.md (phase a positions + validation + FiringPositionSmoke; phase b defenders take crossfire positions) | if the branch has a report: review; else relaunch a Sonnet subagent from the branch's last commit with the same brief. Accept phase b only when the day loop (30 seeds, both scenarios, script opponent) beats the baselines; merge after the gate in the brief |
| Session "Генератор зданий" (local_93362388) | bgen on claude/building-generator: next bot mantle (PlaytestBotRouteMode reads Waypoint.Mantle, C++), then S2.3 blocks (+1200-core engine check) | review its reports; bgen is NOT merged into main before the designer's test |
| Queue after T-05 | mounted MG nest, spec docs/design/quick-specs/mg-nest-2026-11-19.md (designer approved, after T-05) | - |

## Order tomorrow
1. Reachability check of codex-worker; SCRIPT baselines A-00A / A-00B x30 (worker or local x20).
2. T-05 review / relaunch; its day-loop gate; merge when it beats the baselines (and the brief's smokes PASS).
3. Next smart-enemies tasks by the roadmap (T-12 traversal smoke, T-01 clip traces, T-10 gate + day mode) - one at a time.
4. bgen reviews as they come.
5. After lunch: ONE short Russian summary for the designer (what got better, with numbers; what is open; the 3 m / 6 m question).
Night 16:30-08:00 = the scenario regression sweep on the worker if it is on (STOP blocks run_night_trainer).

## Traps learnt 11-19
- worker_seed_batch.ps1 via `powershell -File` cannot take `-Seeds 1,2,3` -> use the wrapper / `& script -Seeds (1..N)`.
- Do not write Saved/agent.lock by hand around build.ps1 on the worker (self-deadlock: build.ps1 waits for it).
- A worker_sync can hang on its first ssh call (11-19 11:47): ConnectTimeout does not cover a stalled session; if no sync
  point appears on the worker after ~5 min, stop it and re-run.
- Other sessions may commit in the main checkout too: `git log -3` before your own commit / push.
