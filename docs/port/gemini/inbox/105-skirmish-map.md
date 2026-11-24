# Gemini 105-skirmish-map (DESIGNER PRIORITY - pause 102, return to it after)

Designer 2026-11-24 ~15:45: X6 is too big for 3 vs 3 - "just wandering the map looking for enemies". He wants a SMALL map for
3 v 3 fights (the trainer recordings): the same kind of content as X6, only smaller: at least one enterable building, several
cover types (sandbags, jersey barriers, barrels, low walls, high cover), fences, props (cars, crates...), all of it.
This becomes the 6th kept map (designer OK today).

Make:
1. Layout: Training Ground generator, depot/X6 profile (gen_ref_01_02_depot_1 = X6 reference), bounds about 100 x 100 m
   (max 120 m edge), 1-2 enterable buildings, the cover mix above, fences, props via the props pipeline
   (Content/Data/LevelGen/props/<map>.props.json). Accept it as Content/Data/LevelGen/accepted/gen_skirmish_01.json.
   Name the map L_Gen_Skirmish_01 (/Game/Maps/Generated/).
2. Unreal map: Scripts/Editor/build_levelgen_map.py in YOUR worktree (never the main checkout - the designer plays there),
   navmesh built, drop links / vault covers as on X6.
3. Content/Data/LevelJson/L_Gen_Skirmish_01.json: copy the X6 rules (PMC only, no monsters / horde / pressure, real ammo,
   damage on, weather as X6, session recording works) with ONE PMC squad of 3 (pmc_commander.json, posture defend or hunt -
   choose the one that finds the player within ~1-2 min) on a post 50-70 m from the player start, player squad 3.
4. Check on the worker only: LevelGenBuildingsSmoke / InteriorExitSmoke / GeneratedPropsX6Smoke-style checks with -Map on the
   new map, plus a 5-seed PMC arena or bot route run: first contact time (target < 90 s), no stuck units.
5. Deliver: branch gemini/105-skirmish-map, a top-down screenshot of the layout (PNG in outbox) for the designer, report with
   first-contact times. Untracked .umap: say exactly which files the Lead must copy into main.
Speed matters (designer: "as fast as possible"), but no half-built map: route + navmesh must pass.
Rules: docs/port/TANDEM.md 'WORK PROCESS'. Report in outbox + chat FINISH. Never push.
