# Claude (lead) resume, 2026-11-17 ~16:30 - state for the next lead session

Read first: CLAUDE.md, MEMORY.md (all rules), docs/port/WEEK_PLAN_2026-11-17.md, docs/design/TACTICAL_MAP.md, TASK_BOARD («Баги
дизайнера 2026-11-17», «Решения дизайнера 2026-11-17»), chat `python Scripts/Tools/agent_chat.py read --last 15`. Restart the chat
listener (Bash run_in_background: `python Scripts/Tools/agent_chat.py wait --as claude --timeout 3600`, restart after each message).

## Merged today (main, pushed)
F-1 facing return 3 s, F-2 leader takes the corner, F-3 obstacle clicks, F-4a mission goals (+ Codex 039 labels, 040 save), B17-1 corner pick,
B17-2/3/4 mantle (ghost pose, run climb, fence vault), Gemini 077 PMC order lifecycle, Codex 037/038, FormationJitter fix (API agent),
Grok G001 ramp stall, 082 props vs rotated buildings, x6 layout committed, ladder-only roof factor 0.65, golden regen. main built at 50e8e4a9
(no C++ change until FormationJitter 61dba12a / G001 7705adf6 - REBUILD main before the designer tests, only with his editor closed).

## Running (owner: the lead)
- **Tactical map P1** (TACTICAL_MAP.md part P1): Claude Opus subagent of the OLD session in worktree CodexTactics-claude, branch
  `claude/tactical-map-p1`. It stops when the old session closes - check `git -C ../CodexTactics-claude log main..claude/tactical-map-p1`
  + status; review / finish / merge. Then P2 (PMC placement on spots) -> Antigravity, P3 (arena metrics) -> agent.
- **Grok G002** (trainer behaviour metrics + MAP-Elites, docs/port/grok/inbox/G002-*): headless run in CodexTactics-grok, session name
  `grok-G002`, plan 03d7169e. Drive Grok with `Scripts/Tools/grok_task.ps1 -PromptFile <f> -Session grok-G002 -Model grok-4.7`
  (hidden; prompts without double quotes; refuses to run if XAI_API_KEY is set - subscription only). Review + merge its FINISH.
- **Antigravity (Gemini, day)**: queue 080 enemy jitter (plan 8617b609 approved) -> 081 ramp fight -> 078 corpse cost -> 079 short orders.
- **Codex 041** day sweep (192/287 at 14:13, 4 initial FAIL); keeps the 10 % rule (designer!); may get one more reset.
- **codex-worker** (second machine): «CodexTactics Worker Loop» - smokes after 08:00, then pmc-v5 training until next 08:00; morning report
  (3 best / 3 worst clips, stalemate share, quiet spells, orders used). 20:30: post the frozen commit in chat topic `trainer-night`; the
  worker session (CodexWork171126) syncs with `worker_sync.ps1 -SkipSmoke` (no stop needed).

## Claude's own open items
- Medic loses his own order on the ramp (Grok G001 "needs Claude": OperativeCharacterMovement.cpp:276 / :620, SquadSubsystemFormation.cpp:1375).
- StealthModeSmoke (fire gate in stealth cover), Codex 041 reds, 42 + 072 jump-down after the designer's mantling check, F-4b (P2).
- Hidden runs: every long agent run through `Scripts/Tools/run_hidden.ps1` (designer: no Terminal windows).

## Rules the designer stressed today
Gemini CLI is NOT used for now. Resources: Claude subscription + Anthropic API (~83 USD left; use it: API reviews before merges, overflow
agents via claude_api_agent.ps1), Grok (subscription), Antigravity (day), codex-worker. Never change an agent rule he did not ask to change
(memory no-unasked-rule-changes). Reply in Russian; code / docs / commits in English; commit + push main with $env:CODEX_AGENT='claude'.
