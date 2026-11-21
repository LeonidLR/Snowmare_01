# T-08 slice 2 - test infrastructure for the A/B: zone cover, DefenceRoles switch, rule 4 after the first shot (report)

Agent: Claude sub-agent (Opus), worktree `CodexTactics-grok`, branch `ai/t08-slice2` (from 8b5883d8 = worker/t08-holder-fallback + origin/main 36a4bbcc). Spec: the lead's brief 2026-11-21, designer decisions 2026-11-21 (the defended zone gets cover; fall-back rule 4 only after the hammers have opened fire), slice-1 report `T08-holder-fallback.report.md`. Not merged. No A/B run (the lead's).

## What was built (every switch OFF by default = today's behaviour)

| Arena arg | Effect | Where |
|---|---|---|
| `ZoneCover=0\|1` | each objective round (any scenario) gets low-cover blocks inside the objective zone facing the attackers' approach, placed after the site / spawns are chosen and BEFORE any squad spawns (so before the defence plans and `BuildZonePosts`); the round then waits for the navmesh rebuild (new stage 5), spawns, and destroys the blocks at its end | `PmcArenaCommand.cpp` `PlaceZoneCover` / `CheckZoneCoverNav` / `ClearZoneCover`, pure layout `AI/PmcZoneCoverRules.h` |
| `DefenceRoles=0\|1` | 1 = for the run `firing_positions.defence_use` + `keep_occupier` (`UFiringPositionSubsystem::SetConfig`) and `defence_roles.enabled` (new `UPmcSquadSubsystem::SetDefenceRolesConfig`) on; 0 / absent = the shipped JSON | arena stage 0 |
| `Rule4AfterFire=0\|1` | overrides the new JSON key `defence_roles.rule4_after_first_fire` (shipped false); absent = the JSON's value | `PmcDefenceRolesRules::DecideFallBack` + arena stage 0 |

- **Zone cover layout (pure, `PmcZoneCover::*`, numbers in the new commented `Content/Data/AI/pmc_arena_zone_cover.json`):** approach = unit vector from the zone centre to the point where the attackers' nav path (their spawn -> the objective, the same query the arena's "objective paths" line uses) first crosses into the zone radius (segment / circle intersection, also for a segment that passes through without a point inside); fallback = toward the attackers' spawn. Slot 0 centred 3.5 m out on the approach bearing, slots 1 / 2 at +-60 deg 4.0 m out; 200 x 40 x 110 cm (LOW cover: < 130 cm, > the 45 cm knee trace); long side perpendicular to the approach (`side_yaw_follow` 0; 1 turns the side blocks to their own bearing). Every footprint corner must lie inside radius - 20 cm, else the block is pulled in 25 cm steps down to 1.5 m, else dropped; a block overlapping an earlier one (SAT) is dropped.
- **World side:** per block - on the navmesh, ground trace (pawns ignored) within 100 cm of the objective's Z, an overlap box (lifted 10 cm, 2 cm inset) against WorldStatic / WorldDynamic / PhysicsBody / Destructible and pawns, footprint >= 3 m from either side's spawn; else skipped with the reason in the log. Spawned as `AStaticMeshActor` with `/Engine/BasicShapes/Cube` (Movable before the mesh is set, `BlockAll`, so it blocks ECC_Visibility for `CoverSlotAlongRay`; affects navigation; tag `PmcArenaZoneCover`); the firing-position cache is invalidated after spawn and after removal. Stage 5 waits >= 0.3 s, then until `IsNavigationBeingBuiltOrLocked` is false (timeout 30 s: warning, the round goes on, `zone_cover_nav_ready` false). After the wait it checks the navmesh is cut under each block (evidence the blocks affect navigation). After the spawn it counts fighters standing inside a block (must be 0; `SmokeUtils::FreeSpot` + the navmesh keep them out).
- **StartRound split:** `PrepareRound` (seed, attacker, site, spawns, objective + its reach / paths logs) and `SpawnRound` (weather, squads, orders, result record, track); without `ZoneCover=1` both run in the same step exactly as before.
- **Rule 4 gate:** `FPmcDefenceRolesConfig::bRule4AfterFirstFire`; when set, rule 4 is added only when `bDefenceHasFired` (the slice's first-shot flag, `Team.Defence.FirstShotAt`). `SlackS` is still computed. Rules 1 / 2 and the finish gate are unchanged (test covers both settings, rule 1 before the first shot, the finish gate, rule 2).
- **Result JSON:** `rounds[].config` = `zone_cover`, `defence_roles` (the args as given), `rule4_after_fire`, `defence_use`, `keep_occupier`, `defence_roles_enabled` (effective values) and, with ZoneCover=1, `zone_cover_blocks`, `zone_cover_skipped`, `zone_cover_approach_from_path`, `zone_cover_approach_yaw_deg`, `zone_cover_nav_ready`, `zone_cover_nav_cut`, `zone_cover_fighters_inside`. Log lines: `[PmcArena] variant: ...` (stage 0, every run), `[PmcArena] zone cover: N blocks at ... (approach dir ..., skipped M: reason)`, `zone cover: navmesh rebuilt after X s, cut under N of M blocks`, `zone cover: N blocks removed (round R over)`.
- **Trainer trap:** the only new key in `pmc_commander.json` is the bool `rule4_after_first_fire` - not a gene (`genome.py` skips bools), so nothing to freeze in `pmc_commander.bounds.json`. The zone-cover numbers live in their own file the trainer never reads. (The slice-1 `defence_roles` numbers are still unfrozen - unchanged, the lead's call.)

## Files

| File | |
|---|---|
| `Source/CodexTactics/Public/AI/PmcZoneCoverRules.h`, `Private/AI/PmcZoneCoverRules.cpp` | new pure rules: `FPmcZoneCoverConfig`, `FPmcZoneCoverBlock`, `ConfigFromJson` / `LoadConfig`, `ApproachDirection`, `LayoutBlocks`, `FootprintCorners`, `IsInsideZone`, `FootprintDistance`, `FootprintsOverlap` |
| `Content/Data/AI/pmc_arena_zone_cover.json` | new, commented, the numbers above |
| `Source/CodexTactics/Public/AI/PmcDefenceRolesRules.h`, `Private/AI/PmcDefenceRolesRules.cpp` | `bRule4AfterFirstFire` + JSON read + the rule 4 gate |
| `Content/Data/AI/pmc_commander.json` | `defence_roles.rule4_after_first_fire: false` + comment |
| `Source/CodexTactics/Public/AI/PmcSquadSubsystem.h` | public `GetDefenceRolesConfig` / `SetDefenceRolesConfig` (runtime override; the shipped file is untouched) |
| `Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp` | the three args, PrepareRound / SpawnRound split, stage 5, zone cover world side, `rounds[].config`, header doc |
| `Source/CodexTacticsTests/Private/AI/PmcZoneCoverRulesTest.cpp` | new: `CodexTactics.AI.PmcZoneCover.Approach / Layout / Footprint / Config` |
| `Source/CodexTacticsTests/Private/AI/PmcDefenceRolesRulesTest.cpp` | new `CodexTactics.AI.PmcDefenceRoles.Rule4AfterFirstFire`; Config test reads / mirrors the new key |

## Gate

| Gate | Result |
|---|---|
| `Scripts/build.ps1` (local, no designer editor open) | Succeeded; the unity-clash checker lists only the 4 pre-existing clashes (my helper names renamed) |
| `CodexTactics.AI.PmcZoneCover` | 4 / 4 |
| `CodexTactics.AI.PmcDefenceRoles` | 6 / 6 (5 + Rule4AfterFirstFire) |
| `CodexTactics.AI.FiringPosition` | 13 / 13 |
| `CodexTactics.AI.Pmc` | 80 / 80 (75 + 1 + 4) |
| defaults (no new args), `smoke_parallel -Remote off -Jobs 3` | FiringPositionSmoke PASS, PmcRoofDefenceSmoke PASS, PmcStalemateSmoke PASS, PmcCrossroadsSmoke / PmcAlleySmoke KNOWN-FAIL (as on main); no `zone cover` line in any log; `[PmcArena] variant: ZoneCover=0 DefenceRoles=0 Rule4AfterFire=json -> defence_use 0, keep_occupier 1, defence_roles.enabled 0, rule4_after_first_fire 0`; `rounds[].config` all false except `keep_occupier` true (the shipped value) |
| `Rule4AfterFire=1` (one seed, `Seed=2 ZoneCover=1 DefenceRoles=1 Rule4AfterFire=1`) | variant line shows `rule4_after_first_fire 1`; the only fall-back came after "the defence fires its first shot" (rule 2+4); round A by elimination (the same seed without the gate: B by elimination - one seed, no conclusion) |

## Sanity, 5 seeds each (`smoke_parallel -Commands PmcCrossroadsSmoke -Repeat 5 -SmokeArgs "Seed={n} ..." -Remote off -Jobs 3`, L_Gen_Depot_1, Street, scripted attackers B; knownFail by design)

Columns: blocks placed / skipped; approach yaw and its source; navmesh cut under blocks / wait; fighters inside a block; slice log `zone posts: N covered slots inside the zone`; holder lines "covered zone post" / "no covered post he can reach"; fall-back rules fired; round result; survivors.

`ZoneCover=1` (the layer itself off: no zone-post scan runs - BuildZonePosts belongs to `defence_roles.enabled`):

| seed | blocks / skipped | approach | nav cut | inside | zone posts | result | survivors |
|---|---|---|---|---|---|---|---|
| 1 | 3 / 0 | 83 deg, path | 3/3, 0.3 s | 0 | - | B by elimination | A0 B1 |
| 2 | 3 / 0 | -164 deg, path | 3/3, 0.3 s | 0 | - | B captured | A1 B1 |
| 3 | 3 / 0 | -14 deg, path | 3/3, 0.3 s | 0 | - | A by elimination | A3 B0 |
| 4 | 3 / 0 | -82 deg, path | 3/3, 0.3 s | 0 | - | B captured | A1 B3 |
| 5 | 3 / 0 | -17 deg, path | 3/3, 0.3 s | 0 | - | B captured | A1 B3 |

`ZoneCover=1 DefenceRoles=1`:

| seed | blocks / skipped | approach | nav cut | inside | zone posts | holder on zone post / not | fall-backs (rules) | result | survivors | smoke |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 3 / 0 | 83 deg, path | 3/3 | 0 | 17 | 0 / 1 | - | A by elimination | A3 B0 | FAIL |
| 2 | 3 / 0 | -164 deg, path | 3/3 | 0 | 17 | 2 / 0 | 1 (4) | B by elimination | A0 B1 | PASS |
| 3 | 3 / 0 | -14 deg, path | 3/3 | 0 | 12 | 1 / 0 | 1 (4) | A by elimination | A3 B0 | FAIL |
| 4 | 3 / 0 | -82 deg, path | 3/3 | 0 | 12 | 1 / 1 | 1 (4) | B captured | A2 B3 | FAIL |
| 5 | 3 / 0 | -17 deg, path | 3/3 | 0 | 13 | 2 / 0 | 1 (2+4) | B by elimination | A0 B3 | PASS |

Slice 1 found 0 covered zone posts on 4 of these 5 seeds; with the blocks every seed finds 12-17 (the best post is a block on every seed). All 30 planned blocks placed, none skipped, nobody inside a block, every round finished (35-78 s). 5 seeds are a smoke, not a verdict.

## Notes for the lead (read before the A/B)

1. **Hammers may take the blocks as their T-05 firing positions.** Seed 1: hammer Kessler's fall-back plan is "route 0.1 m from his firing position" - the crossfire pair picker chose a block inside the zone as his firing position (low cover in range of the kill zone is a valid candidate). So ZoneCover=1 changes the hammers' positions too, not only the holder's. If the A/B should isolate the holder / fall-back effect, the lead may want the firing-position scan to exclude the blocks (e.g. by the `PmcArenaZoneCover` tag) - not done here (a design call; it changes what "cover in the zone" means for the hammers).
2. **The holder still keeps his G008 post on 2 of 5 seeds** ("is the holder but the zone has no covered post he can reach (17 posts)") - slice 1's route test (a holder route must not cross the kill zone) refuses the zone posts from where his G008 post stands. Unchanged slice-1 behaviour; it caps how often the layer can run even with cover.
3. **Parallel smokes overwrite each other's result JSON** (pre-existing): the arena names its result `Saved/Arena/<timestamp to the second>.json`; smokes started in the same second wrote the same file (my seed 1-3 runs of a batch share one JSON). Logs are per run. For the 150-seed A/B use `Out=<per-run path>` or judge the logs; the `rounds[].config` echo is only trustworthy per file when names do not collide.
4. `DefenceRoles=1` sets `defence_use` / `keep_occupier` world-wide (both teams' configs); only the defence code reads them, so in practice it is "for the defending team". `keep_occupier` already ships true.
5. Fall-back rule 4 alone still dominates in these 5 seeds (3 of 4 fall-backs, the 4th was 2+4) - the A/B with `Rule4AfterFire=1` will show the difference.

## Deviations

- Count is clamped to 1..3 (the three specified slots); more blocks would need a layout rule the brief did not give.
- The approach for the side blocks: they face the approach (as the centre block), not their own bearing; switchable by `side_yaw_follow`.
- A navmesh timeout does not fail the run (unlike the stage-0/1 gate): it is logged and echoed (`zone_cover_nav_ready` false).
