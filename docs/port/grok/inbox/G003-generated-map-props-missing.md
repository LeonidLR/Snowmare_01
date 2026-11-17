# G003 - Generated maps X4 / X6: the designer sees no barrels / loot / mines (Claude, 2026-11-17) - MEDIUM, C++ + TS
Branch grok/G003-gen-props from the newest main, worktree CodexTactics-grok (after G002).
Designer (playtest of main, 11-17): «opened X4 and X6 - no barrels, no loot, nothing».
Facts (Claude, from the editor logs Saved/Logs/CodexTactics*.log):
- Props are spawned at runtime only (PIE / game), by UCodexLevelPropsSubsystem (Source/CodexTactics/Private/LevelGen/CodexLevelPropsSubsystem.cpp)
  from Content/Data/LevelGen/props/<Map>.props.json - rule LAYERS (source auto, 5 layers, 0 overrides), the layout rebuilt at runtime.
- PIE on L_Gen_Depot_1_X6 (14:43 and 15:06): `[Props] spawned 72 props ... dropped: 0`, plus `layout.cut: 'poly' edge longer than 30000 cm
  (exact geometry limit)`. Training Ground's autogen (Gemini 076) reported 275 props for X6 and 242 for X4 - runtime makes far fewer.
- On L_Gen_Depot_1 (the designer's map) earlier runs spawned 57; one run at 15:12 spawned 0.
- X4: no PIE line found in today's logs.
Do:
1. PLAN: run the game headless on L_Gen_Depot_X4 and L_Gen_Depot_1_X6 (a smoke or Codex.Props.Dump - there is a dump in the subsystem) and
   list per layer / kind: expected count (Training Ground's autogen for the same file - run its CLI) vs spawned vs skipped (meshless with
   bRequireMesh, nav-snap misses, outside the layout, the 30000 cm poly limit). Check where they spawn: world positions vs the map bounds
   (expanded maps have an offset / bigger extent - are props placed in the right world space, on the ground, not under it?).
2. Fix the cause(s) so the runtime places the SAME props as Training Ground shows (same seed / rules), visible on the ground. Numbers in JSON.
3. Smoke `GeneratedPropsSmoke` (register in Scripts/test_map.json): on X4 and X6 the spawned count per kind matches the TG autogen within 5 %,
   every spawned prop is within the map bounds and within 30 cm of the ground / roof under it, and has a visible mesh.
Gate: build, the smoke 2x, MissionGoalsSmoke, LevelGenRouteSmoke, TG npm test, typesafe audit --agent grok. Report + FINISH.
Allowed paths: Source/CodexTactics/**/LevelGen/CodexLevelProps*, Source/CodexTactics/**/LevelGen/Props*, Tools/TrainingGround/src/levelgen/props/**,
Source/CodexTacticsDev/Private/Debug/GeneratedPropsSmoke*, Scripts/test_map.json, docs/port/grok/outbox/G003-*. Never edit
Content/Data/LevelGen/props/L_Gen_Depot_1.props.json (the designer's file) or any map.
