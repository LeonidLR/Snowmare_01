# 040 — Reached mission goals in saves

Ready for review: codex/040-goals-save from main c21583c6.

- Existing USaveGameSubsystem JSON integration: optional world_state.mission_goals object maps the canonical current map package (PIE prefix removed, matching save metadata) to reached goal ID strings. No version bump; format remains3. Only active goals are captured.
- Restore is inside the existing Quests phase: missing field/map means empty progress; non-string/unknown IDs ignored. UMissionGoalsSubsystem restores flags, marker/label visibility and HUD silently, latches completed state when all are reached, and emits no goal/completion events or level end. Pending travel/applying-save guards prevent default-spawn goal evaluation before restoration.
- Save edit scope announced before edits in chat638: only Private/Core/SaveGameSubsystem.cpp. Other source changes are the allowed MissionGoalsSubsystem header/cpp and MissionGoalsSmokeCommand.cpp. No SaveGameRules, restore-order table, policy, Config, assets or designer-map changes.
- Smoke uses isolated Saved/SmokeMissionGoals slots. It reaches one goal, saves, clears state and loads to1/N with hidden marker/label and correct HUD, then reaches the rest and completes once. Full-progress save/load remains N/N without replaying reach/completion events. Stronger final negative controls start with nonzero progress, then verify foreign-map IDs and a legacy field-less file clear it.

| Gate | Actual result |
|---|---|
| Runtime build |PASS115.79s,12/12 actions |
| MissionGoals + Save unit tests |12/12 PASS (5 MissionGoals,7 Save.Policy, including Version/NoAutosave/RestoreOrder) |
| Initial MissionGoalsSmoke |PASS64.8s |
| SaveRoundTripSmoke |PASS66.5s |
| SaveLoadSmoke |PASS47.6s |
| Final smoke-only rebuild |PASS32.70s,4/4 actions |
| Final stronger MissionGoalsSmoke |PASS93.9s; nonzero legacy/map-isolation preconditions exercised |
| Scope / whitespace |Exact four source paths, git diff --check PASS |
| Final staged TypeSafe audit |Required PASS before commit |

All standalone smokes Jobs1/NoRetry/Multiprocess, actual engine PASS markers checked. Runtime did not change after the first full green gate; only the stronger smoke was rebuilt/rerun. Evidence Saved/Logs/task040/: build-*, unit-* + unit-test-report/, <Smoke>-{engine,wrapper}.log + result.json, stronger-smoke-build-*, stronger-MissionGoals-*, fixtures/. Actual JSON fixtures checked: partial stores GOAL_1, full stores both IDs, legacy field absent, foreign map has matching GOAL_1 under another package key. Process-local SDK validation skip used only for unit/gate startup; no persistent settings or priority override.

Router3/confidence0.71 | primary main session | pass; no local model. Last usage check13% primary/25% secondary remaining, reset primary14:23 Jerusalem. Finish current commit then wait with reserve; no new task before reset if at10%.
