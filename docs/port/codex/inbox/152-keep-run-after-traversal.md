# Codex 152 (L2; Lead reviews) - an order given as a RUN stays a run after ladders / windows / mantles (designer playtest 2026-11-27)

Designer: ordered an operative (double-click = run) off a roof; the path went down (ladder / drop) and through a window. He started running,
then WALKED after the traversal, so the designer had to double-click again. Rule: if an order started as a run, the operative keeps running
for that whole order after every traversal (ladder up / down, window vault, mantle, fence, drop), unless something legitimately forbids
running at that moment (CanSprint false: wounded / carrying / cold / prone / stealth) - then he walks while it is forbidden and runs again
when it is allowed, still within the same order. A new order replaces the gait as today.

Lead's lead (verify, do not trust): the path ends at the traversal -> AOperativeCharacter::HandleMoveFinished clears bSprinting
(OperativeCharacterMovement.cpp ~836); the resume after the traversal (ResumeMoveAfterLadder -> RequestMove, OperativeCharacterLadder.cpp;
window / mantle resumes in OperativeCharacterMantle.cpp, ledge drop resume) does not restore it. Likely fix: remember the order's gait
(e.g. bOrderSprint set in OrderMoveTo, cleared on a new order / StopOperative) and re-apply it on every traversal resume. Check formation
followers (Gemini 150 StoodUpForSprint / traversal guard in SquadSubsystem::UpdateFollower) and the enemy side are not broken.
- Unit test for the gait rule; a smoke step (extend an existing roof / window smoke, e.g. RoofLadderSmoke or WindowDetourSmoke - full names)
  proving: run order -> traversal -> still sprinting after it.
- Lane C:/Work/Lanes/CodexTactics-152, branch codex/152-keep-run-after-traversal from origin/main. Build via Scripts/build.ps1 -Remote worker,
  Smart tests, mapped smokes. Report docs/port/codex/outbox/152-keep-run-after-traversal.report.md; post "152 MERGE-READY | <sha> | ..."
  via Scripts/Tools/agent_chat.py --as codex --to claude. No main / push / maps / .uasset / Config / VFX / designer JSON. 10 %: commit, LIMIT, stop.
