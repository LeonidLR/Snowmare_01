# T-05 - firing positions that can actually fire (report)

Agent: Claude sub-agent, worktree `CodexTactics-agents`, branch `claude/firing-positions` from origin/main c72394c0. Not merged, main not touched.
Spec: roadmap T-05, GDD smart-enemies section 5 + F2, brief `docs/port/claude/inbox/T005-firing-positions.md`.

## Handover (kept current)

**Phase a: DONE and gated** - commit `0bf3fbd2` (rules + service + 8 unit tests + `FiringPositionSmoke` PASS on X4 + regression smokes green). Diagnosis numbers below.
**Phase b: code in, partly effective, NOT accepted.** `PmcSquadSubsystem::AssignFiringPositions` (called from `RefreshDefence`, behind `firing_positions.defence_use`): per-man weapon band (0.2-0.9 of reach, tier 2 = 1.8x), ground = Corner / LowCover slots the man TAKES (TakeCover, so `bInCover` counts as on position), hammers = crossfire pair across the first two men's candidate sets, others the best spaced position; roof = RoofEdge inside the defended area only (none qualify on X4: the rim is 29 m from the spots, so G008 roof posts stay). Merged origin/main (scripted opponent) into the branch.
Last gate (worker, commit 64bae31c, all via `worker_sync -ShipHead`): FiringPositionSmoke, TacticalPmcSmoke, PmcArenaTacticalSmoke, PmcFireLineSmoke, HitchProbeSmoke, PmcRoofDefenceSmoke, PmcStalemateSmoke = PASS; PmcCrossroadsSmoke / PmcAlleySmoke = KNOWN-FAIL (still red on purpose). One seed each, scripted opponent: crossroads on_pos 0.67 (was 0.33), crossfire 33.4 deg (was 9.1 p50 baseline), worst open idle 13.7 s (unchanged), only 1 of 3 men seated (Kessler, Vance: 0 eligible); alley: 0 of 3 seated, on_pos 0.33, idle 16.3 s, crossfire 6.4. Targets (30 seeds p50): on_pos >= 0.67, crossfire >= 45, idle <= 2 s: NOT reached.
Why men are not seated: not yet known. HEAD a0e2d397 adds a per-man log line `[PmcAI] team N: <name> band a..b cm (tier t): V valid positions, C not cover, L off level, F forward of the kill zone, E eligible` - built on the worker (5 DLLs back, build exit 0, 17:33) but the smoke for it did NOT finish. NEXT command (reads the reason):
`$env:CODEX_AGENT='claude'; powershell -ExecutionPolicy Bypass -File Scripts/Tools/worker_sync.ps1 -ShipHead -Commands PmcCrossroadsSmoke,PmcAlleySmoke -Jobs 2 -NoRetry -AllLogs` then `grep "band .* tier" Saved/Logs/Worker/<newest>/Smoke-PmcCrossroadsSmoke.log`. Likely fixes by the counts: many "not cover" -> allow Window kind / a post stance beside the wall; "off level"/"forward" high -> relax; few "valid" -> the area / band (a shotgun man's 15 m reach has almost no cover on the open Depot street; widen tier 2 or let him seat at any cover within the objective's defended area).
Then re-run the 9-smoke gate (list in the brief), `python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude` (it audits UNSTAGED changes only), and tell the lead the ref for the 30-seed day loop.

Traps found on the way (useful for the next agent):
- Another agent's `worker_sync` resets the worker tree + DLLs between my `build.ps1 -Remote worker` and my `smoke_parallel -Remote on` ("smokes only" then runs THEIR build and the smoke times out on a console command that does not exist). Use `Scripts/Tools/worker_sync.ps1 -ShipHead -Commands <smokes> -Jobs 2 -NoRetry -AllLogs` (ships HEAD + DLLs and runs in one locked session).
- `Scripts/test.ps1` prints "N tests, 0 failed" when the editor CRASHES mid-run (it counts only completed tests): a `TArray::Add` of its own element asserted in my Pick test and the run reported 5 of 8 tests green. Compare the test count with the number of `IMPLEMENT_*` in the file.
- `pmc_commander.json` is flattened by the night trainer into genes: any nested numeric key (my `firing_positions` block, and the G008 defence keys, which are not in `pmc_commander.bounds.json`) is MUTABLE by default (+-50 %). The C++ reads `firing_positions` from the DEFAULT file only, so trainer copies do not change it, but `apply_champion.ps1` copies a champion over the default file. To freeze: add `"/firing_positions/..."` entries with `"frozen": true` to `pmc_commander.bounds.json` (outside my allowed paths: lead's call).

## Diagnosis (task step 1) - X4, roof `B_hall_n`, spot 0 (5600, -750, 900)

Numbers from `FiringPositionSmoke` (`Saved/Logs/Worker/20261119-162217/Smoke-FiringPositionSmoke.log`). Real shot line = what `AOperativeCharacter::EvaluateShotLine` traces: `ECC_Visibility`, from the muzzle (feet + 140 standing / 85 crouching / 25 prone, `GetMuzzleLocation`) to the target's feet + 80, operatives and turrets ignored.

1. **The tactical file's overwatch spot is not at the rim.** The roof's walk surface runs on 28.6-29.1 m past the spot toward the street (rim at 2860-2910 cm). From the spot the line to a street point 15-40 m beyond the rim is blocked by the roof slab itself (`StaticMeshActor_2794`, impact +0 cm over the floor) at 665 cm, 806 cm and 1244 cm; the 80 cm rim step of the brief could never help (the first clear point is 28-29 m away).
2. **At the rim the parapet top is +95 cm** (`StaticMeshActor_2801`). Standing (muzzle +140) is CLEAR at the rim and 40-80 cm behind it for targets 15+ m past the rim. **Crouching (muzzle +85) is blocked at every rim step**: impact +37..+85 cm over the floor, top +95 cm, 5-97 cm from the muzzle. Prone is blocked too (+11..+25 cm).
3. **The sniper rifle kneels for every shot** (`SniperRules::FiringStance`, log `[Sniper] ... kneels for the shot (fire posture)`): a posted sniper always fires from the 85 cm muzzle, under a 95 cm parapet. In the smoke, for every queried area a sniper (crouching-only query) gets 0 roof-edge positions (and 0 positions of ANY kind) while a rifleman gets 16 roof edges over the 5 areas. Together with item 1 this is the cause of "0 shots from 203 overwatch posts" and of G008b's unfinished rounds.
4. The nav floats ~8 cm over the floor: a first version that measured muzzle / aims from nav points was 8 cm optimistic (a low-cover trial failed on a crate top by 3 cm). All stand points and aim points are floor-snapped now.

**Generator findings (roof kinds that cannot fire) - DECIDED: firing gaps in the parapet (designer 11-19); snipers keep kneeling, `SniperRules` unchanged; the building generator adds embrasures + rim overwatch spots later. T-05 keeps the `parapet blocks every stance` log and the exclusion as is.** (a) a roof-edge position for a KNEELING shooter (sniper) needs a parapet gap or a parapet <= ~75 cm; with the 95 cm parapet none exists: either the generator leaves firing gaps / a lower parapet on overwatch roofs, or snipers must be allowed to stand (SniperRules). (b) the overwatch spots in `*.tactical.json` should be at the rim (the analysis ignores the roof slab), or the runtime derives the rim as done here. (c) `[FiringPos] roof <id>: parapet blocks every stance` is logged per spot (X4: 8-9 of ~44 spots per area).

## What was built (phase a)

| File | What |
|---|---|
| `Public/AI/FiringPositionRules.h`, `Private/AI/FiringPositionRules.cpp` | pure rules: `FFiringPosition` (+ `FireOrigin`, `RimStepCm`), `FFiringPositionConfig` (JSON), `ScoreF2` (GDD F2 exactly), `CoveredShare`, `CrossfireAngleDeg / InLineOfFire / CrossfireOk`, `SpacingOf` (G008 spacing), `PickN`, `PickCrossfirePair`, `AreaSamplePoints`, `SampleInBand`, `RoofEscape / RoofProtectOf`, `RimSteps`, `FiringStances` mask |
| `Public/AI/FiringPositionSubsystem.h`, `Private/AI/FiringPositionSubsystem.cpp` | `UFiringPositionSubsystem`: candidates, validation traces, per-area cache, `PositionsCovering`, `GetCandidates`, `PickCrossfirePair`, `RetracePosition`, `DiagnoseRoofSpot`, `FindRimDistance`, console `CodexTactics.FiringPositions <radius_m> [band_min_m band_max_m] [count]` |
| `PmcSquadSubsystem.{h,cpp}` | read helper `CoverSlotAlongRay` (the ray + `FindCoverSlotAt` step of `FindCoverNear`, which now calls it: no behaviour change) |
| `Content/Data/AI/pmc_commander.json` | new block `firing_positions` (every number commented) |
| `CodexTacticsTests/Private/AI/FiringPositionRulesTest.cpp` | 8 tests `CodexTactics.AI.FiringPosition.*` (F2, share, crossfire, pick, pair, samples, roof terms, config + shipped-file mirror) |
| `CodexTacticsDev/Private/Debug/FiringPositionSmokeCommand.cpp`, `Scripts/test_map.json` | `FiringPositionSmoke` + registry / options / impact rule |

Design points / deviations from the brief (review these):
- Kinds built: Corner, LowCover (cover slots), Window, RoofEdge. **Excluded: StairHead** (the tactical file has no stair-head geometry; the layout has stair tops but not the flight) and **ObstacleTop** (mantle edge data is on the unmerged building-generator branch): enum values only.
- `sample_height_cm` is **80**, not 120: `EvaluateShotLine` aims at feet + 80, so a clear line here is a clear real shot. Samples must be standable: navmesh + a capsule overlap test (a crate / wall edge under a sample is no place for a target).
- Rim: `rim_step_max_cm` 80 counts BACK from the rim (end of the walk surface toward the area, `rim_search_max_cm` 6000), not from the file's spot (diagnosis item 1). Walk surface = navmesh within 12 cm, else a floor trace.
- Barricade hits count as blocked (the real rules would shoot over a 60 cm barricade): conservative.
- Corner positions are validated from the corner lean point (`CoverRules::CornerLeanPoint`), in either stance; their REAL shot needs the man in cover, so the smoke only re-traces them.
- `escape` is a stand-in (roof: access tokens / ladder-only; corner 1.0, low cover / window 0.6): no second-way-out data exists yet (T-08).
- Cost per area (one gather, then cached): 0.7-1.7 k traces, 40-80 ms on the worker (X4, 5 areas); cover scan capped by `max_scan_points` 160 / `max_cover_slots` 120.
- Query takes a `StanceMask` (default stand | crouch; a sniper query is crouching-only).

## Gate results, phase a

| Gate | Result |
|---|---|
| remote build `Scripts/build.ps1 -Remote worker` | OK (exit 0, 6 builds; first 6.1 min) |
| unit tests `CodexTactics.AI.FiringPosition` | 8 / 8 pass; `CodexTactics.AI.Pmc*` 69 / 69 pass - run LOCALLY on the main PC with `Scripts/test.ps1` against the DLLs the worker built and sent back |
| `FiringPositionSmoke` (worker, X4) | PASS (Saved/Logs/Worker/20261119-162217): every returned position valid + re-traced clear (>= 1 sample, recorded share), one man per cover object, 2nd query = cache, flushed cache = same list; 5 real-shot trials (roof edge / window / low cover) `SHOT: ok` and the m16 shooter fires; roof diagnosis numbers above |
| regression smokes TacticalPmcSmoke, PmcArenaTacticalSmoke, PmcFireLineSmoke, HitchProbeSmoke, PmcRoofDefenceSmoke, PmcStalemateSmoke (worker, commit e8b1315c) | all PASS (Saved/Logs/Worker/20261119-161632) |
| L_Lab_Alley | not covered (exists only on the worker / bgen branch) |
