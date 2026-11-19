# T-05 - firing positions that can actually fire (report)

Agent: Claude sub-agent, worktree `CodexTactics-agents`, branch `claude/firing-positions` from origin/main c72394c0. Not merged, main not touched.
Spec: roadmap T-05, GDD smart-enemies section 5 + F2, brief `docs/port/claude/inbox/T005-firing-positions.md`.

## Handover (kept current)

**Worker stand-in lead, 11-19 20:15 (supersedes the iteration notes below where they differ).** Ref: worker/t05-firing-positions after c48f077f, kill_zone_reach_factor 0.25.
30 seeds, scripted opponent, medians (on_pos / idle_max s / crossfire deg): crossroads main 0 / 18-19 / 9-14 -> iter 1 (K = objective) 0.33 / 4.1 / 19.0 -> 0.25: 0.33 / 3.1 / 22.8;
alley main 0 / 14-17 / 17-18 -> iter 1 0 / 9.4 / 15.3 -> 0.25: 0 / 8.6 / 18.9. Factor sweep, seeds 1-10: 0.6 0 / 1.9 / 15.2 and 0 / 8.8 / 12.9; 0.4 0.17 / 5.4 / 16.5 and 0 / 8.8 / 15.2; 0.25 0.59 / 1.9 / 23.1 and 0 / 13.6 / 20.7.
Gate on this ref: build OK, FiringPosition 11/11, Pmc 70/70, FiringPositionSmoke / TacticalPmc / PmcArenaTactical / PmcFireLine / HitchProbe / PmcRoofDefence / PmcStalemate PASS (smoke_parallel -Remote off, slot1).
Acceptance (on_pos >= 0.67, crossfire >= 45, idle <= 2 s on both) NOT met. Findings (Saved/Scripts/first_shot_geometry.py on the track files):
(1) the crossfire metric is the pair angle AT THE ATTACKERS at the round's first shot, and the attackers fire first in most seeds from 25-62 m - when a pair IS seated around the approach kill zone it reads 47-72 deg;
(2) on_pos: the arena spawns defenders ~8 m from the objective; a firing position 25-36 m out is reached after contact (caught walking) - next lever: seat only positions the man reaches before the attackers' expected arrival (walk time vs attacker path time), in AssignFiringPositions;
(3) alley: some approach points have no cover that sees them (0 valid of 330 scan points) - ground cover cannot make the A-00B crossfire; roadmap gives height / mobile anvil to T-08b.


**Phase a: DONE and gated** - commit `0bf3fbd2` (rules + service + 8 unit tests + `FiringPositionSmoke` PASS on X4 + regression smokes green). Diagnosis numbers below.
**Phase b (worker stand-in lead, branch `worker/t05-firing-positions`, commits `1ad3f83b`, `c4f8f561`): seating fixed on the crossroads, alley still geometry-limited, NOT accepted (30-seed day loop is the lead's).**
Diagnosis with the new `[FiringPos] cover breakdown` log (seeds 1-2, both smokes): candidate starvation by scan density was NOT the main cause (35-89 slots found per gather, all on level, all in ring). Real causes: (1) the polar grid thinned by ONE global stride made a wider ring sparser (tier 2 found fewer slots than tier 1: 143 -> 97 scan points) and the slot cap counted before the ring filter; (2) 40-50 % of the slots are high walls without an exposed edge (no corner to lean round), another 30-45 % see no area sample; (3) the "forward" test left E = V - F eligible (F = V in most rows); (4) alley: the kill zone is not visible from the ground ring at all (a diagnostic of free-standing nav points in the 11-54 m band: 14 / 63 / 35 see >= 1 sample and 0 / 14 / 0 see >= 4 of 9 on seeds 1 / 2 / 3, so no cover slot there can fire).
Done: one band-independent cover scan per area (`UFiringPositionSubsystem::EnsureCoverScan`, world-anchored grid nearest first, far lines thinned, cut at `max_scan_points` 400; each slot keeps its stance x lean variants and a bit mask of the area samples they reach; a man's band is applied to the masks at query time, so a wider band always sees a superset; `PrepareCoverScan` once with the widest band from `AssignFiringPositions`); slots counted after the ring / level filter; forward rule on the approach axis (`FiringPositionRules::IsForwardOfKillZone`, `forward_margin_cm` 800); all phase b literals in the `firing_positions` JSON block (band factors, area radius floor, roof reach factor, default reach, margin, scan density, `min_cover_quality`) mirrored in the config unit test; unit tests `CodexTactics.AI.FiringPosition.*` 10 / 10 (CoverScanPoints superset + grid + cap, BandAndForward, Config), `CodexTactics.AI.Pmc*` 70 / 70.
Result, crossroads seeds 1-5 (first refresh, scripted opponent, Depot stand-in): shooters seated 3,2,3,3,3 of 3 (before, lead's diag seeds 1-2: 0-1 of 3; commit 1 with margin 300: 3,1,3,2,3). Metrics: on_pos 0.67, 0.67, 0.00, 0.67, 0.67; crossfire at contact 29, 31, 17, 19, 34 deg; worst open idle 0.5, 14.5, 5.7, 3.3, 5.9 s. The chosen pair's crossfire is 94 / 143 / 46 deg when a pair exists (seeds 2, 4, 5), -1 (no pair among 2-4 eligible) on seeds 1 and 3. Alley seeds 1-5: seated 1,0,0,0,0 of 3 (valid positions 0 on 4 of 5 seeds: cause 4), on_pos 0.33 / 0.33 / 0 / 0 / 0, idle 17.5 / 23.3 / 39.1 / 11.7 / 45.3 s. Targets (on_pos >= 0.67, crossfire >= 45, idle <= 2 s, 30 seeds p50) NOT reached.
Gate (local, `smoke_parallel -Remote off`, commit c4f8f561): FiringPositionSmoke, TacticalPmcSmoke, PmcArenaTacticalSmoke, PmcFireLineSmoke, HitchProbeSmoke, PmcRoofDefenceSmoke, PmcStalemateSmoke = PASS. Cover scan cost: 40-117 ms once per area per round (median 92 ms, 300-350 scan points, 700-1900 traces), then cached (gathers of other bands 0.3-3 ms); logged as `[FiringPos] cover scan ... ms`.
NEXT exact steps: (a) crossroads seed 3 seats 3 of 3 but on_pos 0.00 at contact (seated men leave the position / are not counted in cover: read the track file of `Saved/Logs/SeedBatch/t05b-final-cross/seed3` and the walk / TakeCover log lines of the three men); crossfire stays < 45 because only 2-4 positions are eligible per man: raise it by letting the pair search use all candidates of both men (it does) and by widening the pool: try `min_covered_share` 0.22 (2 of 9 samples) or window / open-ground kinds on the ground; (b) alley: the defended area is not visible from any cover on the ground; either move the kill zone (objective is inside the block) to the alley mouth / the street the attackers approach along, or allow window stands on ground floors (Window kind is excluded on the ground today: `not cover` counter), or accept the G008 posts there and drop the alley from the T-05 acceptance; (c) then the lead's 30-seed day loop on commit c4f8f561 or later.

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
