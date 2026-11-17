# Claude (lead) resume, 2026-11-17 evening - state for the next lead session

Read first: CLAUDE.md, MEMORY.md (esp. pipeline-decisions-2026-11-17, designer-decisions-2026-11-17, no-unasked-rule-changes),
docs/port/WEEK_PLAN_2026-11-17.md (B2 re-plan), docs/design/TACTICAL_MAP.md, TASK_BOARD (B17 + R17 tables), docs/port/RESOURCES.md (moves).
Chat: `python Scripts/Tools/agent_chat.py read --last 20`; listener in background (`wait --as claude --timeout 3600`), restart after each message.

## Merged today after the afternoon prompt (b)
Tactical map P1 3219e065, Gemini 080 enemy jitter 78b21ffb, 082 props rotation, Codex 041 report, chat topic limit 25 + --status (b70a73c5),
task board grok / moves (c62bf706), grok_task tests + grok_queue (eb7268f0), no-console fix for chat server (f1537d76), worker test farm
33ff7a93 (by CodexWork171126). main rebuilt for the designer at f1537d76 (later merges since: rebuild before he tests).

## Running agents (each in its own worktree; review + API Opus review + merge)
- **R17-1 formation mantling in turns** - Claude Opus subagent, CodexTactics-claude, branch claude/r17-1-formation-mantle.
- **R17-2 stealth through climbs + StealthModeSmoke** - Claude Opus subagent, CodexTactics-gemcli, branch claude/stealth-climb
  (4c430686 + 73eabd90 + review fixes; lead decision: calibrate the crouch-corner aim base, NOT stand up).
- **G003 generated-map props** (designer tests the generator with all assets TOMORROW) - Claude Opus subagent, CodexTactics-gemday,
  branch claude/g003-gen-props.
- **TACTICAL_MAP P2 + P3** (danger places affect gameplay + arena behaviour metrics for the trainer - wanted by TOMORROW) - Claude Opus
  subagent, CodexTactics-tactical, branch claude/tactical-p2.
- **Grok G002** trainer behaviour metrics + MAP-Elites (CodexTactics-grok, session grok-G002); then G004 corpse cost, G005 short orders
  (grok_queue.ps1 can start the next at night).
- **Codex** (until ~22:00): 042 first aid drag/bag, then 043 ramp fight (B17-6). Keeps the 10 % rule.
- **codex-worker** (session CodexWork171126): tonight trial - trainer until 19:00, 19-24 remote-compile build farm (`build.ps1 -Remote`),
  00-06 full smoke sweep, 06-08 trainer; 08:00 report to the designer (clips, stalemate share, smoke table, compile times) - forward
  key clips to his phone (SendUserFile). From 11-18: test farm 08:00-16:30, trainer 16:30-08:00, freeze commit at 16:30 (topic trainer-night).

## Queue after these (TASK_BOARD R17)
R17-3 + R17-5 click-to-preview building interaction (decisions recorded) + door breach runtime hookup; R17-6 first-aid patient anims;
R17-4 vertical scan by tactical-map points + height roles; Codex 041 new reds: PauseBarrelOrder, Tutorial, PeekMines; Medic order loss
after group arrival (design question); 42 + 072 jump-down after the mantling check.
