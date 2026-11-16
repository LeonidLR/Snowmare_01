# 036 - Update the x2 test layout to the obstacle height standard (Claude, 2026-11-16) - LIGHT, data only
Why: L_Gen_Test_X2 was built from Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x2.json (copy of TestFixtures/L_Gen_Test_X2.layout.json)
which has OLD obstacle heights, so mantle/vault links are not generated there (mantling batch 2 report). Claude rebuilds the map; you
fix the data.
Branch codex/036-x2-heights (from main), worktree CodexTactics-agents.
Do: find the height-class tool (Scripts/Editor/apply_depot_height_classes.py or similar - read its header) and run it on BOTH
Content/Data/LevelGen/TestFixtures/L_Gen_Test_X2.layout.json and Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x2.json (if it only
works on another format, write a tiny adapter script). Verify with its own check / the levelgen validator (TG tests for the fixture).
No map build (Claude does it). Allowed paths: those two JSON files, a new adapter script if needed, report. 10 % limit rule.
