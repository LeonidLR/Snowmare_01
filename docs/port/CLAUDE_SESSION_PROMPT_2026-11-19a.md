# Claude (lead) resume, 2026-11-19 morning (supersedes 18b)

Read first: CLAUDE.md, MEMORY.md (start with `current-state`, then grok-budget, worker-load-jobs, pipeline-decisions-2026-11-17,
no-unasked-rule-changes, main-pc-format-mirror), docs/port/TASK_TEMPLATE.md (inbox template + lead review checklist - binding),
TASK_BOARD "Сделано (2026-11-19)" and "(2026-11-18)". Reply to the designer in Russian.

## Mode: lead = TEST + REVIEW, agents implement (designer 2026-11-19)
- Subscription weekly limit was 95 % on 11-19 07:30 (reset 11-21 09:00 UTC). Hard stop for the whole team at **98 %** (get_usage before
  every bigger step). Until the reset the lead does NOT write features: it dispatches (TASK_TEMPLATE), tests and reviews what
  Antigravity, Grok and API agents hand over, merges.
- Lead test loop for every hand-over: read the diff -> API Opus review with the touched functions as context (`Scripts/Tools/api_task.py
  --effort low`, ~0.05-0.15 USD) -> verify EVERY finding in the code before applying (11-18: 2 of 6 findings were false; a blindly applied
  one broke PeekMinesSmoke) -> run the task's smokes yourself on the agent's build (never merge on the agent's gate alone when you
  changed code) -> merge `--no-ff`, TASK_BOARD row, chat ack -> build main when the designer's editor is closed.
- Generated-map smokes from Git Bash: `MSYS_NO_PATHCONV=1` or `/Game/...` becomes `C:/Program Files/Git/Game/...` (11-19 trap).
  `smoke_parallel.ps1` cannot run a map other than test_map.json's -> run UnrealEditor-Cmd directly for L_Gen_BgenTest_X6.
- Budgets: API ledger 92.5 / 100 USD (Saved/Orchestrator/api_spend.jsonl; CLI agent runs may be under-logged - the designer can check
  the console) -> API only for REVIEWS now (~5 USD left), no new API implementation agents unless the designer tops up. Grok 55 % at
  11-19 morning -> ~12 %/day (ask the designer for /usage each morning). Gemini API: HTTP 402 (prepayment depleted) - Gemini CLI unusable.
  Codex: out until 11-23. Antigravity (Gemini in the IDE, worktree CodexTactics-gemday) is back today = the main implementer for TS / Python.

## State (main 48cd1f22, pushed, built 11-19 ~06:00)
Merged 11-18 evening -> 11-19: Codex 046 PeekMines hand-over, Codex 047 suppression pack split, HitchProbe (level-start work under
the loading cover), Antigravity 085b orphan fence, 087 PMC posts + charges in the generator (opt-in, C++ spawn-at-post open), worker
night guard (worker_sync / build_remote refuse while FROZEN), give-way v1 / v2 / v3 (L_Gen_BgenTest_X6 LevelGenRampSmoke solo
4/17 -> 17/18), ramp / route smokes on generated maps run with the cold off (db82a7b7, designer decision).
Not merged:
- `claude/building-generator` (bgen session local_84047e7a, «Генератор зданий CodexTactics»): P0 + P1a + S1 TS core + S1 Python
  builder + pre-S2 refactors (head 6dc78810). Waits for the DESIGNER'S OWN TEST, then merge; after merge rebuild the UE maps of the
  training arenas L_Gen_Train_1..3 (their accepted JSON changed). Bgen stopped at 95 % until the 11-21 reset; next there: refactor #3
  (builder walls from partitions[]), S2 strip grammar (docs/design/levelgen/S2_ROOMS_PLAN.md 5 + 8.1, designer: S2 = go).
- `grok/G008b-overwatch-hold` (c79926d7): REJECTED as is. Round 2 (lip probe 60 m) made the sniper fire but through obstacles; round 3
  (own rim 2 m) is safe but the sniper fires 0 again and PmcRoofDefenceSmoke fails (spread 4.6 < 5). See Grok schedule.
- `claude/main-pc-mirror` (worker-ops session local_83f5d1d5, ef26ba3d): review fixes done, secret-free mirror; merge after its
  checklist lines 2 (build.ps1 local on the worker) and 3 (smoke_parallel -Remote off) PASS after 08:00, then it installs the 2 h
  schedule. Credentials: moved by worker-ops on the designer's direct order (API env keys, git identity, gh); OAuth logins of
  Claude / Codex / Gemini / Grok = fresh sign-ins by the designer. Open designer item: worker PowerShell execution policy (undefined).
- Worker: night trainer pmc-v6-qd2 (freeze 80f7d55b) until 08:00 -> worker-ops posts the report ~08:20 (cells, defender metrics,
  clips -> designer's phone). After 08:00 the worker is the test farm again (-Jobs 4).

## Schedule - Grok (one lane, ~12 %/day; grok-4.7 for L3, grok-4.7-build-fast for log reading; worktree CodexTactics-grok)
| Day | Task | Done when |
|---|---|---|
| 11-19 | **G008b-4 overwatch fire from the roof edge** (same branch): the posted sniper must shoot from the parapet edge - overwatch spot snapped to the parapet inner face (PmcTacticalRules / tactical map spots) and/or the defender steps to the edge (stand / kneel at the rim) before firing; keep lip_cm 200 (no shooting through obstacles); keep the latched hold | X4 tactical seeds 11 + 33: shots from post > 0, overwatch_kills > 0; PmcRoofDefenceSmoke PASS (spread >= 5, guard > 0.6); Stalemate / ArenaTactical / Arena / Cover / CornerHold / PeekMines PASS |
| 11-19 (if share left) | **LevelGenRouteSmoke red** (props on the route): props autogen keeps the mandatory route clear of mines / tripwires (or Antigravity does the TS part - see below; Grok then only verifies the smoke) | LevelGenRouteSmoke PASS on L_Gen_Depot_1_X6 |
| 11-20 | **Give-way last flight B_x1_8** (leader stops halfway up; guess: crowd avoidance counts a mate on the ground within ~176 cm height as a neighbour) - trace run `only=B_x1_8_t7_block_stair_0 trace`, fix | X6 LevelGenRampSmoke solo 18/18 |
| 11-20 / 21 | **P0b LevelGenBuildingsSmoke** (C++, BUILDING_GENERATOR section 5 layer 2) + the LevelGenRampSmoke probe fix (upper flights 'no navmesh path', +-60 cm z window near the top) | per building PASS/FAIL json; on the merged generator map |
| 11-21 | **PMC spawn at post** (C++ consumer of 087): PmcEncounterRules already reads `spawn: [x, y, z]`; read `post` {pos, yaw, kind}, face the post yaw, posture defend | a generated map with «ЧВК» on spawns squads at their posts; PmcArena smokes PASS |

## Schedule - Antigravity (worktree CodexTactics-gemday; plan first in its chat topic, isolated tasks, lead tests + merges)
| Day | Task | Notes |
|---|---|---|
| 11-19 | **088 props route clearance** (TS, props autogen): no mine / tripwire / blocking prop within N m of the mandatory route (the 087 PMC charges already do it - reuse); regenerate props for Depot_1_X6 / X4 | cause found by the lead 11-18: the 11-16 PASS had no props, the 11-18 run holds 3x on 'Tripwire spotted - squad holding' |
| 11-19 | **084** weather per map / map name / props regen in a frame (inbox 084, already planned) | after 088 |
| 11-20 | **087 follow-ups** (TS): retreat lines of ALL posts kept free, de-dup the pmc_charges layer on re-autogen, tuning key (id, kind, source) after a re-roll | from the 087 API review |
| 11-20 | **Building generator support** (TS, under the bgen session's direction, its branch): isolated S1/S2 pieces the bgen session names (schema /2 types + round-trip tests, validator severity / entry kinds, page glyphs) - ask the bgen session for the list | bgen owns the design; Antigravity never edits its core |
| 11-21 | **085b follow-up**: one shared endpoint-tolerance helper for applyTemplates / mergeAttempt / the validator | from the 085b API review |

## Building generator help (designer priority)
- The bgen session leads it; the lead tests: rebuild L_Gen_BgenTest_X6 only when bgen says, run LevelGenRamp / Ladders / Route smokes
  (cold off), report per flight geometry vs C++, route C++ work to Grok (schedule above).
- Open designer decision: none on S1; F4 rule fix for story 3 (window smash faster than door kick) comes from the bgen session after S2.
- Merge path: designer tests the TG page + a built map -> lead merges claude/building-generator -> rebuild Train_1..3 maps.

## Morning 11-19 first steps
1. get_usage; ask the designer for Grok /usage.
2. Worker report ~08:20 (trainer pmc-v6-qd2 with G008) -> designer's phone; worker-ops 'ready' for the mirror (lines 2-3) -> merge.
3. Start Grok G008b-4 (prompt from the table, session G008b resume, MaxTurns <= 100).
4. Write Antigravity 088 inbox from TASK_TEMPLATE; the designer wakes Antigravity (pastes its prompt) -> plan -> OK.
5. Chat listener: `python Scripts/Tools/agent_chat.py wait --as claude --timeout 3600` in the background, restart after each message;
   never a second `wait`; stop restarting when no agent is active (empty wake-ups cost the weekly limit).
