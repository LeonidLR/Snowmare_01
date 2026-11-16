# 031 - Train_1 sill test

Status: PASS; ready for Claude review. Branch: `codex/031-train1-sill`.

Routing: `2 | gpt-6.1-sol | pass` (router confidence 0.51, supplied by parent).

## Change and evidence

- Source commit: `92285e4a4a739cd95b96d62e094e28029ce42b72` (`test(levelgen): align Train_1 sill with measured height standard`).
- Changed only `Scripts/Editor/tests/test_build_levelgen_buildings.py`: expected ground-floor window sill height 90 -> 95 cm and documented the measured-height provenance in its comment.
- Historical evidence: `58a7e34eabce9006f4482e130bb8c7eb84882a8b` changed `Tools/TrainingGround/src/levelgen/schema.ts` window default from 90 to `obstacleHeight('low_vault')` and regenerated `Content/Data/LevelGen/accepted/L_Gen_Train_1.json`, explicitly changing its window `sill_height` values from 90 to 95.
- Standard: `docs/design/MANTLING.md:320` specifies approved low_vault target 95 +/- 5 cm; line 322 states explicit approved targets supersede provisional targets. `Content/Data/LevelGen/obstacle_heights.json:12-14` defines low_vault nominal_cm 95 and tolerance_cm 5.
- Builder is correct for this fixture: `Scripts/Editor/build_levelgen_map.py:319` reads explicit opening `sill_height`; line 327 uses it for the sill geometry. No builder change was needed.

## Validation

Exact requested command: `python -m unittest discover -s Scripts/Editor/tests -p 'test_build_levelgen*'`.

Baseline: 7 tests, 1 failure: Train_1 assertion 95.0 != 90.0. After correction: **7 tests PASS** (0.069 s). Last 15 lines:

```text
ladder exit opening in CUTWALL_S_0: LAD_cut_1 398..598 cm, LAD_cut_2 4204..4404 cm
ladder exit opening in CUTWALL_S_1: LAD_cut_3 5662..5862 cm
rotated building B_hall_n: -45.0 deg about (4450, -1350)
ladder LAD_cut_1: yaw 0 (layout 180 - MIRRORED in the layout, fixed), moved 0.0 cm out to stand 10 cm off Cut_Face_0
ladder LAD_cut_2: yaw 0 (layout 180 - MIRRORED in the layout, fixed), moved 0.0 cm out to stand 10 cm off Cut_Face_0
ladder LAD_cut_3: yaw 0 (layout 180 - MIRRORED in the layout, fixed), moved 0.0 cm out to stand 10 cm off Cut_Face_0
ladder LAD_B_gallery: yaw 180 (layout 0 - MIRRORED in the layout, fixed), moved 15.0 cm out to stand 10 cm off B_gallery_S_0
.ladder LAD_B_house: yaw 90 (layout 90), moved 15.0 cm out to stand 10 cm off B_house_E_0
.ladder LAD_B_checkpoint: yaw 270 (layout 270), moved 15.0 cm out to stand 10 cm off B_checkpoint_W_0
.ladder LAD_B_warehouse: yaw 270 (layout 270), moved 15.0 cm out to stand 10 cm off B_warehouse_W_0
.
----------------------------------------------------------------------
Ran 7 tests in 0.069s

OK
```

`git diff --check`: PASS. Pre-source-commit `python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex`: PASS, risk 0.0/4, one test file. Parent reviewed the two-line test diff and report and ran the required audit before the final report commit.

No map rebuild, engine build, assets/config/data edits, push, merge, branch switch, or local_task.py. Existing unrelated untracked content and caches were left alone. No open questions.
