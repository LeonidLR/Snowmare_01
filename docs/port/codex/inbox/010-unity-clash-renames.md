# 010 - Rename two unity-build name clashes (Claude, 2026-11-15 night) - LIGHT
Goal: `Scripts/build.ps1` prints no `[unity-clash]` warnings.
Branch codex/010-unity-clash, ONE worktree for all night tasks: `new_agent_worktree.ps1 -Name cxnight -Branch codex/010-unity-clash`.

The build reports (find_unity_clashes):
- `Defaults` (function/variable): Source/CodexTactics/Private/AI/PmcOperatorRules.cpp:25 (`inline FPmcOperatorConfig Defaults()`, used at :152)
  and Source/CodexTactics/Private/Core/HitchGateRules.cpp:7 (`const FHitchGateLimits Defaults;`, used in the CVar lines below it).
- `MakeSlot` (function): Source/CodexTacticsTests/Private/Combat/PeekSightRulesTest.cpp:15 and
  Source/CodexTacticsTests/Private/Tactics/CoverFacingTest.cpp:17.

Do: rename the file-local name in ONE file of each pair to a file-specific name (e.g. `PmcOperatorDefaults`, `MakeCoverFacingSlot`),
update its uses in the same file only. Nothing else.
Allowed paths: exactly those 4 files (you may change only 2 of them) + your report.
Checks: build.ps1 (no unity-clash lines - paste the tail), `test.ps1 -Filter CodexTactics.Combat.PeekSight`,
`test.ps1 -Filter CodexTactics.Tactics.CoverFacing`, `test.ps1 -Filter CodexTactics.AI.PmcOperator`, `test.ps1 -Filter CodexTactics.Core.HitchGate`
(if a filter finds 0 tests, find the right prefix with a grep and say so), typesafe audit.
Router level expected 1 -> your cheapest model.
Report: docs/port/codex/outbox/010-unity-clash-renames.report.md.
