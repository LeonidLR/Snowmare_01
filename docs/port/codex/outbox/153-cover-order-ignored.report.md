# 153 — cover ghost confirmation

Status: **MERGE-READY for Lead review.** Branch: `codex/153-cover-order-ignored` (base `0fd02131`). No merge or branch push performed.

## Cause and evidence

`ExecuteClickPreview` logs “carries out the cover ghost” before dispatch. `ResolveClickCoverSlot` previously showed the raw clicked face without the near-side low-cover approach rule. `ConfirmCoverPreview` then applied `ResolveLowCoverApproach` and returned silently when its reverse-face probe failed, before the double-click verdict log or any movement order. Thus a valid-looking ghost did not guarantee a usable approach. Confirmation also silently discarded invalid targets and only reported `Unreachable`, leaving `NoController` / `Refused` without controller feedback.

The supplied excerpt narrows the state evidence: at 07:13:06 Engineer explicitly leaves cover for a move, before the silent 07:13:08 confirmation; at 07:13:56 a ground sprint precedes the other silent confirmation. These are moving cases, not proof of an in-cover gate. At 07:14:03 the excerpt contains the first click only, with no second-click execution line; that instance cannot establish where a second click went. The exact play-copy X6 scene and its actor geometry were not reproduced.

Double-click timing selects sprint / switch behavior, not whether the armed ghost executes: a slow second click also confirms it. `OrderTakeCover` already replaces a running move and leaves another cover before moving. There is no general in-cover / moving / leaving-cover rejection at that order entry. The identified drop occurs earlier, in controller approach validation. Same-slot confirmation now reports “Already at this cover slot” only when settled; moving / shimmying and stale slots cannot take that shortcut.

## Changes

- Resolve the low-cover near face before ghost display and same-wall classification. Failed approach probes retain cover semantics so confirmation can explain refusal rather than issue an accidental ground move.
- Commit same-wall ghosts directly through cover confirmation instead of redispatching an ordinary click. `OrderTakeCover` retains the existing shimmy behavior.
- Add a pure commit decision: issue the order, report already at this slot, report invalid target, or report unavailable approach. Walking / leaving cover cannot qualify as settled at the current slot.
- Report early approach failures and all rejected single-operative confirmation results in the on-screen game-message feed and log.
- Add `CodexTactics.Tactics.ClickPreview.CoverCommit` unit coverage for another cover, moving/leaving with stale slot data, same-wall movement, already-there, missing target and failed approach.
- Extend `CodexTactics.CoverSwitchLakeSmoke` with runtime low-cover fixtures: settled in A -> player double-click B -> arrive B; ground click leaves B and walks 3 m -> player double-click B -> replace destination and return. A far-face click on thick low cover A exercises the unavailable approach and checks the on-screen message history plus preservation of cover B. Existing high-cover dash checks remain.

## Validation

- `Scripts/build.ps1 -Remote worker`: passed twice (404.59 s and final 402.41 s compile). Final build includes refusal diagnostics and the smoke assertion. Log: `Saved/Logs/Build.log`.
- `Scripts/test.ps1 -Smart -SkipSmokes`: **369 passed, 0 failed**, 79 s. Includes `CodexTactics.Tactics.ClickPreview.CoverCommit`; compiled from the first remote build, with no subsequent decision/unit-test changes. Artifacts: `Saved/Logs/AutomationTests.log`, `Saved/TestReport/`.
- `CODEX_SMOKE_REMOTE=on Scripts/test.ps1 -Smart -Base 0fd02131c44e37f205435192a29e472d645082c1 -SkipUnit`: **9 passed, 0 failed, no reruns**, 215 s smoke wall time. Source checkpoint `8b7ac7a2` and its compiled DLLs were explicitly shipped under the worker lock. The final amendment changes this report only.

| Mapped smoke | Result |
|---|---|
| `CodexTactics.TutorialAmbushLakeSmoke` | PASS |
| `CodexTactics.TurnArenaLakeSmoke` | PASS |
| `CodexTactics.WindowStairHoverSmoke` | PASS |
| `CodexTactics.SaveCoverageLakeSmoke` | PASS |
| `CodexTactics.LakePauseOrdersSmoke` | PASS |
| `CodexTactics.FormationWallHugSmoke` | PASS |
| `CodexTactics.LowCoverMoveLakeSmoke` | PASS |
| `CodexTactics.CoverSwitchLakeSmoke` | PASS |
| `CodexTactics.PeekMinesSmoke` | PASS |

Artifacts: `Saved/Logs/SmokeParallel-last.json`, `Saved/Logs/Worker/farm-20261127-110356-92912/`, individual `Saved/Logs/Smoke-<Name>.log`. The extended smoke explicitly passes in-cover replacement at `Smoke-CoverSwitchLakeSmoke.log:1727`, walking replacement at line 1745, actual arrival at line 1750, and on-screen approach refusal / preservation of the current cover at line 1758.

An earlier queued smoke wrapper was cancelled before launching engines when another lane acquired the worker after the same-build check. Its FAILED wrapper summary is cancellation, not nine executed failures; the explicit-sync replacement run above is the accepted result.

`git diff --check` and `Scripts/test.ps1 -CheckMap`: passed. Unity-name scan found seven pre-existing clashes in unchanged files; no changed-file clash. Both builds report existing C4701 warnings in unchanged `CodexTacticsPlayerControllerOrders.cpp` (lines 1050 and 1150).

## Scope / overlap

Runtime edits: `CodexTacticsPlayerControllerClickPreview.cpp`, `CodexTacticsPlayerControllerCover.cpp`, `ClickPreviewRules.cpp/.h`. Test edits: `ClickPreviewRulesTest.cpp`, `CoverSwitchLakeSmokeCommand.cpp`.

No file overlap with Gemini 135b: no `OperativeCharacterCover*.cpp` or `OperativeCharacterCornerAim.cpp` edits. Controller confirmation still calls those existing cover-order APIs. No maps, assets, Config, VFX or designer JSON changed. Runtime smoke fixtures are transient and are never saved.

Not checked: visual corner animation, rendered screenshots, exact designer play-copy X6 interactions, full test suite / all smokes. Existing near-side low-cover policy and geometry probe remain intact: an unavailable approach now gives an explicit refusal.
