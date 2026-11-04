# Report: T10. Combat Simulator Parity with the New Rules (TypeScript)

**Task:** T10. Combat simulator parity with the new rules (TypeScript)  
**Branch:** `gemini/t10-combat-sim-parity`  
**Date:** 2026-11-04  

## Summary of Deliverable

Ported the new Unreal Engine gameplay rules introduced on 2026-11-04 into the Training Ground combat simulator (`Tools/TrainingGround/src/combatSim/`):
1. **Sight Conditions Parity**:
   - Integrated full weather and daylight multipliers matching `Content/Data/AI/enemy_perception.json` `sight_conditions`.
   - Clear-day squad sight range extends to 120 m on the ground (was flat 60 m).
   - Human enemies (Marksman / PMC fighters) scale their archetype sight range by x2.0.
   - Weather multiplier: Clear 1.0, Overcast 0.85, Snowfall 0.6, Blizzard 0.5, Light Snow 0.75, Fog 0.4, with linear blending during transitions.
   - Time-of-day daylight multiplier interpolates with sun elevation: Day 1.0 (>=10°), Twilight 0.7 (0°), Night 0.35 (<=-10°).
   - Height bonus from `HeightSightRules` (+25% per level, up to x2.0) composes on top.
   - Dynamic data reading from JSON (no hard-coding) via `parseSightConditions` and `loadProjectCombatRules`.
2. **PMC Engagement Bands Parity**:
   - Armed humans fight within their weapon's engagement band from `Content/Data/AI/pmc_commander.json` `engagement_bands` and reach values from `Content/Data/Weapons/weapons_tuning.json` (m16 60 m, sniper 120 m, pistol 25 m, shotgun 15 m).
   - Implemented `PmcEngagementRules` (`resolveBand`, `isValidBand`, `bandFactor`, `isTooClose`, `advanceStopCm`, `advancePoint`, `flankDistanceFor`, `standoffPoint`).
   - Armed humans back off when threat is closer than `standoff_trigger_frac * band.minCm` (retreating to `standoffPoint` at band center).
   - Close-quarters weapons (shotgun) close in towards target rather than retreating.
3. **Backward Compatibility & Reproducibility**:
   - Rules kept behind sim options: `useSightConditions: false` and `useEngagementBands: false` in `DEFAULT_SIM_CONSTANTS`. Old runs and fixtures stay 100% reproducible.
   - CLI flags added to `npm run combatsim`: `--sight-conditions`, `--engagement-bands`, `--weather`, `--sun-elevation`.
4. **Calibration & Verification**:
   - All 21 combatSim unit tests pass (`npm test`).
   - Typecheck clean (`npm run typecheck`).
   - Movement goldens untouched and verified clean (`npm run golden -- --check`).
   - Added calibration note in `docs/design/levelgen/COMBAT_SIM.md` Section 4.1: simulated median engagement distance is ~21 m on Depot with bands (matching UE arena ~21 m), with 8 % of shots under 10 m (matching UE arena 8 %).

---

## 1. Architectural Changes

- `Tools/TrainingGround/src/combatSim/rules.ts`:
  - Added `SightConditionsRules` with weather multiplier, daylight elevation blend, and sight range calculators.
  - Added `PmcEngagementRules` mirroring C++ `PmcCommanderRules.h` / `.cpp`.
- `Tools/TrainingGround/src/combatSim/config.ts`:
  - Added `parseSightConditions`, `parsePmcBandConfig`, and `parseWeaponReaches`.
  - Extended `SimConstants` with `useSightConditions`, `weather`, `sunElevationDeg`, `sightConditions`, `useEngagementBands`, `engagementBands`, and `weaponReachesCm`.
  - Added optional `weaponId` to `EnemyStats` (`MARKSMAN` uses `sniper_rifle`).
- `Tools/TrainingGround/src/combatSim/node/files.ts`:
  - Added loaders: `readEnemyPerception`, `readPmcCommander`, `readWeaponsTuning`, and `loadProjectCombatRules`.
- `Tools/TrainingGround/src/combatSim/sim.ts`:
  - Updated `squadSightRangeCm` and `perceive` to honor sight conditions when active.
  - Updated `tickEngaged` for armed humans to back off when too close and advance to band when far.
  - Added shot distance tracking for median engagement distance and fraction of shots under 10 m in `RunMetrics`.
- `Tools/TrainingGround/src/combatSim/cli/combatsim.ts`:
  - Added CLI flags `--sight-conditions`, `--engagement-bands`, `--weather`, and `--sun-elevation`.
- `docs/design/levelgen/COMBAT_SIM.md`:
  - Added Section 4.1 calibration note detailing sim vs UE arena metrics.
- `Tools/TrainingGround/src/combatSim/combatSim.test.ts`:
  - Added 4 test suites verifying sight conditions, engagement bands, parser fidelity, and sim integration.
