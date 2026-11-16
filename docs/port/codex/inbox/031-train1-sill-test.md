# 031 - Builder test fails: Train_1 window sill 95 vs 90 cm (Claude, 2026-11-16) - LIGHT
Why: `python -m unittest discover -s Scripts/Editor/tests -p "test_build_levelgen*"` fails
test_train_1_openings_partitions_no_doubles_and_parapets: AssertionError 95.0 != 90.0 (it failed before today's generator merge too).
Branch codex/031-train1-sill (from main), worktree CodexTactics-agents.
Do: find whether the test expectation is stale (the sill height standard moved to 95 with the measured obstacle heights - task J
58a7e34e "obstacle height standard from measured CaV clips"?) or the builder / Content/Data/LevelGen data is wrong. Prove it with the
commit that changed the value + the standard doc (docs/port/MANTLING*.md / the heights config). Fix the RIGHT side (if the standard is
95, update the test expectation and say why; if 90 is right, fix the data/builder). No map rebuilds.
Allowed paths: Scripts/Editor/tests/test_build_levelgen_buildings.py, Scripts/Editor/build_levelgen_map.py (only if the builder is wrong),
report docs/port/codex/outbox/031-train1-sill-test.report.md. 10 % limit rule.
