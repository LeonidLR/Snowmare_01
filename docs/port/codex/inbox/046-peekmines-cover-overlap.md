# 046 - PeekMines: simultaneous cover orders onto a slot being vacated (game fix from your 045 diagnosis) - MEDIUM, C++
Designer exception 2026-11-18 (today only): you work until 10 % of your DAILY norm is left (not the weekly 10 %), then stop, commit, report.
Base: main (newest, contains your 045 f4b5edde). Branch codex/046-peekmines. Worktree CodexTactics-agents.

## 0 - close 045 (first, quick)
Confirm the 045 gate A on main: FormationMantleSmoke 2/2 + MantleSmoke. Post one line in topic codex-045.

## 1 - the game fix
Your diagnosis (docs/port/codex/outbox/045-light.report.md section C): when the Commander leaves the Engineer's spot for his own ring and
the Engineer gets a cover order onto the spot being vacated in the same tick, their capsules overlap and the order fails / stalls
(Source/CodexTactics/Private/Characters/OperativeCharacterCover.cpp ~92 and the temporary-ignore / re-snap branch you named).
Fix it in the GAME: a cover order onto a slot whose occupant is leaving waits / queues until the slot is free (or the arrival re-snap
checks the capsule overlap at the end point and keeps the order pending instead of failing), so players who give orders at once get the
same result. Numbers (wait time, overlap tolerance) in the existing cover config JSON, no new literals.
- The smoke on main is the ORIGINAL PeekMinesSmoke (your bb2239a7 changes were not merged) - do not change its checks or order timing.
- Unit test for the slot hand-over rule if it is pure logic.

## Gate
build (Scripts/build.ps1 -Remote worker, exit 2 -> local), PeekMinesSmoke 2/2, CoverSmoke, CoverHugSmoke, GroupCoverSmoke, CornerHoldSmoke,
StealthModeSmoke, Scripts/test.ps1 -Smart, typesafe_triage --audit-diff --agent codex.
Allowed paths: Source/CodexTactics/{Public,Private}/Characters/OperativeCharacterCover*, Source/CodexTactics/{Public,Private}/Characters/CoverSlot*,
the cover config JSON under Content/Data, Source/CodexTacticsTests/Private/** (new tests), docs/port/codex/outbox/046-*
Report docs/port/codex/outbox/046-peekmines.report.md. FINISH in topic codex-046. Never push / merge main.
</content>
</invoke>
<invoke name="Bash">
<parameter name="command">cd "C:/Users/Zephyrus15Duo/Documents/Unreal Projects/CodexTactics"; git add docs/port/codex/inbox/046-peekmines-cover-overlap.md; git commit -q -m "docs(port): Codex 046 - PeekMines cover-slot hand-over game fix; designer exception today: Codex works down to 10 % of the daily norm

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"; CODEX_AGENT=claude git push -q origin main 2>&1|tail -1; python Scripts/Tools/agent_chat.py post --as claude --to codex --topic codex-046 --text "Claude (designer 2026-11-18, exception TODAY only): you keep working until 10 % of your DAILY norm is left (not the weekly 10 %), then stop, commit, report. Task 046: docs/port/codex/inbox/046-peekmines-cover-overlap.md on main $(git rev-parse --short HEAD) - first confirm the 045 gate (FormationMantleSmoke 2/2), then the PeekMines game fix from your own diagnosis." | tail -1