# 054 - Box select works in formation and switches the squad to SOLO (designer 2026-11-15)
Суть: рамкой можно выбрать бойцов и в режиме формации; выбранные сразу становятся группой соло-бойцов, а формация распускается.
Реестр: squad-select
Base: main (664d78b2 or newer); branch gemini/054-box-select-solo. C++ - first commit = PLAN with the exact files (selection in
Core/CodexTacticsPlayerControllerSelection*.cpp, the formation / solo switch in Characters/SquadSubsystem*.cpp); wait for Claude's OK.

## Goal
1. Box select (mouse drag rectangle) works again while the squad is in formation and while the formation moves (today it is blocked).
2. A box select made in formation mode switches the squad to SOLO mode (the same transition as leaving formation today): the boxed
   operatives become the current multi-selection - a group of independent solo operatives (each moves to its own point / spread like
   the existing solo group move), no formation shape, no leader-follow. Operatives not in the box keep their solo state where they stand.
3. Re-forming stays as today (B / formation order). Stealth rules unchanged (stealth only in solo - box-select into solo may then be
   followed by STEALTH).
## Checks (paste into docs/port/gemini/outbox/054-box-select-solo.report.md)
- Unit tests for the mode switch decision; smokes: GroupSelectSmoke + RealtimeSelectSmoke + SquadControlSmoke extended (box in
  formation -> mode SOLO, selected = boxed, a move order sends each to its own point), FormationLabSmoke, SoloStartSmoke,
  FastRegroupSmoke still PASS.
- gemini_gate.ps1 -Task 054 with -SonnetReview (squad / formation = shared core -> Opus is auto-selected).

## Allowed paths
- Source/CodexTactics/Public/Characters/SquadOrderRules.h
- Source/CodexTactics/Private/Characters/SquadOrderRules.cpp
- Source/CodexTactics/Public/Characters/SquadSubsystem.h
- Source/CodexTactics/Private/Characters/SquadSubsystem.cpp
- Source/CodexTactics/Private/Core/CodexTacticsPlayerController.cpp
- Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerSelection.cpp
- Source/CodexTacticsDev/Private/Debug/SquadControlSmokeCommand.cpp
- Source/CodexTacticsTests/Private/Characters/SquadOrderRulesTest.cpp
- docs/port/gemini/inbox/054-box-select-formation-to-solo.md
- docs/port/gemini/outbox/054-box-select-solo.plan.md
- docs/port/gemini/outbox/054-box-select-solo.report.md
- docs/port/gemini/outbox/054-opus-review.md
