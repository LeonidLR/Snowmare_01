# 028 - Build the combat buildings IN THE GAME (designer 2026-11-12: "Gemini builds the buildings in the game")

Your own spec docs/port/codex/inbox/007-levelgen-buildings-builder.md (branch gemini/027-7-cpp-spec, c2a686c6) is APPROVED as the
plan - now you implement it. Second approved C++ trial; the exception covers ONLY the paths below. Work in YOUR worktree
CodexTactics-gemc5 on a new branch `gemini/028-buildings-in-game` from `interim/2026-11-12` (so you have Codex's latest code).
Small commits after every working part. Codex (integrator) reviews and merges it into interim; Claude merges interim into main.

## Allowed paths
- Scripts/Editor/build_levelgen_map.py, Scripts/Editor/levelgen_*.py (the map builder) + their tests.
- Source/CodexTactics/{Public,Private}/LevelGen/** and NEW files you add for openings / hatches / parapets / mantle edges.
- Small, test-covered hooks in Vault* / SightRules / LadderActor / RoofCutawaySubsystem ONLY where your spec says so - list every
  touched line in the report; no behaviour change for maps without the new keys (legacy files must play exactly as before).
- Source/CodexTacticsTests/** and Source/CodexTacticsDev/Private/Debug/** for your tests and smokes.
- Generated maps: ONLY NEW Content/Maps/Generated/L_Gen_Train_1..3.umap (+ their __ExternalActors__ if the builder needs them) in
  YOUR worktree. NEVER L_Gen_Depot_1 or any other existing map; never Config, Content/VFX, CLAUDE.md.

## Asset safety (important)
The packs under Content/ in your worktree are JUNCTIONS to the designer's main checkout. The map builder must save ONLY the new
map package(s) - never `save_directory` / save-all, never resave a pack asset (check the list of saved packages in the log and
paste it into the report). Run the editor commandlet headless in your worktree; the designer's editor on main may stay open.

## Done =
1. L_Gen_Train_1..3 build from their JSON: windows / loopholes as real openings (sill + lintel), partitions with doors, storeys with
   floor gaps, hatches to roofs (climbable + nav link), parapets, mantle-edge tags; legacy levels unchanged (rebuild one legacy
   generated level and diff the actor list - identical).
2. Checks pasted into docs/port/gemini/outbox/028-buildings-in-game.report.md: build.ps1, `test.ps1 -Filter CodexTactics.LevelGen`
   (+ new tests), smokes LevelGenRouteSmoke (extended: a route through stairs and a hatch on L_Gen_Train_1) + a window line-of-sight
   check, DepotVaultSmoke, LevelGenLaddersSmoke, `typesafe_triage.py --audit-diff --agent gemini` (quote any flag on the approved paths).
3. Frames from the game camera: each arena overview + one window / loophole close-up -> MAIN Saved/Orchestrator/buildings/.
If a step needs a design choice the docs do not answer, ask the designer in the chat (Russian, recommended default) and continue.
Also answer Codex #232 (021-map-41: MON_* names and the 5 cm stuck threshold from AUTO_QA.md) with one small commit first.
