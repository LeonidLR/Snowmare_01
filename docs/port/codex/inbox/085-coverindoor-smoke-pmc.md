# Codex 085-coverindoor-smoke-pmc

**TAKEN BY LEAD 2026-11-24 15:00 (Codex was not running) - Codex: skip it.**

Assigned by Lead 2026-11-24. Small C++ smoke fix (Source/CodexTacticsDev only).

Problem: CodexTactics.CoverIndoorSmoke FAILs on the worker since 076 made StoryShowcase_Kit PMC-only
(run 0724b, log Saved/Logs/Worker/20261124-135555/Smoke-CoverIndoorSmoke.log): the level's PMC encounter
spawns a 5-man squad at Y=-3800, it kills the smoke squad, "Commander killed in action -> mission failed",
then "Smoke FAIL: timeout in step 11".

Fix: in the CoverIndoorSmoke setup cancel the level's PMC encounter before the squad moves, same pattern as
InjuredLocomotionDepotSmokeCommand.cpp ~line 354 (UPmcEncounterSubsystem::CancelPending; despawn any PMC
already spawned if the encounter fired before the hold ended). Do not change the level JSON or 076 behaviour.
Check the other smokes on L_Gen_StoryShowcase_Kit in Scripts/test_map.json (CutawayClick, RoofCutawayStorey,
WindowStairHover, FormationStair, FormationWindow) for the same exposure and fix those that need it.

Done = build, -Smart tests, CoverIndoorSmoke + the touched smokes PASS on the worker; branch codex/085-coverindoor-pmc.
Rules: docs/port/TANDEM.md 'WORK PROCESS'. Worktree under C:/Work/Lanes; report in outbox + chat FINISH.
