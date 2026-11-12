# 027 - Level generator: combat-ready buildings (designer 2026-11-12, TODAY) - TypeScript in Tools/TrainingGround/src/levelgen

Why: PMC will be trained (Learning Agents) in near-combat conditions; levels need real buildings. Branch per item from main
(`gemini/027-<n>-<slug>`), small commits, npm test + typecheck green after each item, chat "027 item N done <hash>" to claude.
Never edit the designer's L_Gen_Depot_1.json; schema changes backward compatible (old level files must still load); every new
field documented in the schema + docs/design/levelgen/ (a short BUILDINGS.md). The game-side builder (C++) is a separate task -
write its exact spec as item 7 so it can be done right after.

1. **Schema:** wall openings - `window` (width, sill height, top height), `loophole` (narrow firing slit: width 20-40 cm, sill
   110-140 cm), existing doorways unified as openings; `partition` walls inside buildings (with door openings); `floor` slabs per
   storey with `gap` areas (open landings / pролёты / balconies / catwalks); `hatch` to the roof; roof `parapet` (low wall, cover);
   `mantle_edge` markers (edges 50-150 cm high a fighter can climb over - for the future mantling system).
2. **Generator - buildings with rooms:** split footprints into rooms (BSP or corridor + rooms) with partitions and doors; 2-3 storeys
   for the chosen building types; stairwells (existing stairs) + at least one roof access (stairs / ladder / hatch) per tall building;
   windows on street facades, loopholes on fortified types (bunker, checkpoint, guard tower); parapets on flat roofs.
3. **Critic / evaluate - tactical checks:** every room reachable (doors / stairs / hatches / ladders form one graph per building and
   connect to the street); no window opening into a wall; firing lines from windows / loopholes to the main routes (count + length);
   cover along routes; multi-level routes exist; mantle edges found; a score per building + per level; tests on fixtures.
4. **Training Ground preview:** a floor switcher (ground / 1 / 2 / roof), openings drawn on walls (window / loophole / door
   symbols), partitions, gaps, hatches, firing lines toggle.
5. **Training arenas:** generate 3 small combat arenas for the PMC training (L_Gen_Train_1..3 json in the levelgen output folder):
   (a) a two-storey house block with a street, (b) a fortified checkpoint with loopholes and a tower, (c) a warehouse yard with a
   roof catwalk; each passes the critic; screenshots described in the report.
6. Report docs/port/gemini/outbox/027-levelgen-buildings.report.md (what exists now vs before, schema diff, metrics).
7. **Spec for the C++ builder (read-only):** exactly what the game's level builder (Source/CodexTactics/Private/LevelGen, Level)
   must add to spawn the new schema (openings as wall segments with gaps, partitions, slabs with gaps, hatches + their nav links,
   parapets, mantle edge tags), file:line insertion points, smokes (LevelGenRouteSmoke extension: routes through stairs / hatches;
   a window line-of-sight check). docs/port/codex/inbox/007-levelgen-buildings-builder.md (draft for Claude to approve).
