# 152 ? Keep run orders running after traversal

Branch: `codex/152-keep-run-after-traversal`; base `9a83e2f5` (`origin/main` at lane start).

## Change

Confirmed the lead: `HandleMoveFinished` clears the current sprint when traversal stops a path. Ladder entry/queues and ledge drops additionally call `StopOperative`, so restoring sprint only at ladder exit would lose the original request.

Added `bOrderSprint` to retain the requested gait across path segments. Every resumed `RequestMove` reapplies it when sprint is allowed; the operative tick reevaluates temporary wound/carry/cold/stance/stealth restrictions during the order. Idle arrivals cannot start sprinting. A new move order replaces the intent; explicit stops and explicit gait overrides clear it. Traversal pauses preserve intent through ladder queues/entry and drop waits/entry. Window approach continuation uses the requested gait rather than the temporarily allowed gait. Entering stealth suspends running without discarding intent.

All existing mantle, window/fence vault, mantle descent, ladder and ledge-drop resumes converge on `RequestMove`. Formation path requests discard own-order gait intent, leaving Gemini 150's `StoodUpForSprint` and traversal guard in `USquadSubsystem::UpdateFollower` in control. The separate `AEnemyCharacter` movement implementation is unchanged; PMC operatives use the shared operative path logic.

The worker's first build also exposed an existing missing direct include in `OperativeCharacterAim.cpp` (`LogCodexTactics` undeclared when compiled independently). Added `CodexTactics.h`; no aim behavior change.

## Coverage

- Unit: `CodexTactics.Characters.OperativeUnits.OrderSprintSurvivesTraversal`. Exercises the real order entry, path completion, traversal pause, sprint restrictions/recovery, walk replacement, stop and formation handoff. World-free path activation is simulated; runtime traversal is covered below.
- Extended `CodexTactics.WindowDetourSmoke` on `/Game/Maps/Generated/L_Gen_Depot_1_X6`: after the existing single-window check, a run order goes through that broken window to a navmesh goal beyond the landing. The ordinary vault runs outside the explicit window approach completion guard, so it exercises `HandleMoveFinished` clearing current sprint. Requires observing the vault and sprinting again after it, without a second run order. Existing single-window routing and box-selected group checks remain.
- Older `CodexTactics.LadderClimbSmoke` / tower fixtures are archived in the current impact map and absent from this lane; the active mapped window smoke supplies the runtime regression.

## Validation ? PASS

- `Scripts/build.ps1 -Remote worker`: PASS (439.91 s UBT; missing direct aim include repaired after the first attempt).
- Unit portion of `Scripts/test.ps1 -Smart`: **689 tests, 0 failed**, including `CodexTactics.Characters.OperativeUnits.OrderSprintSurvivesTraversal`.
- `$env:CODEX_SMOKE_REMOTE='on'; Scripts/test.ps1 -Smart -SkipUnit -Base 9a83e2f52df86710a646eccfaf6c1fca5fa99774`: **16 mapped smokes, 0 failed, no retries, no known-fail exemptions** (327 s batch).
- `CodexTactics.WindowDetourSmoke` explicitly logged both the initial sprint and ?same run order is still sprinting on its path after the window vault?; the smoke's existing routing and group checks also passed.

All mapped smokes passed:

- `CodexTactics.FormationWallHugSmoke`
- `CodexTactics.FormationStairSmoke`
- `CodexTactics.RoofFightSmoke`
- `CodexTactics.TurnArenaLakeSmoke`
- `CodexTactics.LakePauseOrdersSmoke`
- `CodexTactics.CoverIndoorSmoke`
- `CodexTactics.LowCoverMoveLakeSmoke`
- `CodexTactics.TutorialAmbushLakeSmoke`
- `CodexTactics.TorsoDriftSmoke`
- `CodexTactics.FormationScanPitchSmoke`
- `CodexTactics.WindowDetourSmoke`
- `CodexTactics.WindowCoverFireSmoke`
- `CodexTactics.CutawayClickSmoke`
- `CodexTactics.FormationRunStanceSmoke`
- `CodexTactics.DropLinkReachSmoke`
- `CodexTactics.CoverSwitchLakeSmoke`

Evidence: `Saved/Logs/Build.log`, `Saved/Logs/AutomationTests.log`, `Saved/TestReport/index.json`, `Saved/Logs/SmokeParallel-last.json`, `Saved/Logs/Smoke-WindowDetourSmoke.log`; worker batch logs in `Saved/Logs/Worker/farm-20261127-101842-99988`.

The lane lacks the untracked Kit fixture used by some mapped smokes, so local batch preflight could not launch them. The successful batch explicitly shipped the committed source and built binaries under the worker lock and used its existing fixtures. No fixture files were added or changed. The impact base was pinned because shared `origin/main` advanced during the run.

`git diff --check` passed. Unity clash scanner reports seven existing collisions, all in unchanged files. The successful build retained two existing C4701 warnings in `CodexTacticsPlayerControllerOrders.cpp`; no new warning in changed code.

No maps, `.uasset`, Config, VFX or designer JSON edits, and no main checkout source changes. No branch refs were pushed. The standard worker sync wrapper invoked `git lfs push origin <commit>` when bundling the unpublished commit; this change introduces no LFS assets or new LFS references.
