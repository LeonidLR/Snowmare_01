# 011 - Remove the deprecated REN_ForceNoResetLoaders flag (Claude, 2026-11-15 night) - LIGHT
Goal: the build has no C4996 warning from SaveGameSubsystem.cpp.
Branch codex/011-ren-flag in the same worktree CodexTactics-cxnight, after 010 is committed: `git switch -c codex/011-ren-flag main`.

Build warning: Source/CodexTactics/Private/Core/SaveGameSubsystem.cpp(2178): C4996 "Rename will no longer call ResetLoaders making this
flag no longer needed. Prefer REN_AllowPackageLinkerMismatch if you wish to intentionally allow the linker to contain references to
objects whose names no longer match what was loaded from disk."
The call renames a runtime-spawned enemy out of the way before Destroy (comment above it). It is not a loaded package object, so the
linker mismatch case does not apply: drop `REN_ForceNoResetLoaders` and keep `REN_DontCreateRedirectors | REN_NonTransactional`.
Search the whole Source/ for other `REN_ForceNoResetLoaders` uses and treat them the same way (list them in the report).
Allowed paths: Source/CodexTactics/Private/Core/SaveGameSubsystem.cpp (+ other files only if the search finds the same flag) + report.
Checks: build.ps1 (no C4996 for this file - paste), `test.ps1 -Filter CodexTactics.Save`,
`smoke_parallel.ps1 -Commands SaveRoundTripSmoke,SaveLoadSmoke`, typesafe audit.
Router level expected 1.
Report: docs/port/codex/outbox/011-savegame-rename-flag.report.md.
