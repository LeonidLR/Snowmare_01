# 023 - Build a ×2 expanded generator map in Unreal and run the route smoke (Claude, 2026-11-16) - MEDIUM, mostly long runs
Why: level generator v2 stage 2 (9596f96e) can expand a map ×2/×4/×6 (Training Ground), but nobody has built an expanded map in UE yet
(docs/port/LEVELGEN_V2_STAGE2_REPORT.md lines 78-81). Check that the existing pipeline builds it and the bot can walk it.
Branch codex/023-levelgen-expanded-build (from main), worktree CodexTactics-agents.
Do:
1. Read the report section above + the header comments of Scripts/levelgen_build_map.ps1, Scripts/levelgen_route_smoke.ps1,
   Scripts/Editor/build_levelgen_map.py (header + argument parsing only).
2. Produce a ×2 expanded map JSON from a TEST copy (never the designer's L_Gen_Depot_1.json): use the generator's CLI / API that the
   stage-2 tests use (Tools/TrainingGround/src/**/levelgenV2Expand*.ts) to write e.g. Content/Data/LevelJson/L_Gen_Test_X2.json
   (new file only). If there is no CLI, add a tiny script under Tools/TrainingGround/scripts/ that calls the same function.
3. Build it with levelgen_build_map.ps1 into a NEW test map (e.g. /Game/Maps/Generated/L_Gen_Test_X2) - never overwrite an existing map.
   Paste the last lines. Then levelgen_route_smoke.ps1 on it. Paste the result.
4. Report what works and what breaks (file:line + error lines): ground plane / NavMeshBounds size, canal, bridges, route.
   No C++ edits - if the build or nav needs C++, write "needs Claude: <what>".
Allowed paths: the new test JSON, the new test map (generated), Tools/TrainingGround/scripts/<new small script>, report
docs/port/codex/outbox/023-levelgen-expanded-ue-build.report.md. Close every editor/commandlet you start.
