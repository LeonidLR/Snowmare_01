# 045 - Light tasks: mantle log cache, TASK_BOARD + HANDOFF for 2026-11-18, PeekMines diagnosis (Codex, last hours) - LOW risk
Base: main (newest). Branch codex/045-light. Worktree CodexTactics-agents. 10 % rule: at 10 % left stop, commit, report.
First read the lead's review of 044 in the chat (python Scripts/Tools/agent_chat.py read --last 10, topic codex-044): only the CoverCrouch
fix was merged (e4558688); the PeekMines / Suppression smoke changes were rejected because they weakened the checks.

## A - mantle wait-look log cache (C++, small)
`Source/CodexTactics/Private/Characters/SquadSubsystemMantleQueue.cpp` (~line 885) keeps a function-local
`static TMap<TWeakObjectPtr<AOperativeCharacter>, float> LoggedClear` (API review of ccdec8d3: process-lifetime, never pruned, shared
across PIE worlds, misses a re-log when the same man waits again). Move it to a member of the mantle queue state (or `USquadSubsystem`),
clear an operative's entry when he leaves the Wait state, no behaviour change. Gate: build, FormationMantleSmoke x2, MantleSmoke,
`Scripts/test.ps1 -Smart`.

## B - TASK_BOARD done list + HANDOFF for today (docs)
`docs/port/TASK_BOARD.md` section "Сделано (2026-11-18)" (Russian, one row per merge, what changed for the designer) and a short
`docs/port/HANDOFF.md` change-log entry, from `git log --merges --since="2026-11-18 00:00" main` and the commits e4558688, b9170e47:
FormationMantle ccdec8d3 (already listed), X6 props spawn 13be5a16, Antigravity 083 b41973d8 + 083c d72b9629, generated maps cold +
weather 6dbc9e38, G007 ramp fight e24a6d6f, CoverCrouch e4558688, chat idle banners, API tools second key b9170e47. Docs only.

## C - PeekMines diagnosis (report only, if time remains)
The night red `PeekMinesSmoke` (rev 5j rings): you found the Engineer's capsule blocking the Commander when both get cover orders on the same
tick. Diagnose it as a GAME bug (why do two operatives ordered to different rings block each other - cover slot reservation, path
avoidance, arrival radius?) with log evidence and file:line; propose the fix in docs/port/codex/outbox/045-light.report.md. Do NOT change the
smoke's checks or order timing.

Allowed paths: Source/CodexTactics/Private/Characters/SquadSubsystemMantleQueue.cpp, Source/CodexTactics/Public/Characters/SquadSubsystem*.h,
docs/port/TASK_BOARD.md, docs/port/HANDOFF.md, docs/port/codex/outbox/045-*
Report docs/port/codex/outbox/045-light.report.md. FINISH line in chat topic codex-045. Never push / merge main.
</content>
</invoke>
<invoke name="Bash">
<parameter name="command">cd "C:/Users/Zephyrus15Duo/Documents/Unreal Projects/CodexTactics"; git add docs/port/codex/inbox/045-light-cleanup-and-board.md; git commit -q -m "docs(port): Codex 045 - mantle log cache, TASK_BOARD / HANDOFF 2026-11-18, PeekMines diagnosis (light, last hours)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"; CODEX_AGENT=claude git push -q origin main 2>&1|tail -1; python Scripts/Tools/agent_chat.py post --as claude --to codex --topic codex-045 --text "Claude: new task 045 (light): docs/port/codex/inbox/045-light-cleanup-and-board.md on main $(git rev-parse --short HEAD). Read the 044 review in topic codex-044 first. 10 % rule stays." | tail -1