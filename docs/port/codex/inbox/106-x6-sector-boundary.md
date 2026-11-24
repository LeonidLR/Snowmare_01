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

## LEAD ADDENDUM 2026-11-24 (Fable review) - FROZEN SCHEMA, overrides the text above
1. Static sector data in its OWN file Content/Data/LevelJson/L_Gen_Depot_1_X6.sectors.json (Gemini 105 writes it; nobody else):
   {"map": "L_Gen_Depot_1_X6", "sectors": [{"id": 1..6, "name": "Sector N", "box_cm": {"min": [x, y], "max": [x, y]}  // FPmcArenaPlayArea format
     "z_cm": {"min": z, "max": z}, "starts": [2-3 x {"pos": [x,y,z], "yaw": deg}], "pmc_posts": [2-3 x {"id", "pos", "yaw", "kind"}],
     "pmc_squad": {"count": 3, "weights": "pmc_commander.json", "posture": "..."}, "contents": {"enterable_buildings": n, "covers": {kind: n},
     "fences_m": n, "ladders": n}, "size_m": [w, h]}]}. Minimum box 60 x 60 m; state each box size.
2. The SELECTION is not in any LevelJson: Saved/Training/active_sector.json (written by the TG tab 107) -> overridden by -Sector=<id> /
   Codex.Sector <id>. The X6 LevelJson stays untouched (it is the designer's file).
3. Active sector at runtime (106): top-level pmc_squads and pressure/ambush points are OFF, only the sector squad spawns, with a random
   pick of start + post (seeded -> real variety, not one memorised opening). Units outside on a mid-session switch are teleported to a start.
4. Boundary (106): navigation outside the box is excluded / cost-filtered so paths never leave (not only a collision wall); ALL AI targets
   clamped: cover picks, flank / retreat points, lures, ladder / window / mantle targets. Camera bounds = box + margin.
5. Recording: the session header carries sector id + box (SessionRecordingRules); 107 item 4 reads it.
6. 107 shows count / posture read-only (editing stays in the Wave Editor) - only the sector selection is written.

DESIGNER DECISION 2026-11-24 ~16:15: WALLS + NAVIGATION (hard boundary: blocking walls on the box edges + nav excluded outside + all AI targets clamped). Not the soft boundary.
