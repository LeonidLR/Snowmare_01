# G013 - Read the 20 kit buildings of the new Soviet pack into data, so the generator can copy real floor plans (TOOL, Python)

Designer 2026-11-20: our generated buildings are too simple and their plans look wrong. He bought the Fab pack "Soviet Apartment
Buildings - Modular Kit + 20 Enterable USSR Buildings" (3 m grid, real series 1-464 / 1-335 / 1-447, Stalin, brick, barracks, dormitory,
L-blocks with shops). It sits UNTRACKED (never commit it) in the agents worktree:
`C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-agents\Content\SovietHouses\` (Maps/Buildings/L_SH_Building_01..20.umap,
Meshes/{Balconies,Buildings,Facade,Interior,Openings,Props,Roof}, mesh names like SM_SH_IntWall_aptdoor_26, SM_SH_Win_PVC_pair_28,
SM_SH_B01_Slab_F0..F5, SM_SH_Stair_ground_28_6, SM_SH_Door_Apt, SM_SH_Ent_Door_Steel).

Branch `grok/G013-kit-buildings-dump` from the newest main, worktree CodexTactics-grok. Commit locally (push may be refused by the
pre-push guard - fine, Claude merges from the local commit). Model: build-fast. Budget: this is the last ~10 % of the Grok month - keep
turns low; at 2 % left: commit WIP, FINISH line, stop.

## Task
1. `Scripts/Editor/dump_kit_buildings.py` - an Unreal Editor Python script (read-only: never save, never modify an asset or map).
   For every `/Game/SovietHouses/Maps/Buildings/L_SH_Building_NN` map: load it (unreal.EditorLoadingAndSavingUtils.load_map or
   LevelEditorSubsystem), list every actor with a mesh (StaticMeshActor, ISM / HISM instances, Blueprint actors' StaticMeshComponents):
   mesh path, world location / rotation / scale, world bounds min/max. Also every StaticMesh asset under /Game/SovietHouses/Meshes: path +
   local bounding box size (cm). Write `Saved/KitDump/kit_buildings.json` (one entry per building + one `meshes` table) and a log line per
   map. Argument `--only NN` for one building. Look at the existing Scripts/Editor/*.py for how this project runs editor Python headless
   (UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=...).
   Do NOT run the editor yourself: Claude runs this script on the agents worktree (the designer's editor may be open). Make it robust
   (try/except per actor, never stop the whole dump on one bad actor).
2. `Scripts/Tools/kit_plan_report.py` - pure Python (no unreal module) that reads kit_buildings.json and writes
   `Saved/KitDump/kit_plans.md` + `kit_plans.json`, per building:
   footprint (m), storeys + storey height (from the slab / floor meshes' Z), entrances (Ent_Door*), stairwells (Stair*, StairGlass*,
   IntWall_stair*) and their position in the footprint (street vs court side), flat doors per landing (Door_Apt / IntWall_aptdoor*),
   rooms per flat (estimate from interior partition walls: IntWall_part*, IntWall_apt*), window module types + widths along each facade,
   balconies per facade, shop parts (shop windows / doors), roof type; and a grid check: every module width / position vs a 300 cm and a
   50 cm grid (how many fit, list misfits). Plus a short summary table of all 20 (series guess from the name/size, footprint, storeys,
   sections, flats per landing).
3. `Scripts/Tools/tests/test_kit_plan_report.py` (unittest, no editor): a fake kit_buildings.json for 2 small buildings -> the report
   finds the right storeys, entrances, flats per landing, grid misfits.

## Never
Commit or copy any pack asset; touch Content/**, maps, C++, Config, the building generator (Tools/TrainingGround/src/levelgen/**,
build_levelgen_map.py); run UnrealEditor; push or merge main; edit .git/hooks.

## Gate
`python -m unittest Scripts/Tools/tests/test_kit_plan_report.py` | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok`
(or `--agent claude` if grok is not a known agent) | `git diff --check`.
Allowed paths: Scripts/Editor/dump_kit_buildings.py, Scripts/Tools/kit_plan_report.py, Scripts/Tools/tests/test_kit_plan_report.py,
docs/port/grok/outbox/G013-*.

## Report
`docs/port/grok/outbox/G013-kit-buildings-dump.report.md` (Russian): the exact command for Claude to run the dump, what the report
computes and how (heuristics), test output pasted, what is not done.
FINISH in chat: `python Scripts/Tools/agent_chat.py post --as grok --to claude --topic grok-G013 --text "G013 FINISH | <status> | <commit> | done: ... | NOT done: ... | gate: ..."`
