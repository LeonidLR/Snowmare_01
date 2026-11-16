# Task 023 — validated ×2 fixture; UE build handed to Claude

Status: CODEX PREPARATION COMPLETE / UE VALIDATION NOT RUN. The designer explicitly selected "Do not create the map; hand the build to Claude" when asked about the protected-map rule. No Unreal editor, builder, bake, or route command has run; no new map/assets were created. Claude owns the remaining build and route validation. This report does not claim the full UE task passed.

## Prepared data

- New input: Content/Data/LevelGen/TestFixtures/L_Gen_Test_X2.layout.json (codex.levelgen_layout/1).
- Source: Content/Data/LevelGen/props/fixtures/golden_depot.layout.json, a frozen test fixture documented in golden_cases.json; never the live designer Depot map or level JSON.
- API: expandLayout({ layout: frozenFixture, factor: 2, seed: 7, profile }), the same expand.ts function imported by levelgenV2Expand.test.ts:13. Source is unchanged.
- Source SHA256: 0ed64c513f769150a7b2252f14a24f2dfa97e5176512e85af15528ffd2fe8dcc.
- Output SHA256: 3371cb9a0a1590773c03a0c518859b659ba6eaf73b619aeb47c6da5b1c17fc43.
- Size: 177×124 → 250×175 m, area ratio 1.993347913249499; bounds min [-8750,-12500], max [8750,12500] cm. Expansion seed 7, retries 0.
- validateGenLayout and validateExpansionState: zero errors. Canonical JSON roundtrip verified; fresh expansion matches output byte for byte.
- Four routes completable: R_main (mandatory), V1_gallery, V2_north_blocks, V3_cut. This is a generator check, not UE bot evidence.
- Two road seams and two canal seams connected, all ratio 1; no blocked seams. Original two crossings preserved, four crossings total. Nineteen new lots; 24 buildings, 268 walls, 9 ladders, 430 covers.
- New tiny script Tools/TrainingGround/scripts/expand-test-x2.ts refuses existing-output overwrite; --verify performs no writes.

## Validation run

```powershell
$env:CODEX_AGENT='codex'
$env:PYTHONIOENCODING='utf-8'
node Tools/TrainingGround/scripts/expand-test-x2.ts --verify
node --test Tools/TrainingGround/src/levelgen/levelgenV2Expand.test.ts
```

Both exit 0. Focused stage-2 suite: tests 8, pass 8, fail 0. During preparation the roundtrip assertion initially distinguished JavaScript -0 from JSON 0; comparing canonical JSON resolved this verification-only issue without changing generator output.

## Concrete handoff pipeline — Claude only; Codex must not run

Run from this agents worktree with the project editor closed. Do not use -Fresh: the new target must not already exist.

```powershell
$env:CODEX_AGENT='codex'
$env:PYTHONIOENCODING='utf-8'
if (Test-Path -LiteralPath 'Content/Maps/Generated/L_Gen_Test_X2.umap') { throw 'Target already exists; refuse overwrite' }
if (Test-Path -LiteralPath 'Content/Data/LevelJson/L_Gen_Test_X2.json') { throw 'Runtime JSON already exists; refuse overwrite' }
powershell -ExecutionPolicy Bypass -File Scripts/levelgen_build_map.ps1 -Layout 'Content/Data/LevelGen/TestFixtures/L_Gen_Test_X2.layout.json' -Target '/Game/Maps/Generated/L_Gen_Test_X2' -StatusFile 'Saved/LevelGen/build_map_Test_X2_status.json'
if ($LASTEXITCODE -ne 0) { throw 'Build/bake failed; inspect status and logs before route' }
powershell -ExecutionPolicy Bypass -File Scripts/levelgen_route_smoke.ps1 -Map '/Game/Maps/Generated/L_Gen_Test_X2' -StatusFile 'Saved/LevelGen/route_smoke_L_Gen_Test_X2.json'
```

Input is deliberately separate from runtime output: the builder writes Content/Data/LevelJson/<target leaf>.json. Using that path as layout input would overwrite the expanded layout with a different runtime format. No builder refactor needed.

Expected outputs if Claude executes the handed-off pipeline: Content/Maps/Generated/L_Gen_Test_X2.umap; Content/Data/LevelJson/L_Gen_Test_X2.json; Saved/Logs/LevelGenBuildMap.log; Saved/Logs/Gen_Test_X2Layout.txt (builder strips L_ in flush_report); Saved/Logs/NavBake-L_Gen_Test_X2.log; Saved/Logs/LevelGenRouteSmoke-L_Gen_Test_X2.log; the two explicit Saved/LevelGen status files. Header/argument inspection does not establish a complete generated .uasset/sidecar manifest; inspect any generated packages before staging. No asset output currently exists.

## Pending evidence

Ground-plane / NavMeshBounds sizing, UE canal and bridge geometry, nav bake, bot route, final builder lines, and route result remain UNTESTED. The builder header says ground and nav use bounds, trench uses cut.poly/crossings, and nav extends from below cut to 3 m above highest walkable level. These are declared behavior, not observed success. Report section LEVELGEN_V2_STAGE2_REPORT.md:78–81 identifies this exact UE validation gap. If execution requires C++ or broad investigation: needs Claude, no C++ changes.

Routing: `2 | gpt-6.1-sol | pass (preparation)`; router confidence .60. UE execution is handed to Claude per the designer's explicit answer; no Codex map exception was granted. No local model used.

Final handoff: prepared artifacts are committed at `ff5f1e0b`. The designer declined Codex map creation and requested transfer of the build to Claude. needs Claude: build the new test map and validate ground/nav/canal/bridges/route using the exact commands above. Existing maps and Depot remain untouched.
