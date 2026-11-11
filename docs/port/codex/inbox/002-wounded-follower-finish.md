# 002 - Finish "the squad slows to the wounded follower" (after 001)

Do this AFTER task 001 is reviewed. It CONTINUES existing work: branch `fix/wounded-follower-pace`, worktree
`C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-woundfollow` (already exists - do NOT create a new one), WIP commit
5dc9788b by a Claude agent that was stopped after a clean build, before the tests. Read `git diff main...HEAD` first, then finish it.
Rules: docs/port/codex/README.md (commit on this branch only, no push / merge, no assets / maps / configs).

## The designer's decision (2026-11-11)
A wounded operative who is NOT the leader and walks in the formation must not fall behind: while a wounded member is in the formation
the WHOLE squad slows to the most wounded member's pace (data toggle, default on; light wound x0.65, heavy x0.45; a critically wounded,
crawling member is excluded - say how). His torso keeps his formation look sector while his legs walk along the movement (the injured
8-direction walk). The leader-is-wounded case: the followers must not overrun him. Fast regroup on B must not drag a wounded man faster
than his allowed speed. (The WIP has SquadWoundPaceRules, SquadSubsystemWoundPace.cpp and WoundedFollowerSmokeCommand.cpp - check them.)

## Checks that must pass (results in the report)
- `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1`
- `Scripts/test.ps1 -Filter CodexTactics.Squad` (incl. the new SquadWoundPaceRulesTest) and `-Filter CodexTactics.Animation.Injured`
- `Scripts/smoke_parallel.ps1 -Commands CodexTactics.WoundedFollowerSmoke,CodexTactics.InjuredLocomotionSmoke,CodexTactics.FastRegroupSmoke,CodexTactics.FormationLabSmoke,CodexTactics.SoloStartSmoke -Jobs 2`
- `python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex`
Squash the WIP into a proper Conventional commit (or add follow-up commits), a CHANGELOG line, a note in docs/systems/squad-and-orders.md.
Frames from the smoke (game camera) of the wounded follower in formation -> copy 2 into the MAIN repo `Saved/Orchestrator/woundfollow/`
(the only allowed write into main besides your report). Report: `docs/port/codex/outbox/002-wounded-follower-finish.report.md`.
