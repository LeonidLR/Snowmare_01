# 076 - Generated maps have NO mines, barrels or loot (Claude, 2026-11-16) - MEDIUM, TypeScript (+ tiny server change)
Base: main (branch from main at the START of this task - Claude's level generator agent merges fixes into main tonight).
Branch gemini/076-generated-props.
Why (designer): on the generated maps (L_Gen_Depot_X4, L_Gen_Depot_1_X6, L_Gen_Test_X2) there are no mines, barrels, loot - nothing.
Cause (Claude checked): props are NOT part of the layout or the UE builder; the game loads them at runtime from
Content/Data/LevelGen/props/<MapName>.props.json (Source/CodexTactics/Private/LevelGen/CodexLevelPropsSubsystem.cpp:100). Only
L_Gen_Depot_1.props.json exists. The Training Ground props tools live in Tools/TrainingGround/src/levelgen/props/ and
src/departments/PropsPanel.tsx («Прогнать маршрут с пропсами»).
Do:
1. Read the props code + L_Gen_Depot_1.props.json format + the props validator/fast gate rules (src/levelgen/props/fastGate.ts, task 047
   history) and how PropsPanel saves a props file.
2. Automatic props for any generated / expanded / re-rolled layout: a deterministic (seeded) props pass that places mines, barrels
   (explosive), loot crates and other prop kinds the Depot file uses, with densities per area / zone type from a JSON config
   (Content/Data/LevelGen/props_autogen.json - mirror the Depot file's mix per 10 000 m2 as the default), obeying the existing
   validator rules (not on routes' critical chokepoints unless allowed, not inside walls/buildings without entry, spacing, mines not on
   the spawn / goals). For an EXPANDED map keep the original area's props from the source map's props file where the layout is
   unchanged, generate only for new / re-rolled areas.
3. Write Content/Data/LevelGen/props/L_Gen_<Name>.props.json (the exact map name the UE build uses - read the build target rule in
   server/levelgenApi.ts) when the designer presses «Принять» / «Собрать в Unreal»; show the props on the 2D plan (toggle) and in the
   playback. Never overwrite L_Gen_Depot_1.props.json or a props file the designer edited by hand (keep a "source": "auto" field and
   refuse to overwrite files without it; ask in the UI).
4. Generate props files NOW for the existing generated maps L_Gen_Depot_X4 (layout accepted/gen_ref_01_02_depot_1_x4.json),
   L_Gen_Depot_1_X6 (accepted/gen_ref_01_02_depot_1_x6.json) and L_Gen_Test_X2 (accepted/gen_ref_01_02_depot_1_x2.json) with a small CLI
   script, and commit them.
Tests: determinism, validator passes on N seeds, expanded map keeps the original props, refuse-to-overwrite rule, map-name rule.
Whole TG suite + typecheck + build green. No C++ (if the runtime needs a change, write "needs Claude"), no .uasset, no Content/Maps/**.
Allowed paths: Tools/TrainingGround/src/levelgen/props/**, src/departments/PropsPanel.tsx, src/departments/LevelGenDepartment.tsx
(props toggle only), server/levelgenApi.ts (save hook only), a new CLI script under Tools/TrainingGround/scripts/,
Content/Data/LevelGen/props_autogen.json, the 3 new props files, report.

UPDATE 2026-11-16 evening (Claude): the layers system is merged (docs/port/LEVELGEN_LAYERS_REPORT.md) - layers 7/8/9 (mines / loot /
special items: turrets + ALL barrels) already SHOW and EDIT props (manual place / move / delete into the props file "overrides").
Your job is only the AUTOMATIC generation of the props + writing the props files (items 2-4); reuse the layers' props read/write code,
do not duplicate it. BUDGET IS SMALL: hard limit 45 minutes - do items 2 and 4 first (auto props + the 3 files), item 3 hooks only if time
is left; list everything not done in your FINISH message.
