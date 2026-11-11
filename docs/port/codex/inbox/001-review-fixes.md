# 001 - Four small C++ fixes from Gemini's review (the trial task)

Branch `codex/review-fixes`, worktree `-Name cxfix` (see the README). Source of the findings:
`docs/port/gemini/outbox/011-cpp-review.report.md` (read findings 2, 3, 4, 6 there - full context and line numbers). Verify each one
against the code first; if a finding is wrong, skip it and say why in the report.

1. **#2** `Source/CodexTactics/Private/Characters/SquadSubsystemRegroup.cpp` ~line 122 `UpdateFastRegroup`: `GetWorld()->GetTimeSeconds()`
   without a null check (the sibling functions guard it). Guard it the same way.
2. **#3** `Source/CodexTactics/Private/Characters/OperativeCharacterCombat.cpp` ~lines 804 / 926: `HoldShotForInjuredRaise()` bumps
   `InjuredRaiseHolds` / `LastInjuredRaiseHoldTime` before the other fire refusals (crawl hold, sniper move hold, aim cone), so a shot
   refused for another reason still counts as an injured raise. Evaluate the raise hold only after those refusals - WITHOUT changing
   when a real wounded shot is delayed (raise_to_fire_s stays 0.25 s; InjuredLocomotionSmoke / InjuredLocomotionDepotSmoke measure it).
3. **#4** `Source/CodexTactics/Private/Characters/SquadSubsystem.cpp` ~lines 1086-1091 `ExitSoloMode`: a critically wounded operative
   (`IsCriticallyWounded()`) gets `SetStance(leader stance)` + `MarkStanceFromSquad()` although the critical-wound rule refuses it.
   Skip both for a critically wounded man (he stays prone).
4. **#6** `Source/CodexTactics/Private/UI/CodexTacticsHUD.cpp` ~line 1593 `DrawOperativeLabels`: `Member->GetCapsuleComponent()`
   dereferenced without a check. Skip the member when there is no capsule.

## Checks that must pass (put the results in the report)
- `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1` (Succeeded, no new warnings in the touched files)
- `Scripts/test.ps1 -Filter CodexTactics.Squad` and `-Filter CodexTactics.Characters.CriticalWound` and `-Filter CodexTactics.Animation.Injured`
- `Scripts/smoke_parallel.ps1 -Commands CodexTactics.FastRegroupSmoke,CodexTactics.InjuredLocomotionSmoke,CodexTactics.SoloStartSmoke -Jobs 2`
- `python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex`
One commit per fix (4 commits) + a line in `docs/port/CHANGELOG.md`. Report: `docs/port/codex/outbox/001-review-fixes.report.md`.
