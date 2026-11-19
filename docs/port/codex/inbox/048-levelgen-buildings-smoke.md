# 048 - LevelGenBuildingsSmoke: every enterable building reachable from its doors (P0b) + ramp-smoke probe fix - MEDIUM, C++ (Dev)
Limit rule: at 10 % of your limit left: stop, commit (WIP is fine), handover at the top of the report, FINISH line.
Base: origin/main (newest). Branch codex/048-buildings-smoke. Worktree CodexTactics-agents.

## Design gate
- Spec: docs/design/levelgen/BUILDING_GENERATOR.md section 5 (traversal gate, layer 2 "Bot smoke on the built map") and section 1.5
  (results so far). Building data: the level's accepted layout JSON (`Content/Data/LevelGen/accepted/*.json`, the level JSON
  `levelgen_layout` key points to it): buildings (footprint, floors / storeys, doors, flights / stairs, ladders, hatches, roof_walkable,
  enterable / the role fields if present).
- Acceptance: new dev command `CodexTactics.LevelGenBuildingsSmoke` (Source/CodexTacticsDev/Private/Debug/, pattern of
  LevelGenRampSmokeCommand.cpp): per ENTERABLE building, from just outside each exterior door, a navmesh path (FindPathSync, the
  operative nav agent) to every room centre / storey landing / the walkable roof must be COMPLETE (not partial); writes
  `Saved/LevelGen/buildings_smoke_<map>.json` (per building: id, pass, first_unreachable {target, kind, pos}, counts); the smoke
  result line: PASS when every enterable building passes, else FAIL listing the first 10 failing ids. Optional args `only=<id>`,
  `maxfail=<n>` (report mode: always PASS, for the generator page).
- Ramp-smoke probe fix (same branch, Source/CodexTacticsDev/Private/Debug/LevelGenRampSmokeCommand.cpp): (a) the "every 1 m" climb
  sample projects with a +-60 cm z window round a straight lerp - near the top of a flight the real ramp surface is lower, so it reads
  '---' and the leg is skipped: project along the real flight profile (or a wider window clamped to the flight's own z range);
  (b) upper flights (1 -> 2) report "no navmesh path" because TopIn = top + 300 cm inward lands off the navmesh: project TopIn to the
  navmesh on the top level first. Do not change what the legs check.
- Proved by: on /Game/Maps/Generated/L_Gen_BgenTest_X6 (copy from the MAIN checkout ../CodexTactics, untracked there - do not commit:
  Content/Maps/Generated/L_Gen_BgenTest_X6.umap, Content/Data/LevelJson/L_Gen_BgenTest_X6.json,
  Content/Data/LevelGen/accepted/gen_bgen_test_x6.json) the new smoke runs and reports (the generator branch says 0 failing
  buildings in its TS validator - list any building the navmesh disagrees on); on the OLD /Game/Maps/Generated/L_Gen_Depot_1_X6
  (main) it reports many failures (the audit: 24 of 70) - both numbers in the report. Ramp smoke: upper flights get walked / legs no
  longer skipped on X6.
- Designer decision needed: none.

## Run notes
- smoke_parallel.ps1 cannot run a map other than test_map.json's: run UnrealEditor-Cmd directly from PowerShell:
  `UnrealEditor-Cmd <project> /Game/Maps/Generated/L_Gen_BgenTest_X6 -game -nullrhi -nosplash -nosound -unattended -windowed -NoTelemetry
  -ExecCmds="CodexTactics.LevelGenBuildingsSmoke" -abslog=<path> -UseFixedTimeStep -FPS=60 -FixedSeed -SquadStart=formation`
  (from Git Bash set MSYS_NO_PATHCONV=1 or /Game becomes a Windows path). Add the new smoke to test_map.json (map L_Gen_Depot_1_X6,
  report mode maxfail so the sweep stays green) and to docs/port/SMOKE_REGISTRY if one exists.
- Builds: `Scripts/build.ps1 -Remote worker` (exit 2 = worker busy / frozen -> local).

## Never
- Change gameplay C++ (this is a Dev smoke task), maps, accepted / level JSON, Config, Content/VFX; weaken an existing smoke check;
  push or merge main.

## Gate
build | the new smoke on L_Gen_BgenTest_X6 and L_Gen_Depot_1_X6 (numbers) | LevelGenRampSmoke default (test_map) + on X6 legs=solo
(report legs before -> after) | LevelGenLaddersSmoke | `Scripts/test.ps1 -Smart` | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex`.
Allowed paths: Source/CodexTacticsDev/Private/Debug/LevelGenBuildingsSmokeCommand.cpp (new), LevelGenRampSmokeCommand.cpp,
Source/CodexTacticsDev/** helpers you add, Scripts/test_map.json, docs/port/codex/outbox/048-*,
docs/design/levelgen/BUILDING_GENERATOR.md section 1.5 (one results paragraph).

## Report
docs/port/codex/outbox/048-buildings-smoke.report.md: handover, design of the checks, per-map numbers, ramp legs before -> after, gate.
FINISH in chat topic codex-048: `048 FINISH | <status> | <commit> | done: ... | NOT done: ... | gate: ... | report: <path>`.
