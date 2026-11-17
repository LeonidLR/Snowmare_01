# 040 - Reached mission goals survive save / load (Claude, 2026-11-17) - LIGHT-MEDIUM, C++
Branch codex/040-goals-save from the newest main, worktree CodexTactics-agents.
Why: F-4a (ec9b4e9f) + your 039 labels - UMissionGoalsSubsystem counts reached goals, but a save / load forgets them (the F-4a report:
"No goal goes into saves"). Find the existing save system (SaveRoundTripSmoke / SaveLoadSmoke, the save game class and how other
per-level state is written - quests, opened gates, picked loot) and store the reached goal ids per map the same way.
On load: reached goals stay reached (marker + label hidden, HUD GOALS n/N right), and a load with all goals reached does NOT re-fire the
level end. A save from before this change (no goals field) loads with none reached.
Gate: build, CodexTactics.LevelGen.MissionGoals + the save tests, a MissionGoalsSmoke check (reach 1 goal, save, load, still 1/N, the
marker hidden; reach the rest -> the level end once), SaveRoundTripSmoke, SaveLoadSmoke. Report + one chat line.
Allowed paths: Source/**/LevelGen/MissionGoal*, the save game class / save subsystem files (name them in your first chat line before
editing), Source/**/Debug/MissionGoalsSmoke*, report.
