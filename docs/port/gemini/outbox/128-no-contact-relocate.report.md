# Gemini 128 — Sector Rounds No-Contact PMC Relocate (~40 m)

Model: `Hydrogen`
Branch: `gemini/128-no-contact-relocate` (from `origin/main` `7bcb850c`)

## Summary

- **Data (`Content/Data/LevelJson/L_Gen_Depot_1_X6.sectors.json`)**:
  - Added top-level defaults: `"no_contact_relocate_s": 90`, `"relocate_distance_m": 40`, `"relocate_max_per_round": 3` (parsed by `FSectorFightConfig` / `TrainingSectorRules::FightConfig`, overridable per sector).
- **Pure Rules (`TrainingSectorRules.h/cpp`)**:
  - Added `ShouldRelocateNoContact(...)` gating on `!bInGrace`, `DelayS > 0`, `MaxPerRound > 0`, `RelocatedCount < MaxPerRound`, and `Now - LastContactAt >= DelayS`.
  - Added `PickRelocatePoint(...)`: strictly enforces `MinPlayerDistM >= min_start_post_distance_m`, prefers `[RelocateDistanceM - 5, RelocateDistanceM + 5]` (`35..45 m`) without LOS and beside cover > without LOS > with LOS closest to `RelocateDistanceM` > out-of-band fallback.
- **Contact Tracking & Runtime Relocation (`PmcSquadSubsystem`, `TrainingSectorSubsystem`)**:
  - Added `UPmcSquadSubsystem::GetLatestContactActivityTime()` (max `LastSeenContactTime` and `LastShotTime` across all teams) and synced `LastContactAt` in `UTrainingSectorSubsystem::TickFighting` and `RecordShot` (also scanning operative `GetLastShotTime()`).
  - When `ShouldRelocateNoContact` triggers (skipped during tactical pause / turn-based), `UTrainingSectorSubsystem::RelocatePmcSquad` samples `9 x 16` radial points around player centroid within the active sector box, checks street ground (`Z <= 110 cm`, `ImpactNormal.Z > 0.7`) + navmesh (`<= 60 cm` off ground, `Allows` with `135 cm` inset), tests eye-to-eye `ECC_Visibility` LOS (`UTacticalSightSubsystem::EyePoint`) and nearby cover rays (`180 cm` waist-height `ECC_WorldStatic`), teleports living PMC fighters preserving HP/ammo/rockets-fired, and orders their teams via `UPmcSquadSubsystem::EngageRelocatedTeam`.
- **Session Recording (`SessionRecordingRules.h/cpp`)**:
  - Added `bPmcRelocated`, `RelocateDistanceM`, `bRelocateLos` to `FDemoRoundEvent`, serializing `"type":"pmc_relocated"` (`t`, `rt`, `sector_id`, `seed`, `round`, `distance_m`, `los`) without incrementing footer `"rounds"`.

## Verification

| Check | Scope / Command | Result |
|---|---|---|
| Dispatched build | `machine_dispatch.py build --lane CodexTactics-128` (`worker`) | `Result: Succeeded` (`323.99 s`) |
| Impact map | `Scripts/test.ps1 -CheckMap` | `Impact map OK` |
| Unit tests | `Scripts/test.ps1 -Filter "CodexTactics.Training+CodexTactics.AI.Pmc" -SkipSmokes` | `80 tests, 0 failed` (`Training.Sector.Fight` covers relocate rules, config, and `pmc_relocated` JSON) |
| Sector smokes | `machine_dispatch.py smoke --lane CodexTactics-128 --smokes SectorRoundsSmoke,SectorBoundarySmoke --jobs 1` (`worker`) | `SectorRoundsSmoke PASS (51.4 s)`, `SectorBoundarySmoke PASS (41.6 s)` |
| Relocate smoke | `worker_sync.ps1 -ShipHead -BinariesFrom ... -Commands SectorRoundsSmoke -Jobs 1 -SmokeExtra "-SmokeArgs 'RelocateNoContact=1'"` (`worker`) | `PASS (57.6 s)`: `pmc_relocated` after `90.0 s` at `42.0 m` (`LOS no`, `cover yes`, `fallback no`, `Z=10.00`, units & HP preserved) |
