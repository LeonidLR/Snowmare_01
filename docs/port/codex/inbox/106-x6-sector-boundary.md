# Codex 106-x6-sector-boundary (after 103; before 104 / 070)

Designer 2026-11-24: X6 gets 6 training SECTORS (drawing docs/port/gemini/inbox/105-x6-sectors-designer.webp; data by Gemini 105 -
agree the JSON shape with Gemini in chat first: a "sectors" array in the X6 LevelJson, box_cm / player_start / pmc_squad).
Runtime (C++, CodexTactics module):
0. Sector ids 1-6 (designer): top row 1, 2, 3; bottom row 4, 5, 6 as in his top view.
1. Sector choice: LevelJson "active_sector" + console Codex.Sector <id|0> (+ -Sector=<id> command-line arg for smokes / trainer);
   0 / absent = today's X6 unchanged.
2. On an active sector: the player squad spawns at its start, only that sector's PMC squad is scheduled, an invisible blocking
   boundary on the box edges (pawns of both teams cannot leave; projectiles not blocked), a visible line on the ground + the
   minimap box; PMC posts / hunt / patrol targets clamped inside the box; session recording tags the sector id.
3. Tests: unit (sector parse, clamp), one smoke SectorBoundarySmoke on X6 (squad ordered outside stops at the edge; PMC stays in).
Branch codex/106-x6-sector-boundary; build, -Smart, smoke on the worker. Report in outbox + chat FINISH.
