# 121 Plan — Session Recording Tactics & Offline Tagger

**Branch**: `gemini/121-recording-tactics`  
**Model**: Hydrogen

## 1. Additive Recording Format (`SessionRecordingRules`, `SessionRecordingSubsystem`)
- **Events** (`FDemoEvent` interleaved by `t`):
  - `shot_hit` / `damage`: `who`, `whom`, `dmg`, `dist`
  - `kill` / `down`: `killer`, `victim`, `weapon`, `dist`, `was_victim_aware`, `victim_covered_by_ally`
  - `stealth_on` / `stealth_off`: `op`
  - `cover_enter` / `cover_leave`: `op`, `height` (`High` / `Low`)
  - `climb`: `op`, `kind` (`roof` / `ladder` / `mantle`), `height_cm`
- **Snapshot / Order `ops` fields (additive)**:
  - `stealth` (bool), `in_cover` (bool), `height` (cm above ground reference), `seen_by_enemy` (int), `under_fire` (seconds since last incoming shot, `-1` if none), `scan_pitch` (deg).

## 2. Offline Tagger (`Scripts/Tools/demo_tactics.py`)
- Pure rule functions with named threshold constants.
- Detects all patterns from `DESIGNER_TACTICS_DOCTRINE_2026-11-25.md`:
  - `fallback_to_cover_after_contact`, `high_ground`, `distract_and_flank`, `prone_then_crawl_to_cover`, `first_in_cover_overwatch`, `flank_into_rear`, `unwatched_flank_seconds`, `scan_pitch_range`, plus `kill` list.
- Graceful degradation on old `v: 1` `.jsonl` files (such as `2026-11-25_L_Gen_Depot_1_X6_094811.jsonl`).

## 3. Verification
- C++ unit tests in `SessionRecordingTest.cpp` (`CodexTactics.Telemetry.SessionRecording.*`).
- Python unit tests in `Scripts/Tools/test_demo_tactics.py`.
- Worker build + `CodexTactics.Telemetry.SessionRecording` + `SectorRoundsSmoke` / `SectorBoundarySmoke` via `machine_dispatch.py`.
- Run `demo_tactics.py` on `2026-11-25_L_Gen_Depot_1_X6_094811.jsonl` and on a fresh `v: 1` recording with the new events.
