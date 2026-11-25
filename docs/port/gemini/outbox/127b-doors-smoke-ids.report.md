# Gemini 127b — Update `DoorsSmoke` to Renamed Showcase Door IDs

Model: `Hydrogen`
Branch: `gemini/127b-doors-smoke-ids` (from `origin/main` `3c3159e6`)

## Changes (`Source/CodexTacticsDev/Private/Debug/DoorsSmokeCommand.cpp`)

1. **Renamed Showcase Door IDs (`gen_story_showcase.json` after `20a6a6308`)**:
   - `S01_open_f0_d19` -> `S01_open_f0_d_court`
   - `S01_open_f0_d20` -> `S01_open_f0_d_gable_u0`
   - `S03_locked_door_f0_d19` -> `S03_locked_door_f0_d_court`
   - `S03_locked_door_f0_d20` -> `S03_locked_door_f0_d_gable_uL`
   - `S09_broken_back_windows_f0_d19` -> `S09_broken_back_windows_f0_d_court`
   - `S09_broken_back_windows_f0_d20` -> `S09_broken_back_windows_f0_d_gable_uL`
   - `S04_rubble_escape_f0_d19` -> `S04_rubble_escape_f0_d_court`
   - Updated the Stage A & D log strings to match the new IDs.
2. **Stage D Target Depth**:
   - After `20a6a6308`, `S01_open_f0_d_gable_u0` `[2670, -6700]` opens into `S01_open_r_f0_F1-living` (4.6x4.5 m, median free ray > `confined_median_m` 1.7 m), where `Column` formation trails followers `-330` and `-480 cm` behind the leader (`-80` and `-230 cm` outside when stopping `250 cm` inside, failing `D: 1 / 3 inside`).
   - Updated Stage D's primary inside click to step into the adjacent 2.4 m hall `S01_open_r_f0_F1-hall` `(2320, -6380)` (keeping the `250 cm` fallback) so all 3 squad members clear `S01_open_f0_d_gable_u0` cleanly.

## Verification

| Check | Scope / Command | Result |
|---|---|---|
| Dispatched build | `machine_dispatch.py build --lane CodexTactics-127b` (`worker`) | `Result: Succeeded` (`387.49 s`) |
| Impact map | `Scripts/test.ps1 -CheckMap` | `Impact map OK` |
| Unit tests | `Scripts/test.ps1 -Filter "CodexTactics.Doors" -SkipSmokes` | `31 tests, 0 failed` |
| Smoke | `machine_dispatch.py smoke --lane CodexTactics-127b --smokes DoorsSmoke --jobs 1` (`main`) | `PASS` (`68.2 s`) |
