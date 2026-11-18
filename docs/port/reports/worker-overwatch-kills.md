# overwatch_kills = 0 on L_Gen_Depot_X4: evidence (worker-ops, 2026-11-18)

**Verdict:** the metric is correct; the behaviour gives 0. Defenders are put on the overwatch roof posts, but the commander
pulls them off before the attackers come into range, so nobody ever fires from a post. Fix = the commander (task G008b),
not `PmcArenaBehaviourRules`.

Data: trainer run `pmc-v6-qd` (QD trial on X4, scenario tactical, 2026-11-18 06:00-08:00), 552 side-rounds,
`Saved/Reports/worker-2026-11-18/pmc-v6-qd/gen_*/jobs/*.arena.json` + `*.arena/round_*.track.json` + job logs.
Note: track `from` / `who` are display names and the two squads can share one ("Operator Dahl" on A and on B): key fighters by (name, side).

## 1. The metric works

- `IsAtOverwatchSpot` (`Source/CodexTactics/Private/AI/PmcArenaBehaviourRules.cpp:178`): 2D <= `SpotRadiusCm` 300 and the same floor
  (`floor_tolerance_cm` 150, `Content/Data/LevelGen/tactical_runtime.json`).
- A post defender stands exactly on the spot. Example (job `g0000_c03_eval_champion_0`, round 1):
  `placed on a overwatch post V(X=-7713, Y=-1765, Z=310) ... B_x1_9_t7_gallery`, and the spawn line has `feet Z=302.150`.
  Spot Z is 300: dZ 2 cm, 2D 0 cm.
- So any hit from a post would be flagged. The flag is set in `PmcArenaCommand.cpp:1728` and becomes a kill at `:1765`.

## 2. Nobody fires from a post

The 203 side-rounds that start a defender on an overwatch spot (2D <= 5 cm at frame 0):

| Measure | Value |
|---|---|
| Shots fired while still within 3 m of the post | **0** (0 hits) |
| Time on the post (still within 3 m), p10 / p25 / p50 / p75 / p90 | 4.0 / 5.8 / **17.2** / 30.6 / 33.4 s |
| First non-Ambush order, p10 / p25 / p50 / p75 / p90 | 0.5 / 3.0 / **10.5** / 30 / 31 s (200 of 203) |
| The man's first shot anywhere, p10 / p25 / p50 / p75 / p90 (113 of 203 ever fire) | 15.5 / 18.9 / **36.8** / 64.7 / 90.8 s |
| First two orders (most common) | Ambush -> Objective 77, -> TakeCover 36, -> FallBack 36, -> Advance 26, -> Flank 21 |

Across all hits in the run, 6229 hits but `overwatch: true` on 1. Kills: 605, of which 0 were overwatch kills.

## 3. Why: the threat is out of range, and the first contact kills the Ambush score

1. **At t = 0 the post defender already knows a threat 120-155 m away.**
   Log: `team 1: Operator Kessler opens fire (threat at 12125 cm)` and `Vance ... 15487 cm`.
   - Sniper roof `range_m` is 110 (`L_Gen_Depot_X4.tactical.json`, `overwatch_roofs[].range_m`).
   - The posts are picked along the squad's 160 m route (`PmcSquadSubsystem.cpp:2672-2716`), so the attackers are still out of range.
2. **That contact sets `Team.bEverContact = true`.**
   It happens at `PmcSquadSubsystem.cpp:736` (any known enemy) or `:355` (encounter engaged), and reaches the view at `:1425`.
3. **The Ambush score is gated on "no contact ever".**
   `PmcCommanderRules.cpp:204`: `W.Ambush * Step(!S.bEverContact) * ...`.
   - Once the team has had contact, Ambush scores 0, so the post bonus (`PostKindFactor` / `overwatch_roof` weight, `PmcTacticalRules.cpp:53`) no longer counts.
   - The next decision picks the best other order: Objective, TakeCover, FallBack, Advance or Flank. The walk takes him off the roof.
   - `ShouldHoldFire` (`PmcCommanderRules.cpp:868`) is not the cause: the logs show "opens fire" at once (attention off 1.00). There is simply nobody in range yet.
4. **Result:** the attackers reach the post's range (median first shot of these men is 36.8 s) only after the man left the post
   (median 17 s).

## 4. Suggested fix (G008b, lead's call)

- Keep an overwatch post until real contact **within the post's range**, or until `ambush_max_hold` runs out.
  For example, in the Ambush case, replace `Step(!S.bEverContact)` for a man with `F.PostKind == Overwatch` by
  "no enemy seen within the roof's range_m" or `ContactSeconds < AmbushMaxHoldSeconds`.
- Put the numbers in `pmc_commander.json`.
- Check it on X4 with the QD metric itself: `overwatch_kills` > 0, and shots from posts > 0 in the tracks.
