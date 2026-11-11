# 001 - Four C++ review fixes

Status: implemented and verified; ready for Claude review. No push or merge.
Branch: `codex/review-fixes` from `d47d2a29`.
Worktree: `C:/Users/Zephyrus15Duo/Documents/Unreal Projects/CodexTactics-cxfix`.

## Findings verified and fixed

All four assigned findings from Gemini review 011 were confirmed; none skipped.

- #2, `Source/CodexTactics/Private/Characters/SquadSubsystemRegroup.cpp`: `UpdateFastRegroup` uses the same guarded world time / zero fallback as `ArmFastRegroup`.
- #3, `Source/CodexTactics/Private/Characters/OperativeCharacterCombat.cpp`: request the injured raise before turning as before, but call `HoldShotForInjuredRaise` only after crawl, sniper movement, open-shot, grenade and aim/cover refusals clear. Refused attempts no longer increment `InjuredRaiseHolds` or stamp `LastInjuredRaiseHoldTime`. The early `DemandInjuredRaise` preserves the overlapping turn/raise and the authored 0.25 s raise. Its repeated demand in the helper is idempotent within the frame.
- #4, `Source/CodexTactics/Private/Characters/SquadSubsystem.cpp`: a critically wounded follower skips both stance copying and `MarkStanceFromSquad` on regroup, staying prone.
- #6, `Source/CodexTactics/Private/UI/CodexTacticsHUD.cpp`: skip the member when its capsule is absent.

`Source/CodexTacticsDev/Private/Debug/InjuredLocomotionDepotSmokeCommand.cpp` also changes its timing assertion to use the existing `FireOpenTime` measurement. The old assertion incorrectly treated the first counted raise hold as the start of the animation: once aim refusals stop counting as raise holds, aiming can consume most of the raise before a hold is counted. The timing bounds remain 0.15..0.6 s, the torso must start lowered, and a round must be fired. Initial extra Depot run failed only this old assertion for heavy M16 (0.02 s from first hold, still 0.27 s from opening fire); the corrected smoke passes, with no production timing change.

Documentation: four CHANGELOG rows, HANDOFF status, TANDEM log, and notes in `docs/systems/squad-and-orders.md`, `animation-setup.md`, `camera-hud-minimap.md`.

## Verification

- `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1`: **Succeeded**, exit 0. Final verification build: 138.00 s. No warnings diagnosed in the touched source files; engine-header deprecations and the existing non-preferred MSVC version warning remain. The first build was stopped at the designer's request and resumed after authorization.
- `Scripts/test.ps1 -Filter CodexTactics.Squad`: **32 tests, 0 failed**.
- `Scripts/test.ps1 -Filter CodexTactics.Characters.CriticalWound`: **6 tests, 0 failed**.
- `Scripts/test.ps1 -Filter CodexTactics.Animation.Injured`: **9 tests, 0 failed**.
- `Scripts/smoke_parallel.ps1 -Commands CodexTactics.FastRegroupSmoke,CodexTactics.InjuredLocomotionSmoke,CodexTactics.SoloStartSmoke -Jobs 2`: **3/3 PASS**, none needed a rerun. Durations: 55.5 / 77.2 / 55.1 s. Batch wall time 112.3 s.
- Additional `Scripts/smoke_parallel.ps1 -Commands CodexTactics.InjuredLocomotionDepotSmoke -Jobs 1 -ResultJson Saved/Logs/SmokeParallel-depot-review-fixes.json`: **PASS**, 59.6 s after the assertion correction; no retry. M16 light, RPG-7 light, M16 heavy: first round **0.27 s after opening fire** in every case. InjuredLocomotionSmoke independently measures **0.25 s** in both light standing and critical prone cases.
- `python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex`: **PASS** for the combined implementation and each staged commit, including the final commit.
- `git diff --check`: **PASS**.

Evidence in this worktree: `Saved/Logs/Build.log`, `Saved/Logs/AutomationTests.log`, `Saved/TestReport/`, `Saved/Logs/SmokeParallel-last.json`, `Saved/Logs/SmokeParallel-depot-review-fixes.json`, and `Saved/Logs/Smoke-{FastRegroupSmoke,InjuredLocomotionSmoke,SoloStartSmoke,InjuredLocomotionDepotSmoke}.log`. The automation runner overwrites its report/log for each filter; the last report is Animation.Injured.

## Commits / boundaries / open questions

Exactly four Conventional Commits, one per assigned fix (the smoke assertion correction is folded into the combat fix). First three: `5d4e123a`, `b8b55de6`, `0534e591`; final commit is `fix(hud): skip operative labels without a capsule` at branch HEAD and includes this report.
No assets, maps, gameplay data, configs or CLAUDE.md changed. The untracked content-pack junctions and copied untracked scalability file were created by the mandated worktree setup script and are not staged or committed. No editor UI opened, no asset saves, no push, no merge. Only the report is copied to the main checkout (and the required agent-chat command uses its own storage).
Open questions: none. Awaiting Claude's diff, tests and API review.
