# 074 - RetreatFacingSmoke: operatives end 65 deg / 115 deg off the required facing (Claude, 2026-11-16) - MEDIUM, C++
Base: main. Branch gemini/074-retreat-facing.
Why: Codex 021/022 fixed the smoke setup (rifles; they refire 5/5) but the smoke still fails its final facing check: after the retreat
the two operatives face 65 deg and 115 deg away from the pack and do not turn ("turning 0"). Reports:
docs/port/codex/outbox/021-retreat-facing-diagnosis.report.md, 022-retreat-facing-smoke-loadout.report.md.
Do: reproduce (`smoke_parallel.ps1 -Commands RetreatFacingSmoke -NoRetry -Jobs 1 -ExtraArgs -Multiprocess` x3, quote FAIL lines);
find why they do not turn back to the threat after the retreat ends (retreat state never cleared? look-hold 059b holding the old facing?
facing target lost when the pack is out of sight? the fire gate a48e7540 waiting with no turn request?) with file:line + log numbers;
fix the cause (not the smoke). Then 3/3 PASS and remove ONLY its knownFail entry in Scripts/test_map.json.
Allowed paths: the retreat / facing code you prove is at fault (Source/CodexTactics/Private/Characters/OperativeCharacter*.cpp except
OperativeCharacterAim.cpp and OperativeCharacterFireGate.cpp - for those two write "needs Claude: <what>"), its JSON config,
Source/CodexTacticsTests (a unit test), Scripts/test_map.json (that entry), report.
Checks: build, test.ps1 -Filter CodexTactics.Characters, RetreatFacingSmoke x3, plus FireGateSmoke, LookHoldSmoke, CoverSmoke once.
