# 039 — Mission goal marker labels

Ready for review on codex/039-goal-labels, based on main531acb15 (038 merged).

Unreached mission goals show English GOAL1..N (rendered with a space: GOAL 1) above their hologram. Each UTextRenderComponent faces the current camera, following the existing InteriorExitArrowActor world-label pattern, with its built-in engine font/default material. Sequential labels follow the parsed goal order, including custom goal ids.

The objectives config FMissionGoalsFile owns LabelHeightCm550 and LabelWorldSizeCm80 defaults. Optional root keys label_height_cm / label_world_size_cm override them (finite representable values; nonnegative height, positive size). No new assets or Config/data-file edits. The subsystem configures each label before FinishSpawning. Existing pre-BeginPlay marker visibility restoration remains; SetGoalShown explicitly hides/shows the text and the parent controls actor visibility/ticking, so reached goals stop rendering and ticking.

Changed only the six allowed source paths: MissionGoalMarkerActor header/cpp, MissionGoalsRules header/cpp, MissionGoalsSubsystem.cpp and MissionGoalsSmokeCommand.cpp; plus this report. The smoke now checks every goal's exact sequential text and shown/hidden state initially and after each reached goal; all prior marker/minimap/HUD/events/level-end assertions remain.

| Gate | Result |
|---|---|
| Build | PASS126.63s,17/17 actions, exit0 |
| CodexTactics.LevelGen.MissionGoals |5/5 PASS: CommittedFiles,HudLine,Order,Parse,Reach |
| MissionGoalsSmoke standalone1/2 |PASS48.2s /42.0s, Jobs1/NoRetry/Multiprocess |
| Label assertions |Both GOAL1/2 visible initially; each hides on reaching; unreached one remains visible;6 assertions/run |
| MinimapSmoke |PASS44.4s, Jobs1/NoRetry/Multiprocess |
| Scope/whitespace |Exact six source paths checked; git diff --check PASS |
| Final staged TypeSafe audit |Required PASS before commit |

Evidence: Saved/Logs/task039/build-{wrapper,engine}.log; unit-{wrapper,engine}.log and unit-test-report/; MissionGoals-1/2-{wrapper,engine}.log + result.json; Minimap-{wrapper,engine}.log + result.json. Actual engine RESULT markers checked. Gates are headless; camera-facing implementation reuses the existing project pattern. UE_SKIP_UBT_SDK_SETUP=1 was process-local for unit startup (existing proven workaround); all standalone smokes also used Multiprocess. Shared build/designer priority was respected, no process interrupted. Claude634 temporarily held his next builds; this build succeeded and all gates completed within that window.

Router: level3/confidence0.89 | primary main session | pass, no redo. App usage checked before final commit:38% primary/29% secondary remaining, above designer10% stop threshold. No local model, push, main checkout write or merge into main.
