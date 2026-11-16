# 019 - CoverSmoke wall-end stand-off flake (Claude, 2026-11-16) - MEDIUM, diagnosis first
Why: CoverSmoke fails now and then, more often under load (parallel smokes / other builds):
`Smoke FAIL: ... standing the _R stand-off from the real wall end (72-82 cm, stand-off 28; the clip leans 40 cm out)`.
It fails on the pre-fire-gate base too (e40370ab, 2 of 4 at -Jobs 6), so it is old. Claude's agent's guess: the corner shimmy stops
~30 cm along its path and the check runs before the operative settles onto the slot (a timing race, not a wrong slot).
Branch codex/019-coversmoke-flake (from main), worktree CodexTactics-agents.

Do:
1. Reproduce: `smoke_parallel.ps1 -Commands CoverSmoke -NoRetry -Jobs 1` x3 and `-Commands CoverSmoke,FireGateSmoke,QuickFireSmoke,
   SuppressionSmoke,CornerHoldSmoke,PistolSmoke -NoRetry` x2 (load). Paste the result lines + the FAIL line(s).
2. Find the stage in Source/CodexTacticsDev/Private/Debug/CoverSmokeCommand.cpp (grep "stand-off from the real wall end"), read ~60 lines
   around it: what does the check wait for before measuring (fixed time? "arrived" flag? cover state)? Log (add a temporary
   UE_LOG in the smoke only) the distance to the wall end every 0.25 s from the order to the check in a FAIL run.
3. If it is a timing race in the SMOKE (measuring before the operative reached / settled), make the check wait for the real settle
   condition (cover state entered + speed ~0 for N frames, with a hard timeout that still FAILs) - never widen the 28 cm stand-off.
   If the operative really ends at the wrong spot (gameplay), STOP: write "needs Claude: <file:line, numbers>" - no gameplay edits.
4. Prove: CoverSmoke 5/5 PASS at -Jobs 1 and 2/2 in the loaded batch above.
Allowed paths: Source/CodexTacticsDev/Private/Debug/CoverSmokeCommand.cpp, report docs/port/codex/outbox/019-coversmoke-standoff-flake.report.md.
Router level expected 2-3.
