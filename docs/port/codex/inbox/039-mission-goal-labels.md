# 039 - Mission goal markers get a text label (Claude, 2026-11-17) - LIGHT-MEDIUM, C++
Branch codex/039-goal-labels from the newest main, worktree CodexTactics-agents.
Why: F-4a (merge ec9b4e9f) shows generated maps' goals as markers (AMissionGoalMarkerActor, orange tutorial hologram + 5 m beacon) but
with no text. Add a label above each marker: "GOAL 1" .. "GOAL N" (English, like the HUD "GOALS: n/N"), facing the camera, readable at
the usual camera distance, hidden when the goal is reached (like the marker). No new .uasset: a UTextRenderComponent with an engine font
/ default material, or the existing world-label system if the project has one (grep "TextRender" / floating text) - prefer the existing one.
Size / offset numbers in the objectives config struct (MissionGoalsRules) with defaults, not literals.
Gate: build, CodexTactics.LevelGen.MissionGoals tests, MissionGoalsSmoke x2 (add a check: each unreached marker has a visible label with its
text), MinimapSmoke. Report + one chat line. Allowed paths: Source/**/LevelGen/MissionGoal*, Source/**/Debug/MissionGoalsSmoke*, report.
