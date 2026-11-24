# Codex 107-tg-ai-training-tab (after 106; fold 104 into it)

Designer 2026-11-24: a new Training Ground tab "Тренировка ИИ" (AI training) so he controls where the bots train:
1. Map picker: only the 5 kept maps (StoryShowcase_Kit, Depot_1_X6, Demo01_A_FrozenLake, VehicleTest, MainMenu) - this also
   fixes 104 (the stale L_Gen_Bgen_Test_X6 entry; move its stale props / tactical files to the archive, do not delete).
2. Sector picker for maps that have "sectors" in their LevelJson (X6: 1-6, top row 1 2 3, bottom row 4 5 6 - Gemini 105 data,
   Codex 106 runtime): the top-down map with the 6 boxes, numbers, player start and PMC post drawn; click a box to select it.
3. "Set active" writes active_sector into the map's LevelJson (0 = whole map); per sector show its contents (buildings,
   covers, fences, ladders) and its PMC squad (count, posture), editable count / posture.
4. Show which sector the last recorded sessions / arena runs used (Saved/Demos summary via Codex 075 demo_summary.py if cheap).
Cold is OFF on X6 / Showcase / Lake (cold_rate_mult 0, designer: fights only) - the tab must not change cold_rate_mult.
Branch codex/107-tg-ai-training-tab, TG tests + build of the TG; screenshot of the tab in outbox for the designer.

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
