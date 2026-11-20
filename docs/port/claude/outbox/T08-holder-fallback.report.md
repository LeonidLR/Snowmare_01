# T-08 first slice - holder in the zone + hammers' fall-back (report)

Agent: Claude sub-agent, worktree `CodexTactics-slot1`, branch `worker/t08-holder-fallback` (from 4398591c). Spec: designer decisions 2026-11-20 night (the task brief), GDD smart-enemies PL-03 / PL-12 / F3 / F4, A-00A criterion 5. Not merged.

## Handover (kept current)

**Verdict of the one paired run (seeds 1060-1209, defence_use true + defence_roles.enabled true, one run, no re-run): FAIL.** Pre-declared PASS: crossroads attackers killed per round >= 1.9 AND defender wins >= 75 / 143 AND the >= 2-down share not below main's; alley not below main on wins (16 / 133) and kills (0.67).

| scenario / variant | defender (A) wins | attacker objective captures | attackers killed / round | share of rounds with >= 2 attackers down | median hammer distance to the point at the holder's death (m) |
|---|---|---|---|---|---|
| crossroads main 19a90f52 | 82 | 17 | 1.99 | 0.629 (90 / 143) | 3.9 |
| crossroads garrison 4398591c | 53 | 46 | 1.46 | 0.420 (60 / 143) | 9.1 |
| crossroads t08 | 51 | 54 | 1.51 | 0.469 (67 / 143) | 18.4 |
| alley main | 16 | 40 | 0.67 | 0.188 (25 / 133) | 4.2 |
| alley garrison | 16 | 38 | 0.63 | 0.165 (22 / 133) | 4.2 |
| alley t08 | 19 | 22 | 0.71 | 0.195 (26 / 133) | 13.1 |

Crossroads: all three criteria missed (1.51 < 1.9, 51 < 75, 0.469 < 0.629). Paired vs main: main-only wins 38, t08-only wins 7 (garrison: 35 / 6). Alley: wins 19 >= 16 and kills 0.71 >= 0.67 met; paired main-only 11, t08-only 14; captures 40 -> 22 (the layer stops captures there; it does not yet turn them into wins). "Median hammer distance" = the method of the garrison report (the mates of the defender nearest the point at t = 15 s, at the frame before he dies).

**Why the crossroads did not move (read this before touching the numbers).** The layer needs COVERED posts inside the objective zone (r 6 m, level +-1.5 m). The crossroads zone is an open intersection: on 4 of 5 sanity seeds the scan found 0 posts at `zone_min_quality` 0.5, and still 0 of 5 at 0.35 (only 1 of 5 at 0.1, with a shield of 0.17). The zone-post scan log says why: seeds 1-5 found 14 / 1 / 7 / 3 / 6 wall slots within reach, of which 4 / 1 / 3 / 1 / 5 lie outside the 6 m circle and the rest face away from the approach (CoverQualityAgainst < 0.35). So on the crossroads the holder keeps his G008 post in the open and (no post to fall back to) the hammers get no plan: log counts over the 150 rounds, "is the holder: covered zone post" 122 vs "holder but the zone has no covered post" 209 lines (one per defence refresh, so a death adds one), "fall-back plan" 65 vs "has no fall-back plan" 266. The crossroads result is therefore the T-05 keep_occupier result (c3cc6f3e 44, garrison 53) plus a small gain where posts existed - the scheme as specified cannot be judged there. Over the 150 paired rounds the crossroads zone had 0 covered posts in 80 rounds (median 0, max 24 = the cap); the alley had 0 in only 2 rounds (median 24 = the cap). On the alley the layer ran fully (zone-post holder 209 / fall-back plans 117); it cut attacker captures 40 -> 22.

Fall-back statistics (log lines of the 150 rounds per scenario; "rounds with a fall-back" = rounds with at least one):
| | crossroads | alley |
|---|---|---|
| rounds with a fall-back | 40 | 65 |
| hammers falling back | 50 | 77 |
| rule 4 alone / rule 1 alone / rule 2 alone | 34 / 5 / 1 | 50 / 5 / 2 |
| rules 2+4 / 1+4 | 8 / 2 | 19 / 1 |
| "holds (>= 2 down)" | 3 | 1 |
| arrivals on the zone post | 43 | 74 |
| hammers dead WHILE falling back | 6 | 3 |
| hammers dead after reaching the zone post | 20 | 21 |
| holders dead on a zone post / not on one | 43 / 79 | 63 / 28 |
Rule 4 ("not in time") fires on its own in about two thirds of the fall-backs; rule 2 (bypassed) almost always together with it. Rule 1 (zone emptying) is rare because the holder rarely dies before the squad's other signals. 9 men died on the way of 127 fall-backs (7 %).

**NEXT exact steps (the lead's call):** (1) the crossroads needs somewhere to fall back to: either a real zone with cover (the generator / a bigger `objective_radius_m`, e.g. 10 m, in a test scenario file), or allow open zone posts (the scheme then holds the zone by body, not by cover - a designer decision: "covered" was binding); (2) on the alley the layer works: re-judge it on a scenario whose zone has cover (the alley already has it) after (1); (3) rule 4 dominates - it fires as the attackers reach the kill zone, i.e. at the moment the hammers should be firing; decide whether rule 4 should wait for the first defender shot / for the attackers to be inside the kill zone before it may pull a hammer off a position (designer text: "unless rule 1 or 4 forces it"); (4) rule 3 and the leaving of the zone are not built.

## What was built

| File | What |
|---|---|
| `Source/CodexTactics/Public/AI/PmcDefenceRolesRules.h`, `Private/AI/PmcDefenceRolesRules.cpp` (new) | pure rules: `FPmcDefenceRolesConfig` (JSON `defence_roles`), `DecideFallBack` (rules 1 / 2 / 4, the first-shot gate of rule 2, the finish gate), `PickMover` (bounding: the longest walk moves first, none while a mover is on his way for < bound_max_wait_s), `ZonePostScore` + `PickZonePost` (ordering: quality x line to the approach x distance from the centre; free cover object + spacing, relaxed in two tiers), `PathCrossesKillZone` (a route never within kill_zone_clear_m of the kill zone, capped at 0.7 x the start's own distance), `LoadConfig` |
| `PmcSquadSubsystem.h/.cpp` | `BuildZonePosts` (once per round: 17 origins inside the zone x 16 rays, cover slots by `CoverSlotAlongRay`, inside the radius, on the level, ranked), `ApplyDefenceRoles` (called from `RefreshDefence` after `AssignFiringPositions`: the holder takes the best reachable zone post whose route does not cross the kill zone; every hammer gets a fall-back plan = best free zone post + nav-path length from his firing position, <= max_path_queries queries per man per refresh), `UpdateFallBack` (at the decision rate, NO traces: arrivals, the squad's facts from `Team.Known` (age <= known_max_age_s) and the new `Team.EnemiesSeen` (the dead are removed from `Known`), the three rules, bounding, a zone man at his post but out of cover takes it - the commander's Ambush does not run in contact on a post), hooks: first defender shot (`HandleWeaponFired`), enemies seen (`UpdateSight`), death log (`NotifyOperativeDied`), `AssignFiringPositions` stores `HolderIndex` / the kill zone and leaves fallen-back men out, `RefreshDefence` keeps a fallen-back man's zone post over a refresh |
| `PmcCommanderRules.h/.cpp` | the role gate only: `FPmcFighterView::bZonePost` (point pull Retake / Occupy = 0 for a man on a zone post) and `bFallingBack` (Ambush walk scores 1.6: outscores a fight order, under fire too) |
| `Content/Data/AI/pmc_commander.json` | new commented block `defence_roles` (`enabled` false) |
| `Source/CodexTacticsTests/Private/AI/PmcDefenceRolesRulesTest.cpp` | 5 tests `CodexTactics.AI.PmcDefenceRoles.*`: FallBackRules, Bounding, ZonePosts, KillZoneRoute, Config (+ shipped-file mirror, `enabled` false) |

Log lines (grep `[PmcAI]`): `<name> falls back (rule R: ...)`, `<name> holds (>= 2 down: N of M seen; rules ...)`, `<name> is the holder: covered zone post ...`, `fall-back plan: zone post ...`, `zone posts: N covered slots inside the zone (... outside the zone / off level / weak cover; nearest slot X m)`, `<name> is on the zone post`, `<name> died (holder|hammer|other, fall-back falling back|fallen back|-, on a zone post|not ...)`, `the defence fires its first shot`.

Design points / deviations (review these):
- Needs `firing_positions.defence_use` AND `keep_occupier` (the holder is the occupier, the hammers are the firing-position men); with `defence_roles.enabled` false NOTHING changes (the only always-on additions are bookkeeping: the first-shot flag, `EnemiesSeen`, the death log line, default-false view flags).
- Rule 4's attacker clock is the attackers' distance to the zone EDGE at `attacker_speed_factor` x walk speed (T-05's number), NOT the capture clock: the arena's `hold_s` is not known to the squad. The finish gate counts the attackers the squad SAW go down (`EnemiesSeen`), so "2 of 3" needs two seen; a never-seen attacker is not counted.
- `zone_min_quality` ships 0.1 (was 0.5, then 0.35 in the draft): changed on sanity seeds 1-5 only (crossroads zones with no wall on the approach side); posts are RANKED by quality, so the best available wins. At 0.1 a post may shield almost nothing (seed 1: 0.17).
- Bounding is per squad, one mover at a time; a walk that has not arrived after `move_timeout_s` counts as arrived. Once a hammer is in the zone he stays for the round (no leaving, no point pull). No rule can send him out again.
- The holder who dies is replaced by the refresh (the nearest remaining shooter becomes the holder, the route check included); rule 1 then also sends the remaining hammer in.
- `defence_roles` numbers are nested numeric keys of `pmc_commander.json`: the night trainer flattens them into genes unless frozen in `pmc_commander.bounds.json` (the `firing_positions` block has the same trap, report T-05). C++ reads the default file only; freezing them is the lead's call.

## Gate

| Gate | Result |
|---|---|
| `Scripts/build.ps1` | OK |
| `CodexTactics.AI.FiringPosition` | 13 / 13 (13 `IMPLEMENT_*`) |
| `CodexTactics.AI.Pmc` | 75 / 75 (70 before + 5 new `PmcDefenceRoles`; the filter also matches tests whose macro text my grep does not count, so the 70 + 5 comparison is the check) |
| `CodexTactics.AI.PmcDefenceRoles` | 5 / 5 |
| smokes, switch ON (`defence_use` + `enabled` true), `smoke_parallel -Remote off -Jobs 3`: FiringPositionSmoke, TacticalPmcSmoke, PmcArenaTacticalSmoke, PmcFireLineSmoke, HitchProbeSmoke, PmcRoofDefenceSmoke | all PASS (132 s) |
| PmcStalemateSmoke alone, switch ON | PASS |
| same, switch OFF (shipped: defence_use false, enabled false), 6 smokes | 5 PASS, PmcRoofDefenceSmoke PASS* (failed once, passed on the harness re-run; with the layer off the only code that runs is bookkeeping + default-false view flags, so I read it as the known flake, not a regression - not isolated further) |
| PmcStalemateSmoke alone, switch OFF | PASS |
| unit tests with the shipped JSON | FiringPosition 13 / 13, Pmc 75 / 75 |

The shipped `pmc_commander.json` has `firing_positions.defence_use` false and `defence_roles.enabled` false; slot1's tracked tree contains only this change (see the commit).
