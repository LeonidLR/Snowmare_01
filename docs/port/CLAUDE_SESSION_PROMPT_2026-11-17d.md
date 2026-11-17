# Claude (lead) resume, 2026-11-17 late evening (supersedes 17c)

Read: CLAUDE.md, MEMORY.md (pipeline-decisions-2026-11-17, designer-decisions-2026-11-17, no-unasked-rule-changes), docs/port/WEEK_PLAN_2026-11-17.md
(B2), docs/design/TACTICAL_MAP.md, TASK_BOARD (B17 + R17 tables), docs/port/RESOURCES.md. Chat listener in background, restart after each message.
Rule: API Opus review (Scripts/Tools/api_task.py, --effort low, max-tokens 10000) before every merge of agent work. Builds: Scripts/build.ps1
-Remote worker (incremental ~1.2 min) - fall back to local on exit 2. Long commands via Scripts/Tools/run_hidden.ps1 (no Terminal windows).

## Merged since 17c
R17-2 stealth through climbs + StealthModeSmoke 6b3d24cc; G003 visible props d33274f9; worker remote build 1d953f2d; daily smoke subset config.

## Work in flight - the subagents of the OLD session STOP when it closes; continue each from its worktree branch
| Item | Worktree / branch | State | Next |
|---|---|---|---|
| R17-1 formation mantling in turns | CodexTactics-claude / claude/r17-1-formation-mantle (5c46d567, 761e3b3a) | gate green; fixing 5 API findings (turn timeout while mantling, path leader stranded in EndSquadMantle, start-frame spike lanes, Hold waiters sync pathing, stall retry) | check commits after 761e3b3a; finish fixes, gate, merge |
| G003b real gameplay props (barrels / loot / mines / tripwires / turrets) | CodexTactics-gemday / claude/g003b-real-props (fae04461..c9ff80fa) | gate green; fixing 8 API findings (ticker teardown, double ticker, bLiveSpawn, dropped-item merge, MinesInert crates, team on turrets/mines - HOSTILE default + config switch, scaled box, 'props spawned' event) | finish, gate, merge - the designer tests the generator with all assets on 11-18 |
| TACTICAL_MAP P2 + P3 (PMC uses danger places; arena behaviour metrics for the trainer) | CodexTactics-tactical / claude/tactical-p2 | running long (gate) | check log / commits; finish, API review, merge - wanted on 11-18 |
| R17-3 + R17-5 click-to-preview + door breach | CodexTactics-gemcli / claude/r17-3-click-preview | running (gate) | check commits; finish, API review, merge |
| Grok G002 trainer metrics + MAP-Elites | CodexTactics-grok, session grok-G002 | running headless | FINISH in chat topic grok-G002; then G004, G005 (grok_queue.ps1) |
| Codex 042 first aid drag/bag -> 043 ramp fight | CodexTactics-agents | Codex until ~22:00, 10 % rule | review + merge |
| codex-worker (session CodexWork171126) | - | 23:45 builds the freeze (post it in topic trainer-night, else origin/main), 00-06 full smoke sweep, 06-08 trainer, ~08:20 report + clips (docs/port/reports/worker-night-2026-11-18.md, Saved/Reports/worker-2026-11-18) | forward key clips to the designer's phone (SendUserFile) |

## Open designer question
Turrets on generated maps: A hostile (route cover, default now) / B player's / C per-turret choice in the generator. Asked 11-17 late; record the answer.

## Queue after these
R17-6 first-aid patient anims; R17-4 vertical scan by tactical-map points + height roles; Codex 041 new reds (PauseBarrelOrder, Tutorial,
PeekMines - PeekMines has no props file on the Lake); Medic order loss after group arrival (design question); 42 + 072 jump-down.
Main must be rebuilt for the designer (only with his editor closed) after the merges.
